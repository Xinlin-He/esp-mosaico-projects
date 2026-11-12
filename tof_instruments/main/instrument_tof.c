// SPDX-License-Identifier: Apache-2.0

#include "instrument_tof.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp/subboard.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mosaico_module_mgr.h"
#include "mosaico_module_tof.h"

#define TOF_TASK_STACK          6144U
#define TOF_TASK_PRIORITY       7U
#define TOF_CLAIM_TIMEOUT_MS    400U
#define TOF_RETRY_DELAY_MS      400U
#define TOF_POLL_DELAY_MS       2U
#define STRUM_QUEUE_LENGTH      6U
#define TOF_CLOSE_ATTEMPTS      3U

/* Both models answer at 0x29 after reset, so each slot moves to its own
 * address before the next one leaves shutdown. Same scheme as the BSP
 * tof_distance example. */
#define TOF_ADDRESS_LEFT        0x30U
#define TOF_ADDRESS_RIGHT       0x31U

/* 20 ms keeps the strum window responsive. VL53L4CD takes whole milliseconds
 * in 10..200 ms; VL53L0X accepts the PAL's supported range. */
#define TOF_TIMING_BUDGET_US    20000U

static const char *TAG = "instrument_tof";
static TaskHandle_t s_task;
static QueueHandle_t s_strums;
static SemaphoreHandle_t s_status_lock;
static int s_last_valid_mm = -1;
static bool s_any_sensor_online = false;

typedef struct {
    bool was_in;
    uint16_t prev_mm;
    int64_t last_strum_us;
} guitar_slot_tracker_t;

static guitar_slot_tracker_t s_guitar_tracker[BSP_SUBBOARD_SLOT_COUNT];

static const char *slot_name(bsp_subboard_slot_t slot)
{
    return slot == BSP_SUBBOARD_SLOT_LEFT ? "left" : "right";
}

static void update_status_reading(int mm, bool online)
{
    if (!s_status_lock || xSemaphoreTake(s_status_lock, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }
    s_any_sensor_online = online;
    s_last_valid_mm = mm;
    xSemaphoreGive(s_status_lock);
}

void instrument_tof_copy_status(char *out, size_t out_len, bool english)
{
    if (!out || out_len == 0) {
        return;
    }
    bool online = false;
    int mm = -1;
    if (s_status_lock && xSemaphoreTake(s_status_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
        online = s_any_sensor_online;
        mm = s_last_valid_mm;
        xSemaphoreGive(s_status_lock);
    }
    if (!online) {
        snprintf(out, out_len, "%s", english ? "ToF: No sensor" : "ToF · 未连接模块");
    } else if (mm < 0) {
        snprintf(out, out_len, "%s", english ? "ToF: Ready" : "ToF · 待扫弦");
    } else {
        snprintf(out, out_len, english ? "ToF %d mm · Strum" : "ToF %d mm · 可扫弦", mm);
    }
}

bool instrument_tof_take_strum(int *direction)
{
    int value = 1;
    if (!s_strums || xQueueReceive(s_strums, &value, 0) != pdTRUE) {
        return false;
    }
    if (direction) {
        *direction = value;
    }
    return true;
}

static bool slot_has_tof(mosaico_module_mgr_slot_t slot)
{
    mosaico_module_mgr_info_t info = {0};
    return mosaico_module_mgr_get_info(slot, &info) == ESP_OK &&
           info.presence == MOSAICO_MODULE_PRESENCE_PRESENT &&
           info.descriptor_state == MOSAICO_MODULE_DESCRIPTOR_VALID &&
           info.eeprom.board_type == MOSAICO_BOARD_TYPE_TOF;
}

static esp_err_t claim_slot(mosaico_module_mgr_slot_t slot,
                            mosaico_module_lease_t *lease)
{
    const mosaico_module_mgr_claim_config_t config = {
        .expected_type = MOSAICO_BOARD_TYPE_TOF,
        .slot = slot,
        .timeout_ms = TOF_CLAIM_TIMEOUT_MS,
    };
    return mosaico_module_mgr_claim(&config, lease);
}

static void release_lease(mosaico_module_lease_t *lease)
{
    if (lease->id == 0) {
        return;
    }
    const esp_err_t ret = mosaico_module_mgr_release(lease);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Release %s ToF lease failed: %s",
                 mosaico_module_mgr_slot_to_name(lease->slot),
                 esp_err_to_name(ret));
        return;
    }
    *lease = (mosaico_module_lease_t){0};
}

/* Hold one slot in reset so its module cannot answer at the default address. */
static esp_err_t hold_slot_shutdown(bsp_subboard_slot_t slot)
{
    const gpio_num_t pin = bsp_subboard_map_gpio(slot, GPIO_NUM_48);
    ESP_RETURN_ON_ERROR(gpio_hold_dis(pin), TAG, "release GPIO hold failed");
    ESP_RETURN_ON_ERROR(gpio_sleep_sel_dis(pin), TAG, "disable sleep selection failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, 0), TAG, "assert XSHUT failed");
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

static esp_err_t close_sensor(bsp_subboard_slot_t slot, mosaico_tof_handle_t *sensor)
{
    if (!*sensor) {
        return ESP_OK;
    }
    for (unsigned attempt = 0; attempt < TOF_CLOSE_ATTEMPTS; ++attempt) {
        if (mosaico_tof_close(*sensor) == ESP_OK) {
            *sensor = NULL;
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGE(TAG, "Close %s ToF sensor failed; driver keeps the handle", slot_name(slot));
    return ESP_FAIL;
}

/* Open one slot, move it to its own address, then start continuous ranging.
 * The driver resets the sensor on open, so the peer's XSHUT must already be low. */
static esp_err_t open_slot(bsp_subboard_slot_t slot, mosaico_tof_handle_t *out)
{
    *out = NULL;
    const uint8_t address = slot == BSP_SUBBOARD_SLOT_LEFT ? TOF_ADDRESS_LEFT : TOF_ADDRESS_RIGHT;
    esp_err_t ret = mosaico_tof_open(slot, out);
    if (ret == ESP_OK) {
        ret = mosaico_tof_set_address(*out, address);
    }
    if (ret == ESP_OK) {
        ret = mosaico_tof_set_timing_budget(*out, TOF_TIMING_BUDGET_US);
    }
    if (ret == ESP_OK) {
        ret = mosaico_tof_start_continuous(*out);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Open %s ToF sensor failed: %s", slot_name(slot), esp_err_to_name(ret));
        close_sensor(slot, out);
        return ret;
    }
    ESP_LOGI(TAG, "%s ready: slot=%s I2C=0x%02X", mosaico_tof_model_name(*out),
             slot_name(slot), address);
    return ESP_OK;
}

static void process_guitar_sample(kodou_game_t *game,
                                  bsp_subboard_slot_t slot,
                                  const mosaico_tof_data_t *data,
                                  uint16_t strike_mm,
                                  uint16_t release_mm,
                                  uint16_t interval_ms)
{
    guitar_slot_tracker_t *tr = &s_guitar_tracker[slot];
    if (!data->valid) {
        tr->was_in = false;
        update_status_reading(-1, true);
        return;
    }
    update_status_reading((int)data->distance_mm, true);

    /* Use the unified thresholds: near = min(40, strike_mm/2), far = strike_mm (or release_mm if larger) */
    const int far_edge = strike_mm > 0 ? (int)strike_mm : (int)release_mm;
    const int near_edge = 10;
    const bool in = (int)data->distance_mm >= near_edge &&
                    (int)data->distance_mm <= far_edge;
    const int64_t now_us = esp_timer_get_time();
    if (in && !tr->was_in &&
        (now_us - tr->last_strum_us) >= (int64_t)interval_ms * 1000LL) {
        const int direction = (tr->prev_mm > data->distance_mm) ? 1 : -1;
        if (s_strums && xQueueSend(s_strums, &direction, 0) == pdTRUE) {
            tr->last_strum_us = now_us;
        }
        if (game && game->view == 4) { /* KODOU_VIEW_GUITAR */
            kodou_game_submit_guitar_strum(game, direction);
        }
    }
    tr->was_in = in;
    tr->prev_mm = data->distance_mm;
}

static void process_sample(kodou_game_t *game, bsp_subboard_slot_t slot,
                           const mosaico_tof_data_t *data)
{
    const kodou_hit_t side = slot == BSP_SUBBOARD_SLOT_LEFT
                                 ? KODOU_HIT_CENTER
                                 : KODOU_HIT_RIM;
    kodou_game_set_tof_distance(game, side, data->distance_mm, data->valid);

    uint16_t strike_mm = 180;
    uint16_t release_mm = 260;
    kodou_game_get_tof_thresholds(game, &strike_mm, &release_mm);
    const uint16_t interval_ms = kodou_game_get_tof_interval_ms(game);
    process_guitar_sample(game, slot, data, strike_mm, release_mm, interval_ms);
}

static esp_err_t measure_active(kodou_game_t *game, bool has_left, bool has_right)
{
    const bool present[BSP_SUBBOARD_SLOT_COUNT] = {has_left, has_right};
    mosaico_tof_handle_t sensors[BSP_SUBBOARD_SLOT_COUNT] = {0};
    esp_err_t ret = ESP_OK;

    /* Reset both slots before any module is opened. An unclaimed module must
     * not answer at 0x29 while its neighbour is being readdressed. */
    for (size_t i = 0; i < BSP_SUBBOARD_SLOT_COUNT && ret == ESP_OK; ++i) {
        ret = hold_slot_shutdown((bsp_subboard_slot_t)i);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Hold %s slot in reset failed: %s",
                     slot_name((bsp_subboard_slot_t)i), esp_err_to_name(ret));
        }
    }
    for (size_t i = 0; i < BSP_SUBBOARD_SLOT_COUNT && ret == ESP_OK; ++i) {
        if (present[i]) {
            ret = open_slot((bsp_subboard_slot_t)i, &sensors[i]);
        }
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ToF ready: left=%d right=%d", has_left ? 1 : 0, has_right ? 1 : 0);
        update_status_reading(-1, true);
        while ((has_left == slot_has_tof(MOSAICO_MODULE_MGR_SLOT_LEFT)) &&
               (has_right == slot_has_tof(MOSAICO_MODULE_MGR_SLOT_RIGHT))) {
            for (size_t i = 0; i < BSP_SUBBOARD_SLOT_COUNT; ++i) {
                if (!sensors[i]) {
                    continue;
                }
                mosaico_tof_data_t data = {0};
                bool ready = false;
                ret = mosaico_tof_read_ready(sensors[i], &data, &ready);
                if (ret != ESP_OK) {
                    break;
                }
                if (ready) {
                    process_sample(game, (bsp_subboard_slot_t)i, &data);
                }
            }
            if (ret != ESP_OK) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(TOF_POLL_DELAY_MS) + 1);
        }
        if (ret == ESP_OK) {
            ret = ESP_ERR_NOT_FOUND;
        }
    }
    update_status_reading(-1, false);
    kodou_game_set_tof_distance(game, KODOU_HIT_CENTER, 0, false);
    kodou_game_set_tof_distance(game, KODOU_HIT_RIM, 0, false);
    for (size_t i = BSP_SUBBOARD_SLOT_COUNT; i-- > 0;) {
        if (sensors[i]) {
            close_sensor((bsp_subboard_slot_t)i, &sensors[i]);
        }
    }
    return ret;
}

static void tof_task(void *arg)
{
    kodou_game_t *game = arg;
    while (mosaico_module_mgr_init(NULL) != ESP_OK) {
        ESP_LOGW(TAG, "ToF module manager unavailable; retrying");
        vTaskDelay(pdMS_TO_TICKS(TOF_RETRY_DELAY_MS));
    }

    while (true) {
        mosaico_module_lease_t left = {0};
        mosaico_module_lease_t right = {0};
        const bool has_left = claim_slot(MOSAICO_MODULE_MGR_SLOT_LEFT, &left) == ESP_OK;
        const bool has_right = claim_slot(MOSAICO_MODULE_MGR_SLOT_RIGHT, &right) == ESP_OK;
        if (!has_left && !has_right) {
            update_status_reading(-1, false);
            vTaskDelay(pdMS_TO_TICKS(TOF_RETRY_DELAY_MS));
            continue;
        }

        const esp_err_t ret = measure_active(game, has_left, has_right);
        if (ret != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "ToF loop interrupted: %s", esp_err_to_name(ret));
        }
        release_lease(&right);
        release_lease(&left);
        vTaskDelay(pdMS_TO_TICKS(TOF_RETRY_DELAY_MS));
    }
}

esp_err_t instrument_tof_start(kodou_game_t *game)
{
    if (!game) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task) {
        return ESP_OK;
    }
    s_status_lock = xSemaphoreCreateMutex();
    s_strums = xQueueCreate(STRUM_QUEUE_LENGTH, sizeof(int));
    if (!s_status_lock || !s_strums) {
        return ESP_ERR_NO_MEM;
    }
    return xTaskCreate(tof_task, "inst_tof", TOF_TASK_STACK, game,
                       TOF_TASK_PRIORITY, &s_task) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}
