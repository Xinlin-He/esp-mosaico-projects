// SPDX-License-Identifier: Apache-2.0
#include <stdbool.h>
#include <stdio.h>
#include "mosaico_game_module.h"
#include "mosaico_raylib_fast.h"
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
} bball_module_state_t;

static int initialize(void *value, const char *asset_root)
{
    bball_module_state_t *state = value;
    bball_game_reset(&state->game, 0x484f4f50U); // 'HOOP'
    bball_view_init_assets(asset_root);
    InitWindow(BBALL_SCREEN_W, BBALL_SCREEN_H, "Arcade Hoops");
    SetTargetFPS(30);
    return 0;
}

static void shutdown(void *value)
{
    (void)value;
}

static void input(void *value, const mosaico_host_input_v1_t *event)
{
    bball_module_state_t *state = value;
    if (!state || !event) {
        return;
    }
    if (event->type == MOSAICO_HOST_INPUT_POINTER) {
        bball_game_point(&state->game, (float)event->x, (float)event->y,
                         event->pressed);
    } else if (event->type == MOSAICO_HOST_INPUT_ACTION) {
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
    } else if (event->type == MOSAICO_HOST_INPUT_CONTROL) {
        if (event->code == MOSAICO_HOST_CONTROL_PAUSE &&
            state->game.phase != ARC_PAUSED) {
            bball_game_toggle_pause(&state->game);
        } else if (event->code == MOSAICO_HOST_CONTROL_RESUME &&
                   state->game.phase == ARC_PAUSED) {
            bball_game_toggle_pause(&state->game);
        } else if (event->code == MOSAICO_HOST_CONTROL_RESET) {
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
    bball_game_update(&state->game);
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

static const mosaico_game_module_v1_t s_module = {
    .descriptor = {MOSAICO_HOST_GAME_ABI_V1, "arcade_hoops", "Arcade Hoops",
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

const mosaico_game_module_v1_t *mosaico_game_module_v1(void)
{
    return &s_module;
}
