// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdio.h>
#include "raylib_lite_game_module_contract.h"
#include "raylib_lite_raylib.h"
#if defined(RAYLIB_LITE_GAME_NATIVE)
#include "raylib_lite_raylib_audio.h"
#endif
#include "snake_game.h"
#include "snake_view.h"

// Host action codes injected by the simulator runner (host/run_game.py):
//   0=left  1=right  2=up(jump)  5=down(back)  3=pause  4=restart  6=fire.
#define ACTION_LEFT    0
#define ACTION_RIGHT   1
#define ACTION_UP      2
#define ACTION_PAUSE   3
#define ACTION_RESTART 4
#define ACTION_DOWN    5
#define ACTION_FIRE    6

typedef struct {
    snake_game_t game;
#if defined(RAYLIB_LITE_GAME_NATIVE)
    Sound sounds[3];
#endif
} snake_module_state_t;

#if defined(RAYLIB_LITE_GAME_NATIVE)
enum { SFX_START, SFX_EAT, SFX_GAME_OVER };
static const char *const SFX_PATHS[] = {
    "start.sound", "eat.sound", "game_over.sound"
};

static void play_cue(snake_module_state_t *state, unsigned cue)
{
    if (cue < 3 && state->sounds[cue].frameCount) {
        PlaySound(state->sounds[cue]);
    }
}
#endif

static int initialize(void *value, const char *asset_root)
{
    (void)asset_root;
    snake_module_state_t *state = value;
    snake_game_reset(&state->game, 0x534e414bU); // 'SNAK'
    InitWindow(480, 480, "Mosaico Snake");
    SetTargetFPS(30);
#if defined(RAYLIB_LITE_GAME_NATIVE)
    InitAudioDevice();
    if (IsAudioDeviceReady()) {
        for (unsigned i = 0; i < 3; ++i) {
            state->sounds[i] = LoadSound(SFX_PATHS[i]);
        }
    }
#endif
    return 0;
}

static void shutdown(void *value)
{
#if defined(RAYLIB_LITE_GAME_NATIVE)
    snake_module_state_t *state = value;
    for (unsigned i = 0; i < 3; ++i) {
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
    snake_module_state_t *state = value;
    if (!state || !event) {
        return;
    }
    if (event->type == RAYLIB_LITE_GAME_INPUT_POINTER) {
#if defined(RAYLIB_LITE_GAME_NATIVE)
        snake_phase_t before = state->game.phase;
#endif
        snake_game_point(&state->game, (float)event->x, (float)event->y,
                         event->pressed);
#if defined(RAYLIB_LITE_GAME_NATIVE)
        if (before != SNAKE_PLAYING && state->game.phase == SNAKE_PLAYING) {
            play_cue(state, SFX_START);
        }
#endif
    } else if (event->type == RAYLIB_LITE_GAME_INPUT_ACTION) {
        if (!event->pressed) {
            return;
        }
        switch (event->code) {
        case ACTION_LEFT:    snake_game_set_dir(&state->game, SNAKE_DIR_LEFT);  break;
        case ACTION_RIGHT:   snake_game_set_dir(&state->game, SNAKE_DIR_RIGHT); break;
        case ACTION_UP:      snake_game_set_dir(&state->game, SNAKE_DIR_UP);    break;
        case ACTION_DOWN:    snake_game_set_dir(&state->game, SNAKE_DIR_DOWN);  break;
        case ACTION_PAUSE:   snake_game_toggle_pause(&state->game);            break;
        case ACTION_RESTART:
        case ACTION_FIRE:    snake_game_start(&state->game);                   break;
        default: break;
        }
    } else if (event->type == RAYLIB_LITE_GAME_INPUT_CONTROL) {
        if (event->code == RAYLIB_LITE_GAME_CONTROL_PAUSE &&
            state->game.phase == SNAKE_PLAYING) {
            snake_game_toggle_pause(&state->game);
        } else if (event->code == RAYLIB_LITE_GAME_CONTROL_RESUME &&
                   state->game.phase == SNAKE_PAUSED) {
            snake_game_toggle_pause(&state->game);
        } else if (event->code == RAYLIB_LITE_GAME_CONTROL_RESET) {
            snake_game_reset(&state->game, state->game.rng);
        }
    }
}

static void update(void *value)
{
    snake_module_state_t *state = value;
    if (!state) {
        return;
    }
#if defined(RAYLIB_LITE_GAME_NATIVE)
    uint32_t score = state->game.score;
    snake_phase_t phase = state->game.phase;
#endif
    snake_game_update(&state->game);
#if defined(RAYLIB_LITE_GAME_NATIVE)
    if (state->game.score > score) {
        play_cue(state, SFX_EAT);
    }
    if (phase == SNAKE_PLAYING && state->game.phase == SNAKE_GAME_OVER) {
        play_cue(state, SFX_GAME_OVER);
    }
#endif
}

static int render(void *value)
{
    snake_module_state_t *state = value;
    snake_view_render(&state->game);
    return 0;
}

static uint32_t state_hash(const void *value)
{
    return snake_game_state_hash(&((const snake_module_state_t *)value)->game);
}

static int state_json(const void *value, char *output, size_t capacity)
{
    const snake_module_state_t *state = value;
    static const char *phases[] = {"start", "playing", "paused", "game_over"};
    const snake_game_t *game = &state->game;
    const char *phase = (unsigned)game->phase < 4 ? phases[game->phase] : "unknown";
    return snprintf(output, capacity,
        "{\"score\":%lu,\"best\":%lu,\"length\":%u,\"phase\":\"%s\","
        "\"speed\":%u,\"tick\":%lu,\"head_col\":%d,\"head_row\":%d,"
        "\"food_col\":%d,\"food_row\":%d,\"state_hash\":\"%08lx\"}",
        (unsigned long)game->score, (unsigned long)game->best, game->length,
        phase, game->move_interval, (unsigned long)game->tick,
        game->body[0].col, game->body[0].row, game->food.col, game->food.row,
        (unsigned long)snake_game_state_hash(game));
}

static const raylib_lite_game_module_v1_t s_module = {
    .descriptor = {RAYLIB_LITE_GAME_MODULE_ABI, "snake", "Mosaico Snake", 480, 480, 30, 1},
    .state_size = sizeof(snake_module_state_t),
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
