// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>

// Classic grid snake. The playfield is a fixed grid; the head advances one cell
// per logic step. Speed increases with the score down to a floor. Hitting a
// wall or the snake's own body ends the run; filling the board wins it.
#define SNAKE_GRID_COLS 22
#define SNAKE_GRID_ROWS 18
#define SNAKE_MAX_CELLS (SNAKE_GRID_COLS * SNAKE_GRID_ROWS)

#define SNAKE_START_LENGTH 4
#define SNAKE_FOOD_VALUE 10
#define SNAKE_GROW_PER_FOOD 2
#define SNAKE_MOVE_INTERVAL_MAX 7
#define SNAKE_MOVE_INTERVAL_MIN 3

typedef enum {
    SNAKE_START,
    SNAKE_PLAYING,
    SNAKE_PAUSED,
    SNAKE_GAME_OVER,
} snake_phase_t;

typedef enum {
    SNAKE_DIR_UP,
    SNAKE_DIR_DOWN,
    SNAKE_DIR_LEFT,
    SNAKE_DIR_RIGHT,
} snake_dir_t;

typedef struct {
    int8_t col, row;
} snake_cell_t;

typedef struct {
    snake_phase_t phase;
    snake_cell_t body[SNAKE_MAX_CELLS]; // body[0] is the head
    uint16_t length;
    snake_dir_t dir;      // current heading applied on the last move
    snake_dir_t next_dir; // buffered input for the upcoming move
    snake_cell_t food;
    uint32_t score, best, tick, rng;
    uint16_t move_timer;    // logic ticks remaining before the next move
    uint16_t move_interval; // logic ticks between moves (lower is faster)
    uint16_t grow;          // pending growth segments
    uint8_t eat_flash;      // brief visual pulse after eating
    uint8_t death_flash;    // brief visual pulse after dying
    bool won;               // board completely filled
} snake_game_t;

// Reset to the title screen, preserving the best score across runs.
void snake_game_reset(snake_game_t *game, uint32_t seed);
// Begin (or restart) a run from the title or game-over screen.
void snake_game_start(snake_game_t *game);
// Request a heading. Starts a run when idle; ignores 180-degree reversals.
void snake_game_set_dir(snake_game_t *game, snake_dir_t dir);
// Pointer/touch control: taps start a run and steer toward the tapped side.
void snake_game_point(snake_game_t *game, float x, float y, bool pressed);
void snake_game_toggle_pause(snake_game_t *game);
void snake_game_update(snake_game_t *game);
uint32_t snake_game_state_hash(const snake_game_t *game);
