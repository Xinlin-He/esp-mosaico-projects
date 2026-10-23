// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdio.h>
#include "raylib_lite_game_module_contract.h"
#include "raylib_lite_raylib.h"
#if defined(RAYLIB_LITE_GAME_NATIVE)
#include "driver/ledc.h"
#include "esp_log.h"
#include "native_feedback.h"
#include "raylib_lite_clock.h"
#include "raylib_lite_raylib_audio.h"
#include "raylib_lite_save.h"
#endif
#include "bball_game.h"
#include "bball_view.h"

// Host action codes injected by the simulator runner (host/run_game.py):
//   0=left  1=right  2=up(jump)  3=pause  4=restart  5=down(back)  6=fire.
#define ACTION_LEFT    0
#define ACTION_RIGHT   1
#define ACTION_UP      2
#define ACTION_PAUSE   3
#define ACTION_RESTART 4
#define ACTION_DOWN    5
#define ACTION_FIRE    6

#define AIM_STEP 0.055f

typedef struct {
    bball_game_t game;
#if defined(RAYLIB_LITE_GAME_NATIVE)
    Sound sounds[5];
    raylib_lite_save_t save;
    bool save_ready;
    uint32_t saved_best;
#endif
} bball_module_state_t;

#if defined(RAYLIB_LITE_GAME_NATIVE)
enum { SFX_START, SFX_SHOT, SFX_MAKE, SFX_MISS, SFX_GAME_OVER };
static const char *const SFX_PATHS[] = {
    "start.sound", "shot.sound", "make.sound", "miss.sound", "game_over.sound"
};

static void play_cue(bball_module_state_t *state, unsigned cue)
{
    if (cue < 5 && state->sounds[cue].frameCount) {
        PlaySound(state->sounds[cue]);
    }
}

static void save_best(bball_module_state_t *state)
{
    if (!state->save_ready || state->game.best <= state->saved_best) {
        return;
    }
    uint64_t now_ms = raylib_lite_time_us() / 1000U;
    if (raylib_lite_save_request(&state->save, &state->game.best, now_ms) == RAYLIB_LITE_OK &&
        raylib_lite_save_flush(&state->save, now_ms, true) == RAYLIB_LITE_OK) {
        state->saved_best = state->game.best;
    }
}
#endif

static int initialize(void *value, const char *asset_root)
{
    bball_module_state_t *state = value;
    bball_game_reset(&state->game, 0x484f4f50U); // 'HOOP'
    bball_view_init_assets(asset_root);
    InitWindow(BBALL_SCREEN_W, BBALL_SCREEN_H, "Arcade Hoops");
    SetTargetFPS(30);
#if defined(RAYLIB_LITE_GAME_NATIVE)
    raylib_lite_save_config_t save_config = {
        .storage = raylib_lite_save_nvs_storage(),
        .storage_namespace = "hoops",
        .key = "record",
        .version = 1,
        .payload_size = sizeof(state->saved_best),
        .debounce_ms = 0,
    };
    state->save_ready =
        raylib_lite_save_init(&state->save, &save_config) == RAYLIB_LITE_OK;
    if (state->save_ready &&
        raylib_lite_save_load(&state->save, &state->saved_best, NULL) == RAYLIB_LITE_OK) {
        state->game.best = state->saved_best;
    }
    InitAudioDevice();
    if (IsAudioDeviceReady()) {
        for (unsigned i = 0; i < 5; ++i) {
            state->sounds[i] = LoadSound(SFX_PATHS[i]);
        }
    }
    esp_err_t fade = ledc_fade_func_install(0);
    if (fade != ESP_OK && fade != ESP_ERR_INVALID_STATE) {
        ESP_LOGW("hoops", "LEDC fade service unavailable: %s", esp_err_to_name(fade));
    }
    raylib_lite_native_feedback_init();
#endif
    return 0;
}

static void shutdown(void *value)
{
#if defined(RAYLIB_LITE_GAME_NATIVE)
    bball_module_state_t *state = value;
    save_best(state);
    raylib_lite_native_feedback_stop();
    for (unsigned i = 0; i < 5; ++i) {
        if (state->sounds[i].frameCount) {
            UnloadSound(state->sounds[i]);
        }
    }
    CloseAudioDevice();
#else
    (void)value;
#endif
}

static void input(void *value, const raylib_lite_game_input_v1_t *event)
{
    bball_module_state_t *state = value;
    if (!state || !event) {
        return;
    }
    if (event->type == RAYLIB_LITE_GAME_INPUT_POINTER) {
#if defined(RAYLIB_LITE_GAME_NATIVE)
        bball_phase_t before = state->game.phase;
#endif
        bball_game_point(&state->game, (float)event->x, (float)event->y,
                         event->pressed);
#if defined(RAYLIB_LITE_GAME_NATIVE)
        if ((before == ARC_TITLE || before == ARC_OVER) &&
            state->game.phase == ARC_AIM) {
            play_cue(state, SFX_START);
        }
#endif
    } else if (event->type == RAYLIB_LITE_GAME_INPUT_ACTION) {
        if (!event->pressed) {
            return;
        }
        switch (event->code) {
        case ACTION_LEFT:    bball_game_aim(&state->game, -AIM_STEP); break;
        case ACTION_RIGHT:   bball_game_aim(&state->game, AIM_STEP);  break;
        case ACTION_UP:
        case ACTION_FIRE:    bball_game_fire(&state->game);           break;
        case ACTION_PAUSE:   bball_game_toggle_pause(&state->game);   break;
        case ACTION_RESTART: bball_game_start(&state->game);          break;
        case ACTION_DOWN:
        default: break;
        }
    } else if (event->type == RAYLIB_LITE_GAME_INPUT_CONTROL) {
        if (event->code == RAYLIB_LITE_GAME_CONTROL_PAUSE &&
            state->game.phase != ARC_PAUSED) {
            bball_game_toggle_pause(&state->game);
        } else if (event->code == RAYLIB_LITE_GAME_CONTROL_RESUME &&
                   state->game.phase == ARC_PAUSED) {
            bball_game_toggle_pause(&state->game);
        } else if (event->code == RAYLIB_LITE_GAME_CONTROL_RESET) {
            bball_game_reset(&state->game, state->game.rng);
        }
    }
}

static void update(void *value)
{
    bball_module_state_t *state = value;
    if (!state) {
        return;
    }
#if defined(RAYLIB_LITE_GAME_NATIVE)
    bball_game_clock_us(&state->game, (int64_t)raylib_lite_time_us());
    uint16_t attempts = state->game.attempts;
    uint16_t resolve_seq = state->game.resolve_seq;
    bball_phase_t phase = state->game.phase;
    bool fever = state->game.fever;
#endif
    bball_game_update(&state->game);
#if defined(RAYLIB_LITE_GAME_NATIVE)
    if (state->game.attempts != attempts) {
        play_cue(state, SFX_SHOT);
        raylib_lite_native_feedback_pulse(70, 40);
    }
    if (state->game.resolve_seq != resolve_seq) {
        if (state->game.outcome == BBALL_SHOT_MADE) {
            play_cue(state, SFX_MAKE);
            if (state->game.streak >= BBALL_HOT_STREAK) {
                raylib_lite_native_feedback_pattern(100, 40, 100, 30, 120);
            } else if (state->game.streak >= 2) {
                raylib_lite_native_feedback_pattern(100, 45, 100, 35, 80);
            } else {
                raylib_lite_native_feedback_pulse(100, 90);
            }
        } else {
            play_cue(state, SFX_MISS);
            raylib_lite_native_feedback_pattern(55, 45, 55, 50, 45);
        }
    }
    if (!fever && state->game.fever) {
        play_cue(state, SFX_START);
        raylib_lite_native_feedback_pattern(100, 70, 100, 50, 120);
    }
    if (phase != ARC_OVER && state->game.phase == ARC_OVER) {
        play_cue(state, SFX_GAME_OVER);
        save_best(state);
    }
#endif
}

static int render(void *value)
{
    bball_module_state_t *state = value;
    bball_view_render(&state->game);
    return 0;
}

static uint32_t state_hash(const void *value)
{
    return bball_game_state_hash(&((const bball_module_state_t *)value)->game);
}

static int state_json(const void *value, char *output, size_t capacity)
{
    const bball_module_state_t *state = value;
    static const char *phases[] = {"title", "aim", "flight", "over", "paused"};
    static const char *shots[] = {"none", "made", "miss"};
    const bball_game_t *g = &state->game;
    const char *phase = (unsigned)g->phase < 5 ? phases[g->phase] : "unknown";
    const char *shot = (unsigned)g->outcome < 3 ? shots[g->outcome] : "none";
    unsigned live = 0;
    for (int i = 0; i < BBALL_BALL_CAP; ++i) {
        if (g->balls[i].alive) {
            live++;
        }
    }
    return snprintf(output, capacity,
        "{\"phase\":\"%s\",\"time\":%u,\"score\":%lu,\"best\":%lu,"
        "\"streak\":%u,\"longest\":%u,\"fever\":%s,\"makes\":%u,\"attempts\":%u,"
        "\"live\":%u,\"distance\":%u,\"points\":%u,\"aim\":%.3f,\"power\":%.3f,"
        "\"outcome\":\"%s\",\"tick\":%lu,\"state_hash\":\"%08lx\"}",
        phase, bball_game_seconds_left(g),
        (unsigned long)g->score, (unsigned long)g->best,
        g->streak, g->longest_streak, g->fever ? "true" : "false",
        g->makes, g->attempts, live, g->distance, g->points,
        (double)g->aim, (double)g->power, shot,
        (unsigned long)g->tick, (unsigned long)bball_game_state_hash(g));
}

static const raylib_lite_game_module_v1_t s_module = {
    .descriptor = {RAYLIB_LITE_GAME_MODULE_ABI, "arcade_hoops", "Arcade Hoops",
                   BBALL_SCREEN_W, BBALL_SCREEN_H, 30, 1},
    .state_size = sizeof(bball_module_state_t),
    .initialize = initialize,
    .shutdown = shutdown,
    .input = input,
    .update = update,
    .render = render,
    .state_hash = state_hash,
    .state_json = state_json,
};

const raylib_lite_game_module_v1_t *raylib_lite_game_module_v1(void)
{
    return &s_module;
}
