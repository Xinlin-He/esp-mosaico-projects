// SPDX-License-Identifier: Apache-2.0
#include "snake_view.h"
#include "mosaico_raylib_fast.h"

#define SCREEN_W 480
#define SCREEN_H 480
#define CELL 20
#define BOARD_X 20
#define BOARD_Y 96
#define BOARD_W (SNAKE_GRID_COLS * CELL)
#define BOARD_H (SNAKE_GRID_ROWS * CELL)

static const Color C_BG        = {8, 14, 20, 255};
static const Color C_BG_BOTTOM = {5, 9, 14, 255};
static const Color C_BOARD     = {14, 22, 30, 255};
static const Color C_GRID      = {22, 34, 44, 255};
static const Color C_FRAME     = {36, 92, 66, 255};
static const Color C_ACCENT    = {80, 224, 128, 255};
static const Color C_HEAD      = {150, 245, 160, 255};
static const Color C_BODY      = {58, 200, 112, 255};
static const Color C_BODY_DARK = {36, 150, 88, 255};
static const Color C_FOOD      = {240, 84, 84, 255};
static const Color C_FOOD_HI   = {255, 158, 158, 255};
static const Color C_LEAF      = {96, 210, 120, 255};
static const Color C_TEXT      = {225, 240, 230, 255};
static const Color C_MUTE      = {110, 140, 128, 255};
static const Color C_PANEL     = {10, 18, 26, 235};

static void draw_centered(const char *text, int y, int size, Color color)
{
    DrawText(text, (SCREEN_W - MeasureText(text, size)) / 2, y, size, color);
}

static int cell_x(int col) { return BOARD_X + col * CELL; }
static int cell_y(int row) { return BOARD_Y + row * CELL; }

static void draw_background(void)
{
    for (int band = 0; band < SCREEN_H; band += 8) {
        float t = (float)band / (float)SCREEN_H;
        Color c = {
            (uint8_t)(C_BG.r + (C_BG_BOTTOM.r - C_BG.r) * t),
            (uint8_t)(C_BG.g + (C_BG_BOTTOM.g - C_BG.g) * t),
            (uint8_t)(C_BG.b + (C_BG_BOTTOM.b - C_BG.b) * t),
            255,
        };
        DrawRectangle(0, band, SCREEN_W, 8, c);
    }
}

static void draw_hud(const snake_game_t *game)
{
    DrawRectangleRounded((Rectangle){12, 12, 456, 64}, 0.22f, 6,
                         (Color){12, 20, 28, 255});
    DrawRectangle(24, 24, 84, 3, C_ACCENT);

    DrawText("SCORE", 26, 20, 12, C_MUTE);
    DrawText(TextFormat("%05lu", (unsigned long)game->score), 26, 36, 24, C_TEXT);

    DrawText("BEST", 176, 20, 12, C_MUTE);
    DrawText(TextFormat("%05lu", (unsigned long)game->best), 176, 36, 24,
             (Color){245, 214, 120, 255});

    DrawText("LEN", 300, 20, 12, C_MUTE);
    DrawText(TextFormat("%u", game->length), 300, 36, 24, C_TEXT);

    DrawText("SPEED", 380, 20, 12, C_MUTE);
    int pips = SNAKE_MOVE_INTERVAL_MAX - game->move_interval + 1;
    for (int i = 0; i < 5; ++i) {
        Color c = i < pips ? C_ACCENT : (Color){40, 56, 48, 255};
        DrawRectangleRounded((Rectangle){(float)(382 + i * 16), 40, 12, 16},
                             0.35f, 4, c);
    }
}

static void draw_board(void)
{
    DrawRectangleRounded(
        (Rectangle){BOARD_X - 8, BOARD_Y - 8, BOARD_W + 16, BOARD_H + 16},
        0.05f, 6, C_FRAME);
    DrawRectangle(BOARD_X, BOARD_Y, BOARD_W, BOARD_H, C_BOARD);
    for (int col = 1; col < SNAKE_GRID_COLS; ++col) {
        DrawRectangle(cell_x(col), BOARD_Y, 1, BOARD_H, C_GRID);
    }
    for (int row = 1; row < SNAKE_GRID_ROWS; ++row) {
        DrawRectangle(BOARD_X, cell_y(row), BOARD_W, 1, C_GRID);
    }
}

static void draw_food(const snake_game_t *game)
{
    int cx = cell_x(game->food.col) + CELL / 2;
    int cy = cell_y(game->food.row) + CELL / 2;
    float pulse = game->eat_flash ? 1.5f : 0.0f;
    float radius = (float)(CELL / 2 - 3) + pulse;
    DrawCircle(cx, cy + 1, radius, (Color){120, 24, 24, 255});
    DrawCircle(cx, cy, radius, C_FOOD);
    DrawCircle(cx - 2, cy - 2, radius * 0.4f, C_FOOD_HI);
    DrawRectangleRounded((Rectangle){(float)(cx - 1), (float)(cy - CELL / 2), 3, 5},
                         0.5f, 3, C_LEAF);
}

static void draw_head_eyes(const snake_game_t *game, int x, int y)
{
    int dx1 = 0, dy1 = 0, dx2 = 0, dy2 = 0;
    switch (game->dir) {
    case SNAKE_DIR_UP:    dx1 = 5;  dy1 = 4;  dx2 = 12; dy2 = 4;  break;
    case SNAKE_DIR_DOWN:  dx1 = 5;  dy1 = 12; dx2 = 12; dy2 = 12; break;
    case SNAKE_DIR_LEFT:  dx1 = 4;  dy1 = 5;  dx2 = 4;  dy2 = 12; break;
    case SNAKE_DIR_RIGHT: default: dx1 = 12; dy1 = 5; dx2 = 12; dy2 = 12; break;
    }
    DrawCircle(x + dx1, y + dy1, 2.4f, (Color){20, 40, 28, 255});
    DrawCircle(x + dx2, y + dy2, 2.4f, (Color){20, 40, 28, 255});
}

static void draw_snake(const snake_game_t *game)
{
    for (uint16_t i = game->length; i > 0; --i) {
        const snake_cell_t *seg = &game->body[i - 1];
        int x = cell_x(seg->col);
        int y = cell_y(seg->row);
        bool head = (i - 1) == 0;
        Color color;
        if (head) {
            color = C_HEAD;
        } else {
            // Fade from bright behind the head to darker toward the tail.
            float t = (float)i / (float)game->length;
            color = (Color){
                (uint8_t)(C_BODY.r + (C_BODY_DARK.r - C_BODY.r) * t),
                (uint8_t)(C_BODY.g + (C_BODY_DARK.g - C_BODY.g) * t),
                (uint8_t)(C_BODY.b + (C_BODY_DARK.b - C_BODY.b) * t),
                255,
            };
        }
        DrawRectangleRounded(
            (Rectangle){(float)(x + 1), (float)(y + 1), CELL - 2, CELL - 2},
            0.35f, 4, color);
        if (head) {
            draw_head_eyes(game, x, y);
        }
    }
}

static void draw_button(const char *text, int y, int size, Color accent)
{
    int tw = MeasureText(text, size);
    int bw = tw + 44;
    int bx = (SCREEN_W - bw) / 2;
    DrawRectangleRounded((Rectangle){(float)bx, (float)y, (float)bw,
                                     (float)(size + 16)}, 0.5f, 6, accent);
    DrawText(text, (SCREEN_W - tw) / 2, y + 8, size, (Color){6, 24, 14, 255});
}

static void draw_panel(const char *title, Color accent)
{
    DrawRectangleRounded((Rectangle){70, 150, 340, 190}, 0.12f, 8, C_PANEL);
    DrawRectangleRoundedLines((Rectangle){70, 150, 340, 190}, 0.12f, 8, accent);
    draw_centered(title, 172, 40, C_TEXT);
    DrawRectangle(160, 224, 160, 2, accent);
}

static void draw_start_card(void)
{
    draw_panel("SNAKE", C_ACCENT);
    draw_centered("Arrow Keys / WASD to move", 244, 16, C_MUTE);
    draw_centered("Eat the fruit, avoid walls & tail", 268, 14, C_MUTE);
    draw_button("PRESS ENTER / TAP", 296, 16, C_ACCENT);
}

static void draw_pause_card(void)
{
    draw_panel("PAUSED", (Color){245, 214, 120, 255});
    draw_centered("Press P to resume", 258, 16, C_MUTE);
}

static void draw_over_card(const snake_game_t *game)
{
    Color accent = game->won ? C_ACCENT : (Color){240, 96, 96, 255};
    draw_panel(game->won ? "YOU WIN!" : "GAME OVER", accent);
    draw_centered(TextFormat("SCORE  %05lu", (unsigned long)game->score), 242, 20,
                  C_TEXT);
    draw_centered(TextFormat("BEST  %05lu", (unsigned long)game->best), 270, 15,
                  C_MUTE);
    draw_button("TAP / ENTER TO RETRY", 296, 15, accent);
}

void snake_view_render(const snake_game_t *game)
{
    if (!game) {
        return;
    }
    BeginDrawing();
    draw_background();
    draw_hud(game);
    draw_board();
    draw_food(game);
    draw_snake(game);

    if (game->phase == SNAKE_START) {
        draw_start_card();
    } else if (game->phase == SNAKE_PAUSED) {
        draw_pause_card();
    } else if (game->phase == SNAKE_GAME_OVER) {
        draw_over_card(game);
    }

    if (game->eat_flash) {
        DrawRectangleRoundedLines((Rectangle){BOARD_X - 8, BOARD_Y - 8,
                                              BOARD_W + 16, BOARD_H + 16},
                                  0.05f, 6, C_ACCENT);
    }
    if (game->death_flash && (game->death_flash & 1U)) {
        DrawRectangle(0, 0, SCREEN_W, 4, C_FOOD);
        DrawRectangle(0, SCREEN_H - 4, SCREEN_W, 4, C_FOOD);
        DrawRectangle(0, 0, 4, SCREEN_H, C_FOOD);
        DrawRectangle(SCREEN_W - 4, 0, 4, SCREEN_H, C_FOOD);
    }
    EndDrawing();
}
