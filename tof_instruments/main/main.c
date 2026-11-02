// SPDX-License-Identifier: Apache-2.0

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "board_display.h"
#include "instrument_tof.h"
#include "bsp/esp_mosaico.h"
#include "bsp/power.h"
#include "instrument_audio.h"
#include "esp_gsp_esp_lcd.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "game_ui.h"
#include "guitar_tune.h"
#include "iris_ota_support.h"
#include "iris_screen_mirror.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui_bundle.h"

#define MOTION_SAMPLE_MS        10U
#define CALIBRATION_SAMPLES     40U
#define GRAVITY_ALPHA           0.055f
#define ACCEL_FULL_SCALE_G      8.0f
#define GYRO_FULL_SCALE_DPS     2000.0f
#define IMU_RAW_SCALE           32768.0f
#define HIT_COOLDOWN_MS         105U
#define HAPTIC_MUTE_MS          42U
#define HAPTIC_PULSE_MS         13U
#define KODOU_ENABLE_IMU        0

static const char *TAG = "kodou_matsuri";
static kodou_game_t s_game;
static QueueHandle_t s_haptic_queue;
static volatile TickType_t s_motion_mute_until;

static uint32_t game_clock_ms(void *ctx)
{
    (void)ctx;
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static bool tick_before(TickType_t now, TickType_t deadline)
{
    return (int32_t)(now - deadline) < 0;
}

static esp_err_t read_motion(float accel[3], float gyro[3])
{
    struct bmi2_dev *imu = bsp_imu_get_handle();
    if (!imu) return ESP_ERR_INVALID_STATE;
    struct bmi2_sens_data data = {0};
    if (bmi2_get_sensor_data(&data, imu) != BMI2_OK) return ESP_FAIL;
    const float accel_scale = ACCEL_FULL_SCALE_G / IMU_RAW_SCALE;
    const float gyro_scale = GYRO_FULL_SCALE_DPS / IMU_RAW_SCALE;
    accel[0] = data.acc.x * accel_scale;
    accel[1] = data.acc.y * accel_scale;
    accel[2] = data.acc.z * accel_scale;
    gyro[0] = data.gyr.x * gyro_scale;
    gyro[1] = data.gyr.y * gyro_scale;
    gyro[2] = data.gyr.z * gyro_scale;
    return ESP_OK;
}

static void haptic_task(void *arg)
{
    (void)arg;
    uint8_t strength;
    while (true) {
        if (xQueueReceive(s_haptic_queue, &strength, portMAX_DELAY) == pdTRUE) {
            (void)bsp_motor_set_strength(strength);
            vTaskDelay(pdMS_TO_TICKS(HAPTIC_PULSE_MS));
            (void)bsp_motor_set_strength(0);
        }
    }
}

static void game_feedback(kodou_hit_t hit, kodou_judgement_t judgement,
                          uint8_t strength, void *ctx)
{
    (void)ctx;
    (void)kodou_audio_hit(hit, judgement, strength);
    s_motion_mute_until = xTaskGetTickCount() + pdMS_TO_TICKS(HAPTIC_MUTE_MS);
    uint8_t pulse = judgement == KODOU_JUDGE_PERFECT ? 31 :
                    judgement == KODOU_JUDGE_GOOD ? 24 : 15;
    (void)xQueueOverwrite(s_haptic_queue, &pulse);
}

static void sensitivity_changed(int level, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "IMU sensitivity=%d", level + 1);
}

static void volume_changed(int level, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "audio volume level=%d", level);
    (void)instrument_audio_set_volume_level((uint8_t)level);
}

static void volume_pct_changed(uint8_t volume_pct, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "audio volume pct=%u%%", (unsigned)volume_pct);
    (void)instrument_audio_set_volume_pct(volume_pct);
}

static void guitar_strum(int chord, int direction, void *ctx)
{
    (void)ctx;
    instrument_audio_guitar_strum(chord, direction);
    uint8_t pulse = 20;
    (void)xQueueOverwrite(s_haptic_queue, &pulse);
}

static void song_started(kodou_song_t song, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "song started: %d", (int)song);
    (void)kodou_audio_start_song(song);
}

static void song_stopped(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "song stopped");
    (void)kodou_audio_stop_song();
}

static void motion_task(void *arg)
{
    kodou_game_t *game = arg;
    bsp_imu_config_t config = BSP_IMU_CONFIG_DEFAULT();
    config.accel_odr = BMI2_ACC_ODR_100HZ;
    config.accel_range = BMI2_ACC_RANGE_8G;
    config.gyro_odr = BMI2_GYR_ODR_100HZ;
    esp_err_t err = bsp_imu_init();
    if (err == ESP_OK) err = bsp_imu_start(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BMI270 unavailable: %s", esp_err_to_name(err));
        kodou_game_set_imu_state(game, KODOU_IMU_ERROR);
        vTaskDelete(NULL);
        return;
    }

    float gravity[3] = {0};
    for (unsigned sample = 0; sample < CALIBRATION_SAMPLES; ++sample) {
        float accel[3] = {0};
        float gyro[3] = {0};
        if (read_motion(accel, gyro) == ESP_OK) {
            for (size_t axis = 0; axis < 3; ++axis) gravity[axis] += accel[axis];
        }
        vTaskDelay(pdMS_TO_TICKS(MOTION_SAMPLE_MS));
    }
    for (size_t axis = 0; axis < 3; ++axis) {
        gravity[axis] /= (float)CALIBRATION_SAMPLES;
    }
    kodou_game_set_imu_state(game, KODOU_IMU_READY);
    ESP_LOGI(TAG, "BMI270 gesture input ready: down-flick=center twist=rim");

    float previous_linear[3] = {0};
    TickType_t cooldown_until = 0;
    TickType_t last_wake = xTaskGetTickCount();
    bool armed = true;
    int displayed_level = 0;
    while (true) {
        float accel[3] = {0};
        float gyro[3] = {0};
        if (read_motion(accel, gyro) != ESP_OK) {
            xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MOTION_SAMPLE_MS));
            continue;
        }
        float jerk_sq = 0.0f;
        float energy_sq = 0.0f;
        for (size_t axis = 0; axis < 3; ++axis) {
            gravity[axis] = gravity[axis] * (1.0f - GRAVITY_ALPHA) +
                            accel[axis] * GRAVITY_ALPHA;
            const float linear = accel[axis] - gravity[axis];
            const float delta = linear - previous_linear[axis];
            previous_linear[axis] = linear;
            energy_sq += linear * linear;
            jerk_sq += delta * delta;
        }
        const float energy = sqrtf(energy_sq);
        const float jerk = sqrtf(jerk_sq);
        const float yaw = fabsf(gyro[2]);
        const float gyro_other = fmaxf(fabsf(gyro[0]), fabsf(gyro[1]));
        int level = (int)fmaxf(jerk * 30.0f, yaw * 0.12f);
        if (level > 100) level = 100;
        if (level > displayed_level) displayed_level = level;
        else if (displayed_level > 0) displayed_level -= 6;
        kodou_game_set_motion_level(game, displayed_level);

        const TickType_t now = xTaskGetTickCount();
        if (!armed) {
            if (jerk < 0.34f && yaw < 145.0f && energy < 0.35f) armed = true;
        } else if (!tick_before(now, cooldown_until) &&
                   !tick_before(now, s_motion_mute_until)) {
            static const float center_threshold[] = {1.55f, 1.15f, 0.82f};
            static const float rim_threshold[] = {520.0f, 410.0f, 315.0f};
            const int sensitivity = kodou_game_get_sensitivity(game);
            kodou_hit_t hit = 0;
            uint8_t strength = 0;
            if (yaw >= rim_threshold[sensitivity] &&
                yaw > gyro_other * 1.12f &&
                (jerk < 3.4f || yaw > 760.0f)) {
                hit = KODOU_HIT_RIM;
                int value = 48 + (int)((yaw - rim_threshold[sensitivity]) *
                                       0.065f);
                strength = (uint8_t)(value > 100 ? 100 : value);
            } else if (jerk >= center_threshold[sensitivity] &&
                       energy >= 0.45f) {
                hit = KODOU_HIT_CENTER;
                int value = 48 + (int)((jerk - center_threshold[sensitivity]) *
                                       22.0f);
                strength = (uint8_t)(value > 100 ? 100 : value);
            }
            if (hit != 0) {
                kodou_game_submit_hit(game, hit, strength);
                armed = false;
                cooldown_until = now + pdMS_TO_TICKS(HIT_COOLDOWN_MS);
            }
        }
        xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MOTION_SAMPLE_MS));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(bsp_power_init());
    iris_ota_support_start();

    esp_gsp_config_t app_config;
    const esp_err_t bundle_err = ui_bundle_open(&app_config);
    if (bundle_err != ESP_OK) {
        ESP_LOGE(TAG, "GSP UI unavailable (0x%x); Recovery RPC remains active",
                 bundle_err);
        return;
    }
    /* A 25 ms render cadence is a better fit for the panel bandwidth than
     * asking the runtime for 60 Hz and letting frames queue up. Game timing
     * and hit windows still use their own monotonic clock. */
    ESP_ERROR_CHECK(esp_gsp_config_set(
        &app_config, ESP_GSP_FIELD_ACTIVE_TICK_MS, 25U) ==
        ESP_GSP_CONFIG_SET_OK ? ESP_OK : ESP_ERR_INVALID_ARG);
    /* Polling touch otherwise waits out a 33 ms idle scan plus two empty
     * samples before a click. Buttons now fire on press; keep the first
     * sample and the release confirmation close to that press. */
    ESP_ERROR_CHECK(esp_gsp_config_set(
        &app_config, ESP_GSP_FIELD_POINTER_POLL_MS, 8U) ==
        ESP_GSP_CONFIG_SET_OK ? ESP_OK : ESP_ERR_INVALID_ARG);
    ESP_ERROR_CHECK(esp_gsp_config_set(
        &app_config, ESP_GSP_FIELD_TOUCH_RELEASE_CONFIRM_POLLS, 1U) ==
        ESP_GSP_CONFIG_SET_OK ? ESP_OK : ESP_ERR_INVALID_ARG);
    esp_display_present_target_config_t display;
    ESP_ERROR_CHECK(board_display_init(&display));
    esp_lcd_touch_handle_t touch = NULL;
    ESP_ERROR_CHECK(board_touch_init(&touch));
    esp_gsp_esp_lcd_config_t lcd = ESP_GSP_ESP_LCD_CONFIG_INIT();
    lcd.display = display;
    lcd.touch = touch;
    /* This panel's interrupt line can stay quiet, so poll the controller. */
    lcd.touch_input_mode = ESP_GSP_TOUCH_INPUT_POLLING;
    /* Keep the moving chart ahead of sensor work. Audio retains a larger
     * buffer, so display bursts no longer have to trade frame cadence for
     * speaker continuity. */
    lcd.task_priority = 9U;
    lcd.perf_log = true;
    ESP_ERROR_CHECK(iris_screen_mirror_init());
    esp_gsp_handle_t ui;
    ESP_ERROR_CHECK(esp_gsp_esp_lcd_start(&app_config, &lcd, &ui));
    ESP_ERROR_CHECK(iris_screen_mirror_attach(ui));

    s_haptic_queue = xQueueCreate(1, sizeof(uint8_t));
    ESP_ERROR_CHECK(s_haptic_queue ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(bsp_motor_init());
    ESP_ERROR_CHECK(kodou_audio_start());
    const kodou_game_hooks_t hooks = {
        .feedback = game_feedback,
        .sensitivity_changed = sensitivity_changed,
        .volume_changed = volume_changed,
        .volume_pct_changed = volume_pct_changed,
        .guitar_strum = guitar_strum,
        .song_started = song_started,
        .song_stopped = song_stopped,
        .clock_ms = game_clock_ms,
    };
    ESP_ERROR_CHECK(kodou_game_init(ui, &s_game, &hooks));
    ESP_ERROR_CHECK(drum_tof_start(&s_game));
    ESP_ERROR_CHECK(xTaskCreate(haptic_task, "kodou_haptic", 2048, NULL, 5,
                                NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
#if KODOU_ENABLE_IMU
    ESP_ERROR_CHECK(xTaskCreate(motion_task, "kodou_motion", 4096, &s_game, 7,
                                NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
#else
    (void)motion_task;
#endif
    ESP_LOGI(TAG, "Kodou Matsuri ready: three songs + dual ToF + touch + audio (IMU disabled)");
}
