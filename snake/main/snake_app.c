// SPDX-License-Identifier: Apache-2.0
#define GSP_BUNDLE_ENABLE_RAW_IDS 1
#include "bundle_gsp.h"
#include "esp_log.h"
#include "game_audio.h"
#include "mosaico_game.h"
#include "raylib_screen_mirror.h"
#include "snake_app.h"
#include "snake_game.h"
#include "snake_view.h"

static const char *TAG = "snake";
static snake_game_t s_game;
static bool s_input;

static esp_err_t on_start(void)
{
    snake_game_reset(&s_game, 0x534e414bU); // 'SNAK'
    return ESP_OK;
}

static esp_err_t after_healthy(void)
{
    return game_audio_init();
}

static void on_event(const mosaico_device_event_t *event)
{
    if (event->type != MOSAICO_DEVICE_EVENT_POINTER) {
        return;
    }
    s_input = true;
    snake_phase_t before = s_game.phase;
    snake_game_point(&s_game, (float)event->x, (float)event->y, event->pressed);
    if (before != SNAKE_PLAYING && s_game.phase == SNAKE_PLAYING) {
        (void)game_audio_play(GAME_AUDIO_START);
    }
}

static bool idle(void)
{
    // The board only advances while playing; pause presentation otherwise until
    // the next touch so the panel stays responsive without spinning the CPU.
    bool skip = s_game.phase != SNAKE_PLAYING && !s_input;
    s_input = false;
    return skip;
}

static void on_update(void)
{
    uint32_t old_score = s_game.score;
    snake_phase_t old_phase = s_game.phase;
    snake_game_update(&s_game);
    if (s_game.score > old_score) {
        (void)game_audio_play(GAME_AUDIO_DESTROY);
    }
    if (old_phase == SNAKE_PLAYING && s_game.phase == SNAKE_GAME_OVER) {
        (void)game_audio_play(GAME_AUDIO_GAME_OVER);
    }
}

static void on_render(void)
{
    snake_view_render(&s_game);
}

static void on_stats(void)
{
    ESP_LOGI(TAG, "score=%lu length=%u state_hash=%08lx",
             (unsigned long)s_game.score, s_game.length,
             (unsigned long)snake_game_state_hash(&s_game));
}

static const mosaico_game_app_config_t s_config = {
    .tag = "snake",
    .window_title = "Mosaico Snake",
    .canvas_bind = GSP_SNAKE_BIND_GAME_CANVAS,
    .touch_points = 1,
    .enable_imu = false,
    .target_fps = 30,
    .gsp_bundle = gsp_bundle_config,
    .register_mirror = raylib_screen_mirror_register,
    .on_start = on_start,
    .after_healthy = after_healthy,
    .on_event = on_event,
    .idle = idle,
    .on_update = on_update,
    .on_render = on_render,
    .on_stats = on_stats,
};

const mosaico_game_app_config_t *snake_app_config(void)
{
    return &s_config;
}
