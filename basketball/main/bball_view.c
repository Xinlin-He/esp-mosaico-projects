// SPDX-License-Identifier: Apache-2.0
#include "bball_view.h"
#include "raylib_lite_raylib.h"
#include "raylib_lite_2d.h"
#include "assets_ids.h"
#include <math.h>
#ifndef RAYLIB_LITE_HOST_SIMULATION
#include "esp_timer.h"
#endif

// Texture-atlas assets (real basketball / rim+net / arcade interior). These
// load on BOTH the device and the Host simulator (the engine reads the atlas
// from the game_assets partition on device, from the generated assets on host)
// and fall back to vector drawing when an atlas is missing.
#ifdef RAYLIB_LITE_HOST_SIMULATION
#include "host_asset_runtime.h"
#endif
static raylib_lite_atlas_t s_ball_atlas;
static raylib_lite_atlas_t s_hoop_atlas;
static raylib_lite_atlas_t s_court_atlas;
static bool s_ball_ok = false;
static bool s_hoop_ok = false;
static bool s_court_ok = false;
// Where the orange rim sits inside hoop.png (measured by prep_hoop2.py):
// centre at (FX,FY) of the texture, ring half-width = FRX of the texture.
#define HOOP_FX 0.4986f
#define HOOP_FY 0.1446f
#define HOOP_FRX 0.4577f

#define SCREEN_W BBALL_SCREEN_W
#define SCREEN_H BBALL_SCREEN_H

// The whole screen is one upright arcade basketball cabinet, seen head-on:
//   marquee (LED scoreboard)  ->  steel tube frame (beam + two columns)
//   ->  backboard + rim + net inside a wire cage  ->  ball-return ramp
//   ->  base panel with coin slot / start button.
#define HEADER_H 72        // marquee height
#define BEAM_BOT 92        // bottom of the top cross beam
#define COL_W 28           // steel column width
#define PLAY_TOP 92        // top of the caged playfield
#define RAMP_TOP 380       // top edge of the ball-return ramp
#define BASE_TOP 452       // top of the base/control panel
#define POPUP_TICKS 26

// Single-point perspective "tunnel": the back opening (BOX_*) holds the
// backboard + rim, and the four walls flare out toward the front (FRONT_*):
//   floor = orange return ramp, sides = blue safety nets, top = lit beam.
#define TUN_FT 92          // front top (just under the marquee beam)
#define TUN_FB 452         // front bottom (just above the base panel)
#define BOX_L 150          // back opening (backboard recess)
#define BOX_R 330
#define BOX_T 104
#define BOX_B 300
#define FRONT_L 6
#define FRONT_R (SCREEN_W - 6)

// --- Palette --------------------------------------------------------------
static const Color C_STEEL = {150, 160, 178, 255};
static const Color C_EDGE = {205, 214, 228, 255};
static const Color C_FRAME = {44, 48, 58, 255};    // black steel tube
static const Color C_BULB_ON = {255, 240, 158, 255};
static const Color C_BULB_OFF = {96, 90, 54, 255};
// LED digits.
static const Color C_LED_BG = {9, 13, 24, 255};
static const Color C_LED_ON = {255, 66, 52, 255};
static const Color C_LED_OFF = {54, 18, 16, 255};
static const Color C_LED_SCORE = {255, 248, 226, 255}; // warm white, reads on the dark board
static const Color C_LED_SCORE_OFF = {36, 42, 58, 255};
static const Color C_LED_TIME = {96, 236, 156, 255};
static const Color C_LED_BEST = {255, 196, 70, 255};
// Playfield / hoop.
static const Color C_TARGET = {216, 66, 48, 255};
static const Color C_RIM = {236, 104, 34, 255};
// Perspective tunnel.
static const Color C_NET = {74, 156, 244, 255};       // blue safety net
static const Color C_RAMP_N = {236, 168, 70, 255};    // ramp near (bright)
static const Color C_RAMP_F = {150, 82, 26, 255};     // ramp far (shadow)
// UI text / feedback.
static const Color C_TEXT = {236, 242, 238, 255};
static const Color C_MUTE = {150, 184, 216, 255};
static const Color C_PANEL = {12, 18, 28, 236};
static const Color C_GOLD = {245, 214, 120, 255};
static const Color C_GOOD = {96, 232, 138, 255};
static const Color C_FEVER = {255, 96, 128, 255};

// --- Small helpers --------------------------------------------------------

static Color lerp_col(Color a, Color b, float t)
{
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    return (Color){(uint8_t)(a.r + (b.r - a.r) * t),
                   (uint8_t)(a.g + (b.g - a.g) * t),
                   (uint8_t)(a.b + (b.b - a.b) * t),
                   (uint8_t)(a.a + (b.a - a.a) * t)};
}

static Vector2 v2lerp(Vector2 a, Vector2 b, float t)
{
    return (Vector2){a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

static void draw_centered(const char *text, int y, int size, Color color)
{
    DrawText(text, (SCREEN_W - MeasureText(text, size)) / 2, y, size, color);
}

static void draw_right(const char *text, int right_x, int y, int size, Color color)
{
    DrawText(text, right_x - MeasureText(text, size), y, size, color);
}

static void draw_soft_shadow(float cx, float cy, float rx, float ry)
{
    for (int i = 3; i >= 1; --i) {
        float t = (float)i / 3.0f;
        DrawEllipse((int)cx, (int)cy, rx * (0.5f + 0.65f * t),
                    ry * (0.5f + 0.65f * t), ColorAlpha((Color){0, 0, 0, 255}, 0.11f));
    }
}

static void draw_glow(float x, float y, float r, Color c)
{
    // Two rings, not four. Each ring is a full per-pixel alpha fill (read +
    // blend + write PSRAM); on a fever frame that overlaps the previous frame's
    // DMA and steals the bandwidth it needs to stay ahead of the scan. Two
    // rings keep the halo readable while roughly halving that blend cost.
    for (int i = 2; i >= 1; --i) {
        float t = (float)i / 2.0f;
        DrawCircleV((Vector2){x, y}, r * (0.55f + 0.75f * t), ColorAlpha(c, 0.08f));
    }
}

// Round steel tube: cylindrical shading (dark edges, bright centre).
static void draw_tube_v(int x, int w, int y0, int y1, Color base)
{
    Color dk = lerp_col(base, (Color){0, 0, 0, 255}, 0.55f);
    Color br = lerp_col(base, (Color){255, 255, 255, 255}, 0.5f);
    for (int i = 0; i < w; ++i) {
        float s = sinf((float)i / (float)(w - 1) * 3.14159265f);
        DrawRectangle(x + i, y0, 1, y1 - y0, lerp_col(dk, br, s));
    }
}

static void draw_tube_h(int y, int h, int x0, int x1, Color base)
{
    Color dk = lerp_col(base, (Color){0, 0, 0, 255}, 0.55f);
    Color br = lerp_col(base, (Color){255, 255, 255, 255}, 0.5f);
    for (int i = 0; i < h; ++i) {
        float s = sinf((float)i / (float)(h - 1) * 3.14159265f);
        DrawRectangle(x0, y + i, x1 - x0, 1, lerp_col(dk, br, s));
    }
}

// --- LED (7-segment) scoreboard ------------------------------------------

static void seg_rect(int x, int y, int w, int h, bool on, Color on_c, Color off_c)
{
    if (on) {
        DrawRectangle(x - 1, y - 1, w + 2, h + 2, ColorAlpha(on_c, 0.32f));
        DrawRectangle(x, y, w, h, on_c);
    } else {
        DrawRectangle(x, y, w, h, off_c);
    }
}

static void draw_led_digit(int x, int y, int w, int h, int t, int d,
                           Color on, Color off)
{
    static const unsigned char seg[10] = {
        126, 48, 109, 121, 51, 91, 95, 112, 127, 123,
    };
    unsigned char m = (d >= 0 && d <= 9) ? seg[d] : 0;
    int vlen = (h - 3 * t) / 2;
    if (vlen < 1) {
        vlen = 1;
    }
    int mid = y + t + vlen;
    seg_rect(x + t, y, w - 2 * t, t, m & 64, on, off);
    seg_rect(x, y + t, t, vlen, m & 2, on, off);
    seg_rect(x + w - t, y + t, t, vlen, m & 32, on, off);
    seg_rect(x + t, mid, w - 2 * t, t, m & 1, on, off);
    seg_rect(x, mid + t, t, vlen, m & 4, on, off);
    seg_rect(x + w - t, mid + t, t, vlen, m & 16, on, off);
    seg_rect(x + t, y + h - t, w - 2 * t, t, m & 8, on, off);
}

static void draw_led_number(int right_x, int y, int w, int h, int t, int gap,
                            unsigned value, int min_digits, Color on, Color off)
{
    int digits[8];
    int n = 0;
    unsigned v = value;
    do {
        digits[n++] = (int)(v % 10u);
        v /= 10u;
    } while (v && n < 8);
    while (n < min_digits && n < 8) {
        digits[n++] = 0;
    }
    int x = right_x - w;
    for (int i = 0; i < n; ++i) {
        draw_led_digit(x, y, w, h, t, digits[i], on, off);
        x -= (w + gap);
    }
}

static void draw_marquee(const bball_game_t *g)
{
    DrawRectangleGradientV(0, 0, SCREEN_W, HEADER_H, (Color){34, 38, 52, 255},
                           (Color){22, 25, 36, 255});

    /* The glass is a rounded square. Labels and digits that sit in the
     * top corners are covered by the bezel, so the board stays in the
     * flat middle of the top edge. */
    const int side = 88;
    const int plate_x = 72;
    const int plate_y = 14;
    const int plate_w = SCREEN_W - 2 * plate_x;
    const int plate_h = HEADER_H - plate_y - 6;

    for (int x = 132; x <= SCREEN_W - 132; x += 24) {
        int idx = x / 24;
        bool on = g->fever ? ((((int)(g->tick / 2) + idx) & 1) == 0)
                           : ((((int)(g->tick / 6) + idx) & 3) == 0);
        if (on) {
            draw_glow((float)x, 8.0f, 6.0f, C_BULB_ON);
        }
        DrawCircle(x, 8, 2.5f, on ? C_BULB_ON : C_BULB_OFF);
        DrawCircleV((Vector2){(float)x - 0.6f, 7.2f}, 0.9f,
                    ColorAlpha((Color){255, 255, 255, 255}, on ? 0.9f : 0.2f));
    }

    DrawRectangleRounded((Rectangle){(float)plate_x, (float)plate_y,
                                     (float)plate_w, (float)plate_h},
                         0.4f, 8, C_LED_BG);
    DrawRectangle(plate_x + 10, plate_y + 2, plate_w - 20, 2,
                  ColorAlpha((Color){120, 120, 140, 255}, 0.25f));

    const int label_y = 18;
    const int digit_y = 34;

    DrawText("SCORE", side, label_y, 13, C_TEXT);
    draw_led_number(side + 16 * 3 + 3 * 2, digit_y, 16, 28, 3, 3,
                    (unsigned)g->score, 3, C_LED_SCORE, C_LED_SCORE_OFF);

    uint16_t secs = bball_game_seconds_left(g);
    bool urgent = g->fever || secs <= 10;
    Color tled = urgent ? C_LED_ON : C_LED_TIME;
    DrawText("TIME", (SCREEN_W - MeasureText("TIME", 13)) / 2, label_y, 13, C_MUTE);
    draw_led_number(SCREEN_W / 2 + 24, digit_y, 22, 30, 4, 4, (unsigned)secs, 2,
                    tled, C_LED_OFF);

    int best_right = SCREEN_W - side;
    draw_right("BEST", best_right, label_y, 13, C_MUTE);
    draw_led_number(best_right, digit_y, 16, 28, 3, 3, (unsigned)g->best, 3,
                    C_LED_BEST, C_LED_OFF);
}

// --- Animated arcade backdrop (flat front-on painted board) ---------------

// Translucent decorative basketball painted on the board.
static void draw_ball_wm(float x, float y, float r, float a)
{
    DrawCircle((int)x, (int)y, r, ColorAlpha((Color){232, 118, 46, 255}, a));
    Color s = ColorAlpha((Color){40, 20, 10, 255}, a * 1.4f);
    DrawLineEx((Vector2){x, y - r}, (Vector2){x, y + r}, 2.0f, s);
    DrawLineEx((Vector2){x - r, y}, (Vector2){x + r, y}, 2.0f, s);
    DrawEllipseLines((int)x, (int)y, r * 0.5f, r, s);
}

// Graffiti artwork on the recessed backboard: a faint painted ball high up
// plus a shadowed "STREET BASKETBALL" wordmark low on the board, clear of the
// rim and net so it stays readable.
static void draw_emblem(const bball_game_t *g, float cx, float cy)
{
    draw_ball_wm(cx, cy - 96.0f, 18.0f, g->fever ? 0.12f : 0.16f);
    Color sh = ColorAlpha((Color){0, 0, 0, 255}, 0.55f);
    Color tx = ColorAlpha(g->fever ? (Color){255, 186, 150, 255}
                                   : (Color){252, 184, 92, 255}, 0.92f);
    draw_centered("STREET", (int)cy + 1, 13, sh);
    draw_centered("STREET", (int)cy, 13, tx);
    draw_centered("BASKETBALL", (int)cy + 16, 17, sh);
    draw_centered("BASKETBALL", (int)cy + 15, 17, tx);
}

// Perspective net wall (one side of the tunnel). Front edge at screen side,
// back edge tucked against the backboard recess; nylon-blue grid.
static void draw_net_wall(const bball_game_t *g, Vector2 ft, Vector2 fb,
                          Vector2 bt, Vector2 bb)
{
    // Dark side panel behind the net (so it reads as netting in shadow, not
    // as blue tiles): near almost black-blue, fading darker toward the back.
    Color near_c = g->fever ? (Color){40, 16, 52, 255} : (Color){22, 34, 62, 255};
    Color far_c = g->fever ? (Color){14, 6, 22, 255} : (Color){8, 14, 30, 255};
    const int STEPS = 64;
    for (int i = 0; i < STEPS; ++i) {
        float u0 = (float)i / STEPS, u1 = (float)(i + 1) / STEPS;
        Vector2 t0 = v2lerp(ft, bt, u0), t1 = v2lerp(ft, bt, u1);
        Vector2 b0 = v2lerp(fb, bb, u0), b1 = v2lerp(fb, bb, u1);
        Color c = lerp_col(near_c, far_c, u0);
        DrawTriangle(t0, b0, b1, c);
        DrawTriangle(t0, b1, t1, c);
    }
    // Thin cyan mesh lines (fade with depth) = the actual safety net.
    for (int k = 0; k <= 10; ++k) {
        float u = (float)k / 10.0f;
        DrawLineEx(v2lerp(ft, bt, u), v2lerp(fb, bb, u), 1.0f,
                   ColorAlpha(C_NET, 0.5f - 0.28f * u));
    }
    for (int r = 0; r <= 8; ++r) {
        float vv = (float)r / 8.0f;
        Vector2 fp = v2lerp(ft, fb, vv), bp = v2lerp(bt, bb, vv);
        DrawLineEx(fp, bp, 0.9f, ColorAlpha(C_NET, 0.34f));
    }
    // Steel rail along the front lip of the frame.
    DrawLineEx(ft, fb, 3.0f, C_STEEL);
}

/* Each framebuffer is primed with the court photo once. Later frames only
 * paste the photo back over the sprites that moved. A full-screen copy while
 * the previous frame is still being sent stalls that transfer for tens of
 * milliseconds, and the panel scan crosses it. */
#define FB_SLOTS 4
#define UNDO_CAP 24

typedef struct {
    const void *pixels;
    bool primed;
    Rectangle undo[UNDO_CAP];
    int undo_n;
} fb_slot_t;

static fb_slot_t s_fb[FB_SLOTS];

// Force every framebuffer to repaint the full court on its next use. Called when
// a full-screen card (title / paused / game-over instruction box) disappears:
// the card can span more than one rotating buffer, and any buffer that is not
// revisited before the player looks at it keeps a corner of the box painted on
// the court (the "black line" residue in the bottom-right). A one-shot full
// re-prime per buffer wipes it; this only happens at a phase change, never while
// the ball is in flight.
static void invalidate_all_slots(void)
{
    for (int i = 0; i < FB_SLOTS; ++i) {
        s_fb[i].primed = false;
        s_fb[i].undo_n = 0;
    }
}

static fb_slot_t *claim_fb(const void *pixels)
{
    if (!pixels) {
        return NULL;
    }
    fb_slot_t *free_slot = NULL;
    for (int i = 0; i < FB_SLOTS; ++i) {
        if (s_fb[i].pixels == pixels) {
            return &s_fb[i];
        }
        if (!s_fb[i].pixels && !free_slot) {
            free_slot = &s_fb[i];
        }
    }
    if (!free_slot) {
        free_slot = &s_fb[0];
    }
    free_slot->pixels = pixels;
    free_slot->primed = false;
    free_slot->undo_n = 0;
    return free_slot;
}

static void undo_push(fb_slot_t *s, float x, float y, float w, float h)
{
    if (!s || s->undo_n >= UNDO_CAP || w < 1.0f || h < 1.0f) {
        return;
    }
    int x0 = (int)floorf(x) - 10;
    int y0 = (int)floorf(y) - 10;
    int x1 = (int)ceilf(x + w) + 10;
    int y1 = (int)ceilf(y + h) + 10;
    if (x0 < 0) {
        x0 = 0;
    }
    if (y0 < 0) {
        y0 = 0;
    }
    if (x1 > SCREEN_W) {
        x1 = SCREEN_W;
    }
    if (y1 > SCREEN_H) {
        y1 = SCREEN_H;
    }
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    s->undo[s->undo_n++] = (Rectangle){(float)x0, (float)y0,
                                       (float)(x1 - x0), (float)(y1 - y0)};
}

static void restore_court_rect(Rectangle r)
{
    const raylib_lite_sprite_frame_t *f =
        raylib_lite_atlas_get_frame(s_court_atlas, RAYLIB_LITE_ASSET_ID_COURT);
    if (!f || f->source.width < 1.0f || f->source.height < 1.0f) {
        return;
    }
    float sx = f->source.width / (float)SCREEN_W;
    float sy = f->source.height / (float)SCREEN_H;
    Rectangle src = {f->source.x + r.x * sx, f->source.y + r.y * sy,
                     r.width * sx, r.height * sy};
    DrawTexturePro(s_court_atlas.texture, src, r, (Vector2){0, 0}, 0.0f,
                   (Color){255, 255, 255, 255});
}

static void note_ball(fb_slot_t *s, float x, float y, float r)
{
    undo_push(s, x - r * 1.4f, y - r * 1.2f, r * 2.8f, r * 2.8f);
}

/* Largest COMBO punch. Glyphs are a 5x7 cell advanced by 6, scaled by
 * font/8, plus a 3px shadow. A rect that stops short of that leaves the
 * outer columns on the court; later buffers still hold them, so the sides
 * keep blinking pink and green after the word is gone. */
static void note_combo(fb_slot_t *s, const bball_game_t *g)
{
    if (g->combo_timer <= 0 || g->combo_pop < 2) {
        return;
    }
    const int scale = 68 / 8;
    unsigned v = g->combo_pop;
    int digits = 1;
    while (v >= 10u) {
        v /= 10u;
        digits++;
    }
    int chars = 7 + digits; /* "COMBO x" + digits */
    int tw = (chars - 1) * 6 * scale + 5 * scale;
    int tx = (SCREEN_W - tw) / 2;
    int top = 250 - 22;
    int bottom = 250 + 34 + 56; /* circle center + radius at full punch */
    int text_bottom = 250 + 7 * scale + 3;
    if (text_bottom > bottom) {
        bottom = text_bottom;
    }
    undo_push(s, (float)tx, (float)top, (float)(tw + 3),
              (float)(bottom - top));
}

static void note_dynamic(fb_slot_t *s, const bball_game_t *g)
{
    float dw = g->rim_rx / HOOP_FRX;
    float dx = g->hoop_cx - HOOP_FX * dw;
    float dy = g->hoop_cy - HOOP_FY * dw;
    // Fever adds the rim halo around the sprite; the restore rect must cover it
    // so no glow pixels linger. The smaller glow (draw_glow) needs less pad, and
    // a smaller court re-copy each fever frame means less PSRAM traffic racing
    // the in-flight transfer.
    float pad = g->fever ? 24.0f : 6.0f;
    undo_push(s, dx - pad, dy - pad, dw + pad * 2.0f, dw + pad * 2.0f);

    for (int i = 0; i < BBALL_BALL_CAP; ++i) {
        const bball_ball_t *b = &g->balls[i];
        if (b->alive) {
            note_ball(s, b->x, b->y, b->r);
        }
    }
    if ((g->phase == ARC_AIM && g->match_ticks > 0) ||
            g->phase == ARC_TITLE || g->phase == ARC_OVER) {
        note_ball(s, g->ball_x, g->ball_y, g->ball_r);
    }
    if (g->phase == ARC_AIM) {
        undo_push(s, (float)COL_W, (float)PLAY_TOP,
                  (float)(SCREEN_W - 2 * COL_W), 78.0f);
        undo_push(s, 150.0f, 368.0f, 180.0f, 48.0f);
        if (g->touch_aiming) {
            float x0 = g->swipe_x0 < g->swipe_x ? g->swipe_x0 : g->swipe_x;
            float y0 = g->swipe_y0 < g->swipe_y ? g->swipe_y0 : g->swipe_y;
            float x1 = g->swipe_x0 > g->swipe_x ? g->swipe_x0 : g->swipe_x;
            float y1 = g->swipe_y0 > g->swipe_y ? g->swipe_y0 : g->swipe_y;
            undo_push(s, x0, y0, x1 - x0 + 16.0f, y1 - y0 + 16.0f);
        }
    }
    undo_push(s, (float)COL_W, (float)RAMP_TOP - 36.0f, 140.0f, 32.0f);
    if (g->popup_timer > 0) {
        undo_push(s, g->popup_x - 90.0f, g->popup_y - 40.0f, 180.0f, 110.0f);
    }
    note_combo(s, g);
    if (g->phase == ARC_TITLE || g->phase == ARC_PAUSED || g->phase == ARC_OVER) {
        undo_push(s, 40.0f, 100.0f, 400.0f, 250.0f);
    }
}

static void draw_scene(const bball_game_t *g)
{
    // Real arcade-machine interior photo as the whole backdrop.
    if (s_court_ok) {
        const raylib_lite_sprite_frame_t *f =
            raylib_lite_atlas_get_frame(s_court_atlas, RAYLIB_LITE_ASSET_ID_COURT);
        if (f) {
            // Always draw the court with a white tint so the 480x480 blit stays
            // on the fast (memcpy / scale) raster path. Tinting the whole photo
            // in fever would drop every pixel onto the slow per-pixel blend
            // path and stutter, so the fever mood is carried by the pulsing red
            // border, rim glow and faster bulbs instead.
            DrawTexturePro(s_court_atlas.texture, f->source,
                           (Rectangle){0, 0, SCREEN_W, SCREEN_H},
                           (Vector2){0, 0}, 0.0f, (Color){255, 255, 255, 255});
            return;
        }
    }
    // Deep arena curtain behind the whole cabinet.
    DrawRectangleGradientV(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H,
                           g->fever ? (Color){40, 12, 46, 255} : (Color){14, 24, 62, 255},
                           g->fever ? (Color){20, 6, 26, 255} : (Color){8, 14, 34, 255});

    // Tunnel corners.
    Vector2 FLt = {FRONT_L, TUN_FT}, FRt = {FRONT_R, TUN_FT};
    Vector2 FLb = {FRONT_L, TUN_FB}, FRb = {FRONT_R, TUN_FB};
    Vector2 BLt = {BOX_L, BOX_T}, BRt = {BOX_R, BOX_T};
    Vector2 BLb = {BOX_L, BOX_B}, BRb = {BOX_R, BOX_B};

    // --- Back wall: grey graffiti backboard recess -----------------------
    Color kT = g->fever ? (Color){104, 24, 60, 255} : (Color){86, 90, 102, 255};
    Color kB = g->fever ? (Color){150, 36, 70, 255} : (Color){40, 44, 54, 255};
    DrawRectangleGradientV(BOX_L, BOX_T, BOX_R - BOX_L, BOX_B - BOX_T, kT, kB);
    // Soft focus glow + emblem art on the board.
    float cx = g->hoop_cx, cy = g->hoop_cy;
    for (int i = 4; i >= 1; --i) {
        DrawCircleV((Vector2){cx, cy}, 30.0f * (float)i,
                    ColorAlpha(g->fever ? (Color){255, 150, 200, 255}
                                        : (Color){150, 190, 255, 255}, 0.028f));
    }
    draw_emblem(g, (BOX_L + BOX_R) / 2.0f, 258.0f);

    // --- Ceiling: dark beam with a row of arcade bulbs -------------------
    DrawTriangle(FLt, BLt, BRt, (Color){24, 26, 36, 255});
    DrawTriangle(FLt, BRt, FRt, (Color){24, 26, 36, 255});
    for (int i = 0; i < 11; ++i) {
        float u = (float)i / 10.0f;
        float lx = FRONT_L + (FRONT_R - FRONT_L) * u;
        bool on = g->fever ? ((((int)(g->tick / 2) + i) & 1) == 0)
                           : ((((int)(g->tick / 5) + i) % 3) == 0);
        Color bc = on ? C_BULB_ON : C_BULB_OFF;
        if (on) draw_glow(lx, TUN_FT + 4.0f, 6.0f, C_BULB_ON);
        DrawCircle((int)lx, (int)(TUN_FT + 4.0f), 3.0f, bc);
    }

    // --- Side walls: blue safety nets ------------------------------------
    draw_net_wall(g, FLt, FLb, BLt, BLb);   // left
    draw_net_wall(g, FRt, FRb, BRt, BRb);   // right

    // --- Floor: orange ball-return ramp flaring to the player ------------
    for (int y = TUN_FB; y >= BOX_B; --y) {
        float t = (float)(TUN_FB - y) / (float)(TUN_FB - BOX_B);
        float lx = FRONT_L + (BOX_L - FRONT_L) * t;
        float rx = FRONT_R + (BOX_R - FRONT_R) * t;
        DrawLine((int)lx, y, (int)rx, y, lerp_col(C_RAMP_N, C_RAMP_F, t));
    }
    // Converging lane grooves on the ramp.
    for (int k = 1; k <= 3; ++k) {
        float u = (float)k / 4.0f;
        DrawLineEx(v2lerp(FLb, FRb, u), v2lerp(BLb, BRb, u), 1.4f,
                   ColorAlpha((Color){255, 226, 170, 255}, 0.35f));
    }
    // Bright front lip of the ramp.
    DrawLineEx(FLb, FRb, 3.0f, ColorAlpha((Color){255, 240, 200, 255}, 0.6f));
}

// --- Hoop -----------------------------------------------------------------

static void draw_net(const bball_game_t *g)
{
    float cx = g->hoop_cx, cy = g->hoop_cy;
    float rx = g->rim_rx, ry = g->rim_ry;
    float depth = ry * 3.6f;
    Color net = ColorAlpha((Color){238, 242, 248, 255}, 0.9f);
    Color netd = ColorAlpha((Color){170, 180, 196, 255}, 0.8f);
    const int N = 16;
    for (int i = 0; i < N; ++i) {
        float a = 3.14159265f * (0.05f + 0.90f * (float)i / (float)(N - 1));
        float tx = cx + cosf(a) * rx;
        float ty = cy + sinf(a) * ry;
        float bx = cx + cosf(a) * rx * 0.26f;
        float by = cy + depth;
        Color c = (i & 1) ? net : netd;
        Vector2 prev = {tx, ty};
        const int segs = 5;
        for (int s = 1; s <= segs; ++s) {
            float t = (float)s / (float)segs;
            float x = tx + (bx - tx) * t;
            float y = ty + (by - ty) * t + sinf(t * 3.14159f) * 3.0f;
            DrawLineEx(prev, (Vector2){x, y}, 1.4f, c);
            prev = (Vector2){x, y};
        }
    }
    for (int k = 1; k <= 4; ++k) {
        float t = (float)k / 4.0f;
        float rx2 = rx * (1.0f - 0.72f * t);
        float ry2 = ry * (1.0f - 0.7f * t);
        float y = cy + depth * t * 0.92f;
        DrawEllipseLines((int)cx, (int)y, rx2, ry2 * 0.55f + 1.0f,
                         ColorAlpha(net, 0.7f - 0.3f * t));
    }
}

static void draw_hoop(const bball_game_t *g)
{
    float cx = g->hoop_cx, cy = g->hoop_cy;
    float rx = g->rim_rx, ry = g->rim_ry;

    // Real rim + net sprite, scaled so its orange ring lands on the logical
    // rim (centre cx,cy; ring half-width == rx).
    if (s_hoop_ok) {
        const raylib_lite_sprite_frame_t *f =
            raylib_lite_atlas_get_frame(s_hoop_atlas, RAYLIB_LITE_ASSET_ID_HOOP);
        if (f) {
            if (g->fever) {
                draw_glow(cx, cy, rx + 6.0f, C_FEVER);
            }
            float dw = (rx / HOOP_FRX) * 1.0f; // draw ring aligned to logical rim
            float dh = dw; // hoop.png is square
            float dx = cx - HOOP_FX * dw;
            float dy = cy - HOOP_FY * dh;
            // Always blit the rim/net sprite with a white tint. A coloured tint
            // (the old fever pink) drops the whole ~200x200 sprite onto the slow
            // per-pixel blend path every frame, which stretches the frame past
            // the panel scan: fever shots stutter and the tail of the transfer
            // nicks the bottom-right of the screen. The fever mood is carried by
            // the pink rim glow and the pulsing border instead.
            DrawTexturePro(s_hoop_atlas.texture, f->source,
                           (Rectangle){dx, dy, dw, dh},
                           (Vector2){0, 0}, 0.0f, (Color){255, 255, 255, 255});
            return;
        }
    }

    // Backboard artwork painted on the recessed grey wall (ref-style):
    // a warm orange target halo behind the rim + a white shooter's square.
    Color halo = g->fever ? (Color){255, 120, 150, 255} : (Color){234, 150, 60, 255};
    for (int i = 5; i >= 1; --i) {
        DrawCircleV((Vector2){cx, cy - 6.0f}, rx * (0.7f + 0.32f * (float)i),
                    ColorAlpha(halo, 0.05f));
    }
    DrawCircleLines((int)cx, (int)(cy - 6.0f), rx * 1.15f, ColorAlpha(halo, 0.5f));
    float tw = rx * 1.1f, th = rx * 0.72f;
    Rectangle tgt = {cx - tw / 2.0f, cy - th - ry * 0.4f, tw, th};
    DrawRectangleLinesEx(tgt, 2.0f, ColorAlpha((Color){245, 250, 255, 255}, 0.8f));
    DrawRectangleLinesEx((Rectangle){cx - tw * 0.20f, tgt.y + th * 0.42f,
                                     tw * 0.40f, th * 0.5f}, 2.0f,
                         ColorAlpha(C_TARGET, 0.85f));

    draw_net(g);

    if (g->fever) {
        draw_glow(cx, cy, rx + 6.0f, C_FEVER);
    }
    Color rim = g->fever ? C_FEVER : C_RIM;
    Color rim_dk = lerp_col(rim, (Color){20, 12, 8, 255}, 0.55f);
    Color rim_hi = lerp_col(rim, (Color){255, 240, 210, 255}, 0.6f);
    DrawEllipse((int)cx, (int)(cy - 2.0f), rx, ry, rim_dk);
    DrawEllipse((int)cx, (int)cy, rx, ry, rim);
    DrawEllipse((int)cx, (int)cy, rx - 3.0f, ry - 2.0f, (Color){26, 18, 14, 255});
    for (int a = 10; a <= 170; a += 8) {
        float r0 = (float)a * 3.14159265f / 180.0f;
        float r1 = (float)(a + 8) * 3.14159265f / 180.0f;
        DrawLineEx((Vector2){cx + cosf(r0) * rx, cy + sinf(r0) * ry},
                   (Vector2){cx + cosf(r1) * rx, cy + sinf(r1) * ry}, 2.5f, rim_hi);
    }
}

// --- Ball -----------------------------------------------------------------

void bball_view_init_assets(const char *asset_root)
{
#ifdef RAYLIB_LITE_HOST_SIMULATION
    if (asset_root) {
        raylib_lite_host_assets_set_root(asset_root);
    }
#else
    (void)asset_root;
#endif
    // Load atlases on both device and host; leaves *_ok false on failure so the
    // renderer transparently falls back to vector drawing.
    s_ball_atlas = raylib_lite_atlas_load("ball.atlas");
    s_ball_ok = s_ball_atlas.texture.id != 0;
    s_hoop_atlas = raylib_lite_atlas_load("hoop.atlas");
    s_hoop_ok = s_hoop_atlas.texture.id != 0;
    s_court_atlas = raylib_lite_atlas_load("court.atlas");
    s_court_ok = s_court_atlas.texture.id != 0;
}

static void draw_ball(float x, float y, float r)
{
    // Prefer the real photographic basketball texture when available.
    if (s_ball_ok) {
        const raylib_lite_sprite_frame_t *f =
            raylib_lite_atlas_get_frame(s_ball_atlas, RAYLIB_LITE_ASSET_ID_BALL);
        if (f) {
            DrawTexturePro(s_ball_atlas.texture, f->source,
                           (Rectangle){x - r, y - r, r * 2.0f, r * 2.0f},
                           (Vector2){0.0f, 0.0f}, 0.0f,
                           (Color){255, 255, 255, 255});
            return;
        }
    }
    Color hi = {255, 178, 112, 255};
    Color mid = {224, 100, 34, 255};
    Color lo = {130, 46, 12, 255};
    int steps = (int)r + 2;
    for (int i = steps; i >= 1; --i) {
        float t = (float)i / (float)steps;
        Color c = (t < 0.5f) ? lerp_col(hi, mid, t * 2.0f)
                             : lerp_col(mid, lo, (t - 0.5f) * 2.0f);
        float ox = x - r * 0.16f * (1.0f - t);
        float oy = y - r * 0.18f * (1.0f - t);
        DrawCircle((int)ox, (int)oy, r * t, c);
    }
    DrawCircleV((Vector2){x - r * 0.32f, y - r * 0.34f}, r * 0.20f,
                ColorAlpha((Color){255, 244, 228, 255}, 0.7f));
    DrawCircleV((Vector2){x - r * 0.30f, y - r * 0.32f}, r * 0.09f,
                ColorAlpha((Color){255, 255, 255, 255}, 0.85f));
    // Classic basketball seams: a vertical centre line, a horizontal centre
    // line, and the two curved side seams (one narrow vertical ellipse).
    Color seam = {46, 20, 6, 255};
    float th = r * 0.11f;
    if (th < 1.2f) {
        th = 1.2f;
    }
    DrawLineEx((Vector2){x, y - r}, (Vector2){x, y + r}, th, seam);   // vertical
    DrawLineEx((Vector2){x - r, y}, (Vector2){x + r, y}, th, seam);   // horizontal
    DrawEllipseLines((int)x, (int)y, r * 0.9f, r, seam);             // left/right bows
    DrawCircleLines((int)x, (int)y, r, ColorAlpha(lo, 0.7f));
}

// --- Ball-return ramp -----------------------------------------------------

static void draw_ready_ball(const bball_game_t *g)
{
    float lift = (g->phase == ARC_AIM) ? g->power * 10.0f : 0.0f;
    draw_soft_shadow(g->ball_x, g->ball_y + g->ball_r * 0.9f, g->ball_r * 1.05f,
                     g->ball_r * 0.34f);
    draw_ball(g->ball_x, g->ball_y - lift, g->ball_r);
}

// --- Steel frame + base ---------------------------------------------------

static void draw_frame(void)
{
    if (s_court_ok) {
        return; // the court photo already includes the cabinet frame
    }
    // Two upright corner columns.
    draw_tube_v(0, COL_W, BEAM_BOT - 6, SCREEN_H, C_FRAME);
    draw_tube_v(SCREEN_W - COL_W, COL_W, BEAM_BOT - 6, SCREEN_H, C_FRAME);
    // Top cross beam over the columns.
    draw_tube_h(HEADER_H, BEAM_BOT - HEADER_H, 0, SCREEN_W, C_FRAME);
    // Corner joint bolts.
    Color bolt = C_STEEL, bhi = C_EDGE;
    int cxs[2] = {COL_W / 2, SCREEN_W - COL_W / 2};
    for (int i = 0; i < 2; ++i) {
        for (int y = BEAM_BOT + 30; y < SCREEN_H - 10; y += 90) {
            DrawCircle(cxs[i] + 1, y + 1, 3.0f, ColorAlpha((Color){0, 0, 0, 255}, 0.4f));
            DrawCircle(cxs[i], y, 3.0f, bolt);
            DrawCircleV((Vector2){cxs[i] - 1.0f, y - 1.0f}, 1.1f, bhi);
        }
    }
}

static void draw_base(void)
{
    DrawRectangleGradientV(0, BASE_TOP, SCREEN_W, SCREEN_H - BASE_TOP,
                           (Color){56, 60, 72, 255}, (Color){28, 31, 40, 255});
    DrawRectangle(0, BASE_TOP, SCREEN_W, 2, ColorAlpha(C_EDGE, 0.6f));

    // Coin slot (left).
    DrawRectangleRounded((Rectangle){60, BASE_TOP + 6, 42, 16}, 0.4f, 4,
                         (Color){14, 14, 18, 255});
    DrawRectangle(79, BASE_TOP + 9, 4, 10, (Color){198, 202, 212, 255});
    DrawText("INSERT COIN", 42, BASE_TOP + 4, 12, C_MUTE);

    // Brand plate (centre).
    draw_centered("HOOPS", BASE_TOP + 5, 20, C_GOLD);

    // Start button (right).
    DrawCircle(SCREEN_W - 78, BASE_TOP + 14, 11.0f, (Color){20, 120, 60, 255});
    DrawCircle(SCREEN_W - 78, BASE_TOP + 14, 9.0f, (Color){60, 210, 110, 255});
    DrawCircleV((Vector2){SCREEN_W - 81.0f, BASE_TOP + 11.0f}, 3.0f,
                ColorAlpha((Color){255, 255, 255, 255}, 0.7f));
    DrawText("START", SCREEN_W - 64, BASE_TOP + 8, 13, C_TEXT);
}

// --- Aiming aids ----------------------------------------------------------

static void draw_swipe(const bball_game_t *g)
{
    if (!g->touch_aiming) {
        return;
    }
    DrawLineEx((Vector2){g->swipe_x0, g->swipe_y0},
               (Vector2){g->swipe_x, g->swipe_y}, 4.0f, C_GOLD);
    DrawCircle((int)g->swipe_x0, (int)g->swipe_y0, 5.0f, C_MUTE);
    DrawCircle((int)g->swipe_x, (int)g->swipe_y, 7.0f, C_GOLD);
}

// One line, centered on the rim. Shared by the wind cue and the miss hint so
// both sit in the same band above the hoop.
static void draw_hoop_line(const bball_game_t *g, const char *txt, int y, int size,
                           Color ink, float alpha)
{
    int tw = MeasureText(txt, size);
    int x = (int)g->hoop_cx - tw / 2;
    if (x < COL_W + 4) {
        x = COL_W + 4;
    }
    if (x + tw > SCREEN_W - COL_W - 4) {
        x = SCREEN_W - COL_W - 4 - tw;
    }
    DrawText(txt, x + 1, y + 2, size, ColorAlpha((Color){20, 16, 18, 255}, 0.5f * alpha));
    DrawText(txt, x, y, size, ColorAlpha(ink, alpha));
}

static void draw_wind_marks(float x, float y, float dir, int count, Color color)
{
    float s = 6.5f;
    for (int i = 0; i < count; ++i) {
        float cx = x + (float)i * 14.0f;
        Vector2 tip = {cx + dir * s, y};
        Vector2 up = {cx - dir * 2.0f, y - s};
        Vector2 dn = {cx - dir * 2.0f, y + s};
        DrawLineEx(up, tip, 3.0f, color);
        DrawLineEx(dn, tip, 3.0f, color);
    }
}

// Above the rim, one line. The word uses the bitmap font; the arrows are
// drawn because that font has no < or >. More arrows mean a stronger push.
// Fever has no wind, and the fever banner uses this line.
static void draw_wind(const bball_game_t *g)
{
    if (g->fever) {
        return;
    }
    float mag = fabsf(g->wind);
    float t = mag / 15.0f;
    if (t < 0.0f) {
        t = 0.0f;
    } else if (t > 1.0f) {
        t = 1.0f;
    }
    int marks = 0;
    if (mag >= 0.6f) {
        marks = t < 0.4f ? 1 : (t < 0.72f ? 2 : 3);
    }
    bool left = g->wind < 0.0f;
    const int size = 20;
    const char *word = "WIND";
    int lw = MeasureText(word, size);
    float marks_w = marks > 0 ? (float)marks * 14.0f : 0.0f;
    float gap = marks > 0 ? 8.0f : 0.0f;
    float total = (float)lw + marks_w + gap;
    float origin = g->hoop_cx - total * 0.5f;
    float lo = (float)COL_W + 4.0f;
    float hi = (float)SCREEN_W - (float)COL_W - 4.0f - total;
    if (origin < lo) {
        origin = lo;
    } else if (origin > hi) {
        origin = hi;
    }
    float word_x = left ? origin + marks_w + gap : origin;
    float mark_x = left ? origin : origin + (float)lw + gap;
    int y = PLAY_TOP + 8;
    float alpha = marks > 0 ? 1.0f : 0.78f;
    Color ink = ColorAlpha(C_GOLD, alpha);
    Color shade = ColorAlpha((Color){20, 16, 18, 255}, 0.5f * alpha);
    DrawText(word, (int)word_x + 1, y + 2, size, shade);
    DrawText(word, (int)word_x, y, size, ink);
    if (marks > 0) {
        float dir = left ? -1.0f : 1.0f;
        float ax = left ? mark_x + 8.0f : mark_x + 6.0f;
        draw_wind_marks(ax, (float)y + (float)size * 0.42f, dir, marks, ink);
    }
}

static void draw_outlined(const char *txt, int x, int y, int size, Color fill, float alpha)
{
    Color edge = ColorAlpha((Color){12, 14, 20, 255}, alpha);
    Color ink = ColorAlpha(fill, alpha);
    DrawText(txt, x - 2, y, size, edge);
    DrawText(txt, x + 2, y, size, edge);
    DrawText(txt, x, y - 2, size, edge);
    DrawText(txt, x, y + 2, size, edge);
    DrawText(txt, x, y, size, ink);
}

static void draw_popup(const bball_game_t *g)
{
    if (g->popup_timer <= 0) {
        return;
    }
    float life = (float)g->popup_timer / (float)POPUP_TICKS;
    float alpha = life < 0.28f ? life / 0.28f : 1.0f;
    float rise = (1.0f - life) * 26.0f;
    int y = (int)(g->popup_y - rise);
    if (g->popup_kind == BBALL_SHOT_MADE) {
        Color ink = {255, 236, 120, 255};
        const char *txt = TextFormat("+%u", g->popup_points);
        int tw = MeasureText(txt, 40);
        draw_outlined(txt, (int)g->popup_x - tw / 2, y, 40, ink, alpha);
        const char *tag = g->fever ? "FEVER!" : "SWISH";
        int tagw = MeasureText(tag, 22);
        draw_outlined(tag, (int)g->popup_x - tagw / 2, y + 42, 22,
                      (Color){255, 255, 255, 255}, alpha);
    } else {
        const char *txt = "MORE POWER";
        Color ink = {186, 214, 255, 255};
        if (g->popup_hint == BBALL_HINT_HARD) {
            txt = "LESS POWER";
            ink = (Color){255, 186, 150, 255};
        } else if (g->popup_hint == BBALL_HINT_LEFT) {
            txt = "AIM LEFT";
            ink = (Color){255, 214, 168, 255};
        } else if (g->popup_hint == BBALL_HINT_RIGHT) {
            txt = "AIM RIGHT";
            ink = (Color){255, 214, 168, 255};
        }
        y = PLAY_TOP + 36 - (int)((1.0f - life) * 4.0f);
        draw_hoop_line(g, txt, y, 22, ink, alpha);
    }
}

// Streak make: the word punches in large, then floats up and fades.
static void draw_combo_pop(const bball_game_t *g)
{
    if (g->combo_timer <= 0 || g->combo_pop < 2) {
        return;
    }
    float t = (float)g->combo_timer / (float)BBALL_COMBO_POP_TICKS;
    float punch = t > 0.72f ? (t - 0.72f) / 0.28f : 0.0f;
    float alpha = t < 0.35f ? t / 0.35f : 1.0f;
    int size = 44 + (int)(24.0f * punch);
    int y = 250 - (int)((1.0f - t) * 22.0f);
    bool hot = g->combo_pop >= BBALL_HOT_STREAK;
    Color ink = hot ? C_FEVER : C_GOOD;
    const char *txt = TextFormat("COMBO x%u", g->combo_pop);
    int tw = MeasureText(txt, size);
    int x = (SCREEN_W - tw) / 2;
    DrawCircle(SCREEN_W / 2, y + size / 2, 22.0f + 34.0f * punch,
               ColorAlpha(ink, 0.22f * alpha));
    DrawText(txt, x + 3, y + 3, size, ColorAlpha((Color){0, 0, 0, 255}, 0.5f * alpha));
    DrawText(txt, x, y, size, ColorAlpha(ink, alpha));
}

// --- Play-field status ----------------------------------------------------

static void draw_status(const bball_game_t *g)
{
    if (g->fever) {
        draw_centered("* FEVER  x2 *", PLAY_TOP + 2, 24, C_FEVER);
    }
    DrawText(TextFormat("%u/%u", g->makes, g->attempts), COL_W + 8, RAMP_TOP - 28,
             20, C_MUTE);
}

// --- Cards ----------------------------------------------------------------

static void draw_panel(int y, int h, Color accent)
{
    DrawRectangleRounded((Rectangle){60, (float)y, 360, (float)h}, 0.1f, 8, C_PANEL);
    DrawRectangleRoundedLines((Rectangle){60, (float)y, 360, (float)h}, 0.1f, 8, accent);
}

static void draw_button_box(int x, int y, int w, int h, const char *text, int size,
                            Color accent)
{
    DrawRectangleRounded((Rectangle){(float)x, (float)y, (float)w, (float)h},
                         0.45f, 6, accent);
    int tw = MeasureText(text, size);
    DrawText(text, x + (w - tw) / 2, y + (h - size) / 2, size,
             (Color){40, 28, 8, 255});
}

static void draw_title_card(const bball_game_t *g)
{
    (void)g;
    draw_panel(156, 176, C_RIM);
    draw_centered("ARCADE HOOPS", 172, 32, C_TEXT);
    draw_centered("60s SHOOT-OUT", 214, 16, C_GOLD);
    draw_centered("Swipe up. Lean to aim", 244, 18, C_TEXT);
    draw_button_box((SCREEN_W - 168) / 2, 284, 168, 36, "TAP / ENTER", 16, C_RIM);
}

// Five pips: lit means a ball is ready. The bar is the wait until the next
// one returns, and it does not stop a shot while a pip is still lit.
static void draw_rack(const bball_game_t *g)
{
    if (g->phase != ARC_AIM) {
        return;
    }
    float y = 388.0f;
    float x0 = 240.0f - (BBALL_RACK - 1) * 11.0f;
    int ready = g->rack;
    if (ready < 0) {
        ready = 0;
    }
    if (ready > BBALL_RACK) {
        ready = BBALL_RACK;
    }
    for (int i = 0; i < BBALL_RACK; ++i) {
        DrawCircle((int)(x0 + i * 22.0f), (int)y, 7.0f,
                   i < ready ? C_LED_SCORE : (Color){48, 56, 72, 255});
    }
    if (g->rack < BBALL_RACK && g->reload_full > 0 && g->reload_timer > 0) {
        float frac = (float)g->reload_timer / (float)g->reload_full;
        DrawRectangle(196, 404, 88, 6, (Color){28, 34, 48, 230});
        DrawRectangle(196, 404, (int)(88.0f * frac), 6, C_GOLD);
    }
}

static void draw_pause_card(void)
{
    draw_panel(180, 120, C_GOLD);
    draw_centered("PAUSED", 198, 42, C_TEXT);
    draw_centered("Press P to resume", 250, 20, C_MUTE);
}

static void draw_over_card(const bball_game_t *g)
{
    bool new_best = g->score > 0 && g->score >= g->best;
    Color accent = new_best ? C_GOLD : C_RIM;
    draw_panel(116, 250, accent);
    draw_centered("TIME UP", 128, 40, accent);
    draw_centered(TextFormat("SCORE  %lu", (unsigned long)g->score), 176, 34, C_TEXT);
    if (new_best) {
        draw_centered("NEW BEST!", 218, 24, C_GOLD);
    } else {
        draw_centered(TextFormat("BEST  %lu", (unsigned long)g->best), 218, 22, C_MUTE);
    }
    draw_centered(TextFormat("makes %u/%u   streak %u",
                             g->makes, g->attempts, g->longest_streak), 250, 20,
                  C_MUTE);
    draw_button_box(BBALL_OVER_BTN_X, BBALL_OVER_BTN_Y, BBALL_OVER_BTN_W,
                    BBALL_OVER_BTN_H, "PLAY AGAIN", 18, C_GOLD);
}

// --- Top-level render -----------------------------------------------------

void bball_view_render(const bball_game_t *game)
{
    if (!game) {
        return;
    }
    BeginDrawing();

    // Logic ticks at 30 Hz but the panel presents slower, so slide the hoop
    // and the ball through the current tick instead of snapping per tick.
    bball_game_t draw = *game;
    float frac = 1.0f;
#ifndef RAYLIB_LITE_HOST_SIMULATION
    {
        static int64_t s_tick_us = 0;
        static uint32_t s_seen_tick = 0;
        int64_t now = esp_timer_get_time();
        if (s_seen_tick != game->tick) {
            s_seen_tick = game->tick;
            s_tick_us = now;
        }
        frac = (float)(now - s_tick_us) / (1000000.0f / (float)BBALL_TICK_HZ);
        if (frac < 0.0f) frac = 0.0f;
        if (frac > 1.0f) frac = 1.0f;
    }
#endif
    bball_game_present(game, frac, &draw.hoop_cx, &draw.ball_x, &draw.ball_y,
                       &draw.ball_r, draw.balls);
    game = &draw;

    // A full-screen card (title / paused / game-over) covers the playfield. When
    // it goes away, wipe every buffer once so no corner of the box lingers on a
    // buffer that the dirty-rect restore did not revisit in time.
    {
        static bool s_prev_card = true; // first frame shows the title card
        bool card = game->phase == ARC_TITLE || game->phase == ARC_PAUSED ||
                    game->phase == ARC_OVER;
        if (s_prev_card && !card) {
            invalidate_all_slots();
        }
        s_prev_card = card;
    }

    /*
     * The pinned engine does not expose framebuffer identities as a public
     * API. Keep the dirty-rectangle implementation dormant and redraw each
     * acquired frame completely; this is correct for every buffer in the
     * engine's rotation and avoids depending on a private accessor.
     */
    fb_slot_t *slot = claim_fb(NULL);
    if (!slot || !slot->primed) {
        draw_scene(game);
        if (slot) {
            slot->primed = true;
            slot->undo_n = 0;
        }
    } else {
        for (int i = 0; i < slot->undo_n; ++i) {
            restore_court_rect(slot->undo[i]);
        }
        slot->undo_n = 0;
    }

    bool aiming = game->phase == ARC_AIM && game->match_ticks > 0;
    // Misses stay in front of the rim (they clank off it). Only a made ball,
    // after it has reached the rim and while it is still inside the net,
    // is drawn behind the net sprite.
    for (int i = 0; i < BBALL_BALL_CAP; ++i) {
        const bball_ball_t *b = &game->balls[i];
        bool in_net = b->alive && b->outcome == BBALL_SHOT_MADE &&
                      b->settle_frame > 0 &&
                      b->y < game->hoop_cy + game->rim_rx * 1.35f;
        if (in_net) {
            draw_ball(b->x, b->y, b->r);
        }
    }
    draw_hoop(game);

    if (aiming || game->phase == ARC_TITLE || game->phase == ARC_OVER) {
        draw_ready_ball(game);
    }
    for (int i = 0; i < BBALL_BALL_CAP; ++i) {
        const bball_ball_t *b = &game->balls[i];
        bool in_net = b->alive && b->outcome == BBALL_SHOT_MADE &&
                      b->settle_frame > 0 &&
                      b->y < game->hoop_cy + game->rim_rx * 1.35f;
        if (b->alive && !in_net) {
            draw_ball(b->x, b->y, b->r);
        }
    }

    if (game->phase == ARC_AIM) {
        draw_wind(game);
    }
    if (aiming) {
        draw_swipe(game);
        draw_rack(game);
    }
    draw_popup(game);
    draw_combo_pop(game);

    // Cabinet frame + base sit in front of the caged playfield.
    draw_frame();
    draw_base();
    draw_marquee(game);
    draw_status(game);

    if (game->phase == ARC_TITLE) {
        draw_title_card(game);
    } else if (game->phase == ARC_PAUSED) {
        draw_pause_card();
    } else if (game->phase == ARC_OVER) {
        draw_over_card(game);
    }

    if (slot) {
        note_dynamic(slot, game);
    }

    // NOTE: no full-screen make/fever border. A screen-edge rectangle blinks
    // on the panel's rounded corners (players read it as a stray line in the
    // bottom-right), and drawing it every made/fever frame is extra per-pixel
    // blend work that stretches the transfer into the scan. The make and fever
    // feedback lives in the popup, combo punch, rim glow, fever banner, haptics
    // and audio instead.
    EndDrawing();
}
