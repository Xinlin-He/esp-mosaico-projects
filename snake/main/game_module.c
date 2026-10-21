// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdio.h>
#include "mosaico_game_module.h"
#include "mosaico_raylib_fast.h"
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
} snake_module_state_t;

static int initialize(void *value, const char *asset_root)
{
    (void)asset_root;
    snake_module_state_t *state = value;
    snake_game_reset(&state->game, 0x534e414bU); // 'SNAK'
    InitWindow(480, 480, "Mosaico Snake");
    SetTargetFPS(30);
    return 0;
}

static void shutdown(void *value)
{
    (void)value;
}

static void input(void *value, const mosaico_host_input_v1_t *event)
{
    snake_module_state_t *state = value;
    if (!state || !event) {
        return;
    }
    if (event->type == MOSAICO_HOST_INPUT_POINTER) {
        snake_game_point(&state->game, (float)event->x, (float)event->y,
                         event->pressed);
    } else if (event->type == MOSAICO_HOST_INPUT_ACTION) {
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
    } else if (event->type == MOSAICO_HOST_INPUT_CONTROL) {
        if (event->code == MOSAICO_HOST_CONTROL_PAUSE &&
            state->game.phase == SNAKE_PLAYING) {
            snake_game_toggle_pause(&state->game);
        } else if (event->code == MOSAICO_HOST_CONTROL_RESUME &&
                   state->game.phase == SNAKE_PAUSED) {
            snake_game_toggle_pause(&state->game);
        } else if (event->code == MOSAICO_HOST_CONTROL_RESET) {
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
    snake_game_update(&state->game);
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

static const mosaico_game_module_v1_t s_module = {
    .descriptor = {MOSAICO_HOST_GAME_ABI_V1, "snake", "Mosaico Snake", 480, 480, 30, 1},
    .state_size = sizeof(snake_module_state_t),
    .initialize = initialize,
    .shutdown = shutdown,
    .input = input,
    .update = update,
    .render = render,
    .state_hash = state_hash,
    .state_json = state_json,
};

const mosaico_game_module_v1_t *mosaico_game_module_v1(void)
{
    return &s_module;
}
