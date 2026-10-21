// SPDX-License-Identifier: Apache-2.0
#include "snake_game.h"
#include <string.h>

static uint32_t next_random(snake_game_t *game)
{
    uint32_t x = game->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return game->rng = x ? x : 0x6d2b79f5U;
}

static bool dir_opposite(snake_dir_t a, snake_dir_t b)
{
    return (a == SNAKE_DIR_UP && b == SNAKE_DIR_DOWN) ||
           (a == SNAKE_DIR_DOWN && b == SNAKE_DIR_UP) ||
           (a == SNAKE_DIR_LEFT && b == SNAKE_DIR_RIGHT) ||
           (a == SNAKE_DIR_RIGHT && b == SNAKE_DIR_LEFT);
}

static void dir_step(snake_dir_t dir, int *out_dc, int *out_dr)
{
    switch (dir) {
    case SNAKE_DIR_UP:    *out_dc = 0;  *out_dr = -1; break;
    case SNAKE_DIR_DOWN:  *out_dc = 0;  *out_dr = 1;  break;
    case SNAKE_DIR_LEFT:  *out_dc = -1; *out_dr = 0;  break;
    case SNAKE_DIR_RIGHT: default: *out_dc = 1; *out_dr = 0; break;
    }
}

static bool cell_on_snake(const snake_game_t *game, int8_t col, int8_t row)
{
    for (uint16_t i = 0; i < game->length; ++i) {
        if (game->body[i].col == col && game->body[i].row == row) {
            return true;
        }
    }
    return false;
}

static void place_food(snake_game_t *game)
{
    const uint16_t free_cells =
        (uint16_t)(SNAKE_MAX_CELLS - game->length);
    if (free_cells == 0) {
        game->won = true;
        game->phase = SNAKE_GAME_OVER;
        return;
    }
    // Pick a random free cell by counting past occupied ones.
    uint16_t target = (uint16_t)(next_random(game) % free_cells);
    for (int row = 0; row < SNAKE_GRID_ROWS; ++row) {
        for (int col = 0; col < SNAKE_GRID_COLS; ++col) {
            if (cell_on_snake(game, (int8_t)col, (int8_t)row)) {
                continue;
            }
            if (target == 0) {
                game->food.col = (int8_t)col;
                game->food.row = (int8_t)row;
                return;
            }
            --target;
        }
    }
}

static void update_speed(snake_game_t *game)
{
    int interval = SNAKE_MOVE_INTERVAL_MAX - (int)(game->score / 40U);
    if (interval < SNAKE_MOVE_INTERVAL_MIN) {
        interval = SNAKE_MOVE_INTERVAL_MIN;
    }
    game->move_interval = (uint16_t)interval;
}

void snake_game_reset(snake_game_t *game, uint32_t seed)
{
    uint32_t best = game->best;
    memset(game, 0, sizeof(*game));
    game->best = best;
    game->phase = SNAKE_START;
    game->rng = seed ? seed : 1U;

    const int8_t start_col = SNAKE_GRID_COLS / 2;
    const int8_t start_row = SNAKE_GRID_ROWS / 2;
    game->length = SNAKE_START_LENGTH;
    for (uint16_t i = 0; i < game->length; ++i) {
        game->body[i].col = (int8_t)(start_col - (int)i);
        game->body[i].row = start_row;
    }
    game->dir = SNAKE_DIR_RIGHT;
    game->next_dir = SNAKE_DIR_RIGHT;
    update_speed(game);
    game->move_timer = game->move_interval;
    place_food(game);
}

void snake_game_start(snake_game_t *game)
{
    if (game->phase == SNAKE_START || game->phase == SNAKE_GAME_OVER) {
        snake_game_reset(game, game->rng);
        game->phase = SNAKE_PLAYING;
    }
}

void snake_game_set_dir(snake_game_t *game, snake_dir_t dir)
{
    if (game->phase == SNAKE_START || game->phase == SNAKE_GAME_OVER) {
        snake_game_start(game);
    }
    if (game->phase != SNAKE_PLAYING) {
        return;
    }
    // Reject reversals against the last committed heading so a quick double tap
    // cannot fold the snake back onto its own neck within a single move.
    if (!dir_opposite(dir, game->dir)) {
        game->next_dir = dir;
    }
}

void snake_game_point(snake_game_t *game, float x, float y, bool pressed)
{
    if (!pressed) {
        return;
    }
    if (game->phase == SNAKE_START || game->phase == SNAKE_GAME_OVER) {
        snake_game_start(game);
        return;
    }
    if (game->phase != SNAKE_PLAYING) {
        return;
    }
    // Steer toward the tapped side relative to the screen centre.
    const float dx = x - 240.0f;
    const float dy = y - 240.0f;
    const float ax = dx < 0 ? -dx : dx;
    const float ay = dy < 0 ? -dy : dy;
    if (ax > ay) {
        snake_game_set_dir(game, dx < 0 ? SNAKE_DIR_LEFT : SNAKE_DIR_RIGHT);
    } else {
        snake_game_set_dir(game, dy < 0 ? SNAKE_DIR_UP : SNAKE_DIR_DOWN);
    }
}

void snake_game_toggle_pause(snake_game_t *game)
{
    if (game->phase == SNAKE_PLAYING) {
        game->phase = SNAKE_PAUSED;
    } else if (game->phase == SNAKE_PAUSED) {
        game->phase = SNAKE_PLAYING;
    }
}

static void snake_die(snake_game_t *game)
{
    game->phase = SNAKE_GAME_OVER;
    game->death_flash = 12;
}

static void snake_move(snake_game_t *game)
{
    if (!dir_opposite(game->next_dir, game->dir)) {
        game->dir = game->next_dir;
    }

    int dc = 0, dr = 0;
    dir_step(game->dir, &dc, &dr);
    const int new_col = game->body[0].col + dc;
    const int new_row = game->body[0].row + dr;

    if (new_col < 0 || new_col >= SNAKE_GRID_COLS ||
        new_row < 0 || new_row >= SNAKE_GRID_ROWS) {
        snake_die(game);
        return;
    }

    const bool growing = game->grow > 0;
    // The tail cell frees up on a normal move, so colliding with it is legal
    // unless the snake is growing this step (the tail stays put).
    for (uint16_t i = 0; i < game->length; ++i) {
        if (game->body[i].col == new_col && game->body[i].row == new_row) {
            if (i == (uint16_t)(game->length - 1) && !growing) {
                break;
            }
            snake_die(game);
            return;
        }
    }

    if (growing && game->length < SNAKE_MAX_CELLS) {
        --game->grow;
        ++game->length;
    }
    for (uint16_t i = (uint16_t)(game->length - 1); i > 0; --i) {
        game->body[i] = game->body[i - 1];
    }
    game->body[0].col = (int8_t)new_col;
    game->body[0].row = (int8_t)new_row;

    if (new_col == game->food.col && new_row == game->food.row) {
        game->score += SNAKE_FOOD_VALUE;
        if (game->score > game->best) {
            game->best = game->score;
        }
        game->grow += SNAKE_GROW_PER_FOOD;
        game->eat_flash = 6;
        update_speed(game);
        place_food(game);
    }
}

void snake_game_update(snake_game_t *game)
{
    if (game->eat_flash) {
        --game->eat_flash;
    }
    if (game->death_flash) {
        --game->death_flash;
    }
    if (game->phase != SNAKE_PLAYING) {
        return;
    }
    ++game->tick;
    if (game->move_timer > 0) {
        --game->move_timer;
        return;
    }
    game->move_timer = game->move_interval;
    snake_move(game);
}

uint32_t snake_game_state_hash(const snake_game_t *game)
{
    const uint8_t *bytes = (const uint8_t *)game;
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < sizeof(*game); ++i) {
        hash = (hash ^ bytes[i]) * 16777619U;
    }
    return hash;
}
