// SPDX-License-Identifier: Apache-2.0
#define GSP_BUNDLE_ENABLE_RAW_IDS 1
#include "bundle_gsp.h"
#include "bsp/motor.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "game_audio.h"
#include "mosaico_game.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "raylib_screen_mirror.h"
#include "bball_app.h"
#include "bball_game.h"
#include "bball_view.h"

static const char *TAG = "hoops";
static const char *NVS_NS = "hoops";
static const char *NVS_BEST = "best";
static bball_game_t s_game;
static bool s_input;
static bool s_motor_ok;
static uint32_t s_saved_best;

// One step of a haptic pattern. strength 0 is off; ms 0 ends the pattern.
typedef struct {
    uint8_t strength;
    uint16_t ms;
} buzz_step_t;

// Release: one tick. Miss: two short taps. Make: one firm pulse.
// Combo: extra taps, and a hot streak holds the last pulse longer.
static const buzz_step_t BUZZ_SHOT[] = {
    {70, 40}, {0, 0},
};
static const buzz_step_t BUZZ_MISS[] = {
    {55, 45}, {0, 50}, {55, 45}, {0, 0},
};
static const buzz_step_t BUZZ_MAKE[] = {
    {100, 90}, {0, 0},
};
static const buzz_step_t BUZZ_COMBO[] = {
    {100, 45}, {0, 35}, {100, 80}, {0, 0},
};
static const buzz_step_t BUZZ_HOT[] = {
    {100, 40}, {0, 30}, {100, 40}, {0, 30}, {100, 120}, {0, 0},
};
static const buzz_step_t BUZZ_FEVER[] = {
    {100, 70}, {0, 50}, {100, 70}, {0, 50}, {100, 120}, {0, 0},
};

static const buzz_step_t *s_buzz;
static uint8_t s_buzz_i;
static uint8_t s_buzz_n;
static int64_t s_buzz_until;

static void buzz_play(const buzz_step_t *steps, uint8_t count)
{
    if (!s_motor_ok) {
        return;
    }
    s_buzz = steps;
    s_buzz_n = count;
    s_buzz_i = 0;
    s_buzz_until = 0;
}

static void buzz_service(void)
{
    if (!s_motor_ok || s_buzz == NULL) {
        return;
    }
    int64_t now = esp_timer_get_time();
    if (s_buzz_until != 0 && now < s_buzz_until) {
        return;
    }
    if (s_buzz_i >= s_buzz_n) {
        (void)bsp_motor_set(false);
        s_buzz = NULL;
        return;
    }
    buzz_step_t step = s_buzz[s_buzz_i++];
    if (step.strength == 0) {
        (void)bsp_motor_set(false);
    } else {
        (void)bsp_motor_set_strength(step.strength);
    }
    if (step.ms == 0) {
        s_buzz = NULL;
        s_buzz_until = 0;
        return;
    }
    s_buzz_until = now + (int64_t)step.ms * 1000;
}

// Previous-frame trackers used to turn state transitions into audio cues.
static bball_phase_t s_prev_phase = ARC_TITLE;
static bool s_prev_fever;

// Persist the best score to flash. This is intentionally NOT called while the
// ball is in play: nvs_commit writes flash, and a flash write disables the CPU
// cache for tens to hundreds of milliseconds, stalling XIP code and the QSPI
// DMA that streams the frame to the panel. That stall is exactly the 50-190 ms
// transfer spike the TE scheduler reports as "overrun", i.e. the tearing the
// player sees while a combo is racking up points. Saving once at game over
// keeps every in-match frame flash-free.
static void persist_best(void)
{
    if (s_game.best <= s_saved_best) {
        return;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u32(handle, NVS_BEST, s_game.best);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err == ESP_OK) {
        s_saved_best = s_game.best;
    } else {
        ESP_LOGW(TAG, "best score not saved: %s", esp_err_to_name(err));
    }
}

// Runs once before the display comes up: mount the game_assets partition and
// load the texture atlases so the device renders the same textured look as the
// Host simulator. bball_view_init_assets() falls back to vector drawing when an
// atlas is missing, so we always return ESP_OK to keep the game playable.
static esp_err_t before_display(void)
{
    bball_view_init_assets(NULL);
    return ESP_OK;
}

static esp_err_t on_start(void)
{
    s_game.use_imu = false; // shoot with an upward swipe, not a board flick
    bball_game_reset(&s_game, 0x484f4f50U); // 'HOOP'
    s_saved_best = 0;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        uint32_t best = 0;
        err = nvs_get_u32(handle, NVS_BEST, &best);
        nvs_close(handle);
        if (err == ESP_OK) {
            s_game.best = best;
            s_saved_best = best;
        }
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "best score unavailable: %s", esp_err_to_name(err));
    }
    s_prev_phase = s_game.phase;
    s_prev_fever = s_game.fever;
    return ESP_OK;
}

static esp_err_t after_healthy(void)
{
    esp_err_t motor = bsp_motor_init();
    s_motor_ok = motor == ESP_OK;
    if (s_motor_ok) {
        (void)bsp_motor_set(false);
        ESP_LOGI(TAG, "haptic ready");
    } else {
        ESP_LOGW(TAG, "haptic unavailable: %s", esp_err_to_name(motor));
    }
    return game_audio_init();
}

static void on_event(const mosaico_device_event_t *event)
{
    if (event->type == MOSAICO_DEVICE_EVENT_IMU) {
        bball_game_accel(&s_game, event->x / 1000.0f, event->y / 1000.0f,
                         event->value / 1000.0f);
        return;
    }
    if (event->type != MOSAICO_DEVICE_EVENT_POINTER) {
        return;
    }
    s_input = true;
    bball_game_point(&s_game, (float)event->x, (float)event->y, event->pressed);
}

static bool idle(void)
{
    // Hardware timer (ESP Timer / SYSTIMER), not the rendered frame count.
    // Sampled here so a paused frame that skips on_update still freezes the
    // countdown instead of letting the gap count as play time.
    bball_game_clock_us(&s_game, esp_timer_get_time());
    // Only the menu / paused screens are static; an active session always
    // animates (clock, ball flight) and must keep ticking.
    bool resting = s_game.phase == ARC_TITLE ||
                   s_game.phase == ARC_OVER ||
                   s_game.phase == ARC_PAUSED;
    bool skip = resting && !s_input;
    s_input = false;
    return skip;
}

static void on_update(void)
{
    uint16_t prev_attempts = s_game.attempts;
    uint16_t prev_resolve = s_game.resolve_seq;
    bball_phase_t prev_phase = s_prev_phase;
    bool prev_fever = s_prev_fever;

    bball_game_update(&s_game);

    if (!prev_fever && s_game.fever) {
        (void)game_audio_play(GAME_AUDIO_START); // fever alert
        buzz_play(BUZZ_FEVER, 6);
    }
    if (s_game.attempts != prev_attempts) {
        (void)game_audio_play(GAME_AUDIO_SHOT); // ball released
        buzz_play(BUZZ_SHOT, 2);
    }
    if (s_game.resolve_seq != prev_resolve) {
        if (s_game.outcome == BBALL_SHOT_MADE) {
            (void)game_audio_play(GAME_AUDIO_DESTROY); // basket
            if (s_game.streak >= BBALL_HOT_STREAK) {
                buzz_play(BUZZ_HOT, 6);
            } else if (s_game.streak >= 2) {
                buzz_play(BUZZ_COMBO, 4);
            } else {
                buzz_play(BUZZ_MAKE, 2);
            }
        } else {
            (void)game_audio_play(GAME_AUDIO_HIT); // clank / miss
            buzz_play(BUZZ_MISS, 4);
        }
    }
    buzz_service();
    if (prev_phase != ARC_OVER && s_game.phase == ARC_OVER) {
        (void)game_audio_play(GAME_AUDIO_GAME_OVER);
        // Match finished: now it is safe to touch flash (nothing is in flight
        // to the panel that a cache-disabling flash write could tear).
        persist_best();
    }
    if ((prev_phase == ARC_TITLE || prev_phase == ARC_OVER) &&
        s_game.phase == ARC_AIM) {
        (void)game_audio_play(GAME_AUDIO_START);
    }

    s_prev_phase = s_game.phase;
    s_prev_fever = s_game.fever;
}

static void on_render(void)
{
    bball_view_render(&s_game);
}

static void on_stats(void)
{
    ESP_LOGI(TAG, "score=%lu best=%lu time=%u streak=%u phase=%d state_hash=%08lx",
             (unsigned long)s_game.score, (unsigned long)s_game.best,
             bball_game_seconds_left(&s_game), s_game.streak,
             (int)s_game.phase, (unsigned long)bball_game_state_hash(&s_game));
}

static const mosaico_game_app_config_t s_config = {
    .tag = "hoops",
    .window_title = "Arcade Hoops",
    .canvas_bind = GSP_HOOPS_BIND_GAME_CANVAS,
    .touch_points = 1,
    .enable_imu = false,
    .imu_sample_ms = 20,
    .target_fps = 30,
    .gsp_bundle = gsp_bundle_config,
    .register_mirror = raylib_screen_mirror_register,
    .before_display = before_display,
    .on_start = on_start,
    .after_healthy = after_healthy,
    .on_event = on_event,
    .idle = idle,
    .on_update = on_update,
    .on_render = on_render,
    .on_stats = on_stats,
};

const mosaico_game_app_config_t *bball_app_config(void)
{
    return &s_config;
}
