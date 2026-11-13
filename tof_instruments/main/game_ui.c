// SPDX-License-Identifier: Apache-2.0

#include "game_ui.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GSP_BUNDLE_ENABLE_LEGACY_NAMES 1
#include "bundle_gsp.h"

#define GAME_TICK_MS          25U
#define NOTE_TRAVEL_MS        1800U
#define PERFECT_WINDOW_MS     78
#define GOOD_WINDOW_MS        165
#define NOTE_SLOTS_PER_TYPE   5U
#define NOTE_CANVAS_W         464
#define NOTE_CANVAS_H         120
#define NOTE_SIZE             56
#define NOTE_RADIUS           27
#define NOTE_Y                (-1)
#define NOTE_START_X          404
#define NOTE_TARGET_X         37
#define NOTE_DIRTY_PAD        2
#define SPRITE_OPAQUE         UINT32_C(0x10000)
#define TOF_THRESHOLD_STEP_MM   10
#define TOF_STRIKE_DEFAULT_MM   180
#define TOF_RELEASE_DEFAULT_MM  260
#define TOF_INTERVAL_STEP_MS    10
#define TOF_INTERVAL_DEFAULT_MS 120
#define TOF_SIDE_COUNT          2
#define TOF_MIN_STRENGTH        52U
#define TOF_MAX_STRENGTH        100U
#define TEST_FLASH_TICKS        8U

enum {
    HOLD_ADJ_NONE = 0,
    HOLD_ADJ_STRIKE_DOWN,
    HOLD_ADJ_STRIKE_UP,
    HOLD_ADJ_RELEASE_DOWN,
    HOLD_ADJ_RELEASE_UP,
    HOLD_ADJ_INTERVAL_DOWN,
    HOLD_ADJ_INTERVAL_UP,
    HOLD_ADJ_VOL_DOWN,
    HOLD_ADJ_VOL_UP,
};

enum {
    TOF_PHASE_ABSENT = 0,
    TOF_PHASE_READY,
    TOF_PHASE_APPROACH,
    TOF_PHASE_HOLD,
    TOF_PHASE_INTERVAL,
    TOF_PHASE_HIT,
};

enum {
    KODOU_VIEW_START = 0,
    KODOU_VIEW_SELECT,
    KODOU_VIEW_SETTINGS,
    KODOU_VIEW_PLAY,
    KODOU_VIEW_GUITAR,
    KODOU_VIEW_RESULT,
};

typedef struct {
    uint16_t time_ms;
    uint8_t type;
} chart_note_t;

typedef struct {
    const char *title_zh;
    const char *title_en;
    const char *meta_zh;
    const char *meta_en;
    const chart_note_t *chart;
    size_t chart_count;
    uint32_t duration_ms;
} song_desc_t;

/* Two original charts. Both keep per-type slot pressure below five notes
 * during the 1.8 s travel window. */
static const chart_note_t s_chart_shanhai[] = {
    {2000, KODOU_HIT_CENTER}, {2500, KODOU_HIT_CENTER},
    {3000, KODOU_HIT_RIM},    {3500, KODOU_HIT_CENTER},
    {4000, KODOU_HIT_RIM},    {4250, KODOU_HIT_RIM},
    {4500, KODOU_HIT_CENTER}, {5000, KODOU_HIT_CENTER},
    {5500, KODOU_HIT_RIM},    {6000, KODOU_HIT_CENTER},
    {6500, KODOU_HIT_CENTER}, {7000, KODOU_HIT_RIM},
    {7500, KODOU_HIT_CENTER}, {8000, KODOU_HIT_RIM},
    {8250, KODOU_HIT_CENTER}, {8500, KODOU_HIT_RIM},
    {9000, KODOU_HIT_CENTER}, {9500, KODOU_HIT_CENTER},
    {10000, KODOU_HIT_RIM},   {10500, KODOU_HIT_RIM},
    {11000, KODOU_HIT_CENTER},{11250, KODOU_HIT_CENTER},
    {11500, KODOU_HIT_RIM},   {12000, KODOU_HIT_CENTER},
    {12500, KODOU_HIT_RIM},   {13000, KODOU_HIT_CENTER},
    {13500, KODOU_HIT_CENTER},{14000, KODOU_HIT_RIM},
    {14500, KODOU_HIT_RIM},   {15000, KODOU_HIT_CENTER},
    {15500, KODOU_HIT_RIM},   {16000, KODOU_HIT_CENTER},
    {16250, KODOU_HIT_CENTER},{16500, KODOU_HIT_CENTER},
    {17000, KODOU_HIT_RIM},   {17500, KODOU_HIT_CENTER},
    {18000, KODOU_HIT_RIM},   {18500, KODOU_HIT_RIM},
    {19000, KODOU_HIT_CENTER},{19250, KODOU_HIT_RIM},
    {19500, KODOU_HIT_CENTER},{20000, KODOU_HIT_CENTER},
    {20500, KODOU_HIT_RIM},   {21000, KODOU_HIT_CENTER},
    {21500, KODOU_HIT_RIM},   {22000, KODOU_HIT_CENTER},
};

static const chart_note_t s_chart_zhuque[] = {
    {1760, KODOU_HIT_CENTER}, {2200, KODOU_HIT_RIM},
    {2640, KODOU_HIT_CENTER}, {3080, KODOU_HIT_CENTER},
    {3520, KODOU_HIT_RIM},    {3740, KODOU_HIT_CENTER},
    {3960, KODOU_HIT_RIM},    {4400, KODOU_HIT_CENTER},
    {4840, KODOU_HIT_RIM},    {5280, KODOU_HIT_CENTER},
    {5500, KODOU_HIT_RIM},    {5720, KODOU_HIT_CENTER},
    {6160, KODOU_HIT_CENTER}, {6600, KODOU_HIT_RIM},
    {7040, KODOU_HIT_CENTER}, {7480, KODOU_HIT_RIM},
    {7700, KODOU_HIT_RIM},    {7920, KODOU_HIT_CENTER},
    {8360, KODOU_HIT_CENTER}, {8800, KODOU_HIT_RIM},
    {9240, KODOU_HIT_CENTER}, {9460, KODOU_HIT_RIM},
    {9680, KODOU_HIT_CENTER}, {10120, KODOU_HIT_RIM},
    {10560, KODOU_HIT_CENTER},{11000, KODOU_HIT_CENTER},
    {11440, KODOU_HIT_RIM},   {11660, KODOU_HIT_CENTER},
    {11880, KODOU_HIT_RIM},   {12320, KODOU_HIT_CENTER},
    {12760, KODOU_HIT_RIM},   {13200, KODOU_HIT_CENTER},
    {13420, KODOU_HIT_CENTER},{13640, KODOU_HIT_RIM},
    {14080, KODOU_HIT_CENTER},{14520, KODOU_HIT_RIM},
    {14960, KODOU_HIT_CENTER},{15400, KODOU_HIT_RIM},
    {15620, KODOU_HIT_CENTER},{15840, KODOU_HIT_RIM},
    {16280, KODOU_HIT_CENTER},{16720, KODOU_HIT_CENTER},
    {17160, KODOU_HIT_RIM},   {17600, KODOU_HIT_CENTER},
    {17820, KODOU_HIT_RIM},   {18040, KODOU_HIT_CENTER},
    {18480, KODOU_HIT_RIM},   {18920, KODOU_HIT_CENTER},
    {19360, KODOU_HIT_RIM},   {19800, KODOU_HIT_CENTER},
};

/* AI-assisted original chart for an energetic Chinese-folk-rock arrangement.
 * It deliberately uses a new motif and rhythm rather than transcribing any
 * commercial recording or Taiko no Tatsujin chart. */
static const chart_note_t s_chart_pozhen[] = {
    {1644, KODOU_HIT_CENTER}, {2055, KODOU_HIT_CENTER},
    {2466, KODOU_HIT_RIM},    {2877, KODOU_HIT_CENTER},
    {3288, KODOU_HIT_RIM},    {3493, KODOU_HIT_CENTER},
    {3699, KODOU_HIT_RIM},    {4110, KODOU_HIT_CENTER},
    {4521, KODOU_HIT_CENTER}, {4932, KODOU_HIT_RIM},
    {5343, KODOU_HIT_CENTER}, {5548, KODOU_HIT_RIM},
    {5754, KODOU_HIT_CENTER}, {6165, KODOU_HIT_RIM},
    {6576, KODOU_HIT_CENTER}, {6987, KODOU_HIT_CENTER},
    {7398, KODOU_HIT_RIM},    {7603, KODOU_HIT_RIM},
    {7809, KODOU_HIT_CENTER}, {8220, KODOU_HIT_RIM},
    {8631, KODOU_HIT_CENTER}, {9042, KODOU_HIT_CENTER},
    {9453, KODOU_HIT_RIM},    {9658, KODOU_HIT_CENTER},
    {9864, KODOU_HIT_RIM},    {10275, KODOU_HIT_CENTER},
    {10686, KODOU_HIT_RIM},   {11097, KODOU_HIT_CENTER},
    {11508, KODOU_HIT_CENTER},{11713, KODOU_HIT_RIM},
    {11919, KODOU_HIT_CENTER},{12330, KODOU_HIT_RIM},
    {12741, KODOU_HIT_CENTER},{13152, KODOU_HIT_RIM},
    {13563, KODOU_HIT_CENTER},{13768, KODOU_HIT_CENTER},
    {13974, KODOU_HIT_RIM},   {14385, KODOU_HIT_CENTER},
    {14796, KODOU_HIT_RIM},   {15207, KODOU_HIT_CENTER},
    {15618, KODOU_HIT_CENTER},{15823, KODOU_HIT_RIM},
    {16029, KODOU_HIT_CENTER},{16440, KODOU_HIT_RIM},
    {16851, KODOU_HIT_CENTER},{17262, KODOU_HIT_RIM},
    {17673, KODOU_HIT_CENTER},{17878, KODOU_HIT_RIM},
    {18084, KODOU_HIT_CENTER},{18495, KODOU_HIT_CENTER},
    {18906, KODOU_HIT_RIM},   {19317, KODOU_HIT_CENTER},
    {19728, KODOU_HIT_RIM},   {19933, KODOU_HIT_CENTER},
    {20139, KODOU_HIT_RIM},   {20550, KODOU_HIT_CENTER},
    {20961, KODOU_HIT_CENTER},{21372, KODOU_HIT_RIM},
    {21783, KODOU_HIT_CENTER},{21988, KODOU_HIT_RIM},
    {22194, KODOU_HIT_CENTER},{22605, KODOU_HIT_RIM},
    {23016, KODOU_HIT_CENTER},{23427, KODOU_HIT_CENTER},
};

static const song_desc_t s_songs[KODOU_SONG_COUNT] = {
    {"山海疾风", "Shanhai Gale",
     "山海疾风 / 120 BPM", "Shanhai Gale / 120 BPM",
     s_chart_shanhai, sizeof(s_chart_shanhai) / sizeof(s_chart_shanhai[0]),
     24000U},
    {"朱雀夜奔", "Zhuque Run",
     "朱雀夜奔 / 138 BPM", "Zhuque Run / 138 BPM",
     s_chart_zhuque, sizeof(s_chart_zhuque) / sizeof(s_chart_zhuque[0]),
     22000U},
    {"破阵新声", "Battle Drum",
     "破阵新声 / 156 BPM", "Battle Drum / 156 BPM",
     s_chart_pozhen, sizeof(s_chart_pozhen) / sizeof(s_chart_pozhen[0]),
     26000U},
};

extern const uint16_t kodou_lane_art[NOTE_CANVAS_W * NOTE_CANVAS_H];
extern const uint16_t kodou_note_sprites[2][NOTE_SIZE * NOTE_SIZE];
static uint16_t s_lane_background[NOTE_CANVAS_W * NOTE_CANVAS_H];
static uint32_t s_note_sprites[2][NOTE_SIZE * NOTE_SIZE];
static bool s_lane_cache_ready;

static const song_desc_t *song(const kodou_game_t *game)
{
    return &s_songs[(unsigned)game->song % KODOU_SONG_COUNT];
}

static void prepare_lane_cache(void)
{
    if (s_lane_cache_ready) return;
    memcpy(s_lane_background, kodou_lane_art, sizeof(s_lane_background));
    for (int t = 0; t < 2; ++t) {
        for (int i = 0; i < NOTE_SIZE * NOTE_SIZE; ++i) {
            const uint16_t color = kodou_note_sprites[t][i];
            s_note_sprites[t][i] = color != 0U ? (SPRITE_OPAQUE | color) : 0U;
        }
    }
    s_lane_cache_ready = true;
}

static inline void canvas_pixel(const esp_gsp_canvas_surface_t *surface,
                                uint16_t x, uint16_t y, uint16_t color)
{
    uint8_t *pixel = (uint8_t *)surface->pixels +
        (size_t)(y - surface->y) * surface->stride_bytes;
    if (surface->pixel_format == ESP_GSP_CANVAS_PIXEL_RGB565) {
        pixel += (size_t)(x - surface->x) * 2U;
        memcpy(pixel, &color, sizeof(color));
    } else {
        pixel += (size_t)(x - surface->x) * 3U;
        pixel[0] = (uint8_t)((color & 0x1fU) << 3);
        pixel[1] = (uint8_t)(((color >> 5) & 0x3fU) << 2);
        pixel[2] = (uint8_t)(((color >> 11) & 0x1fU) << 3);
    }
}

static void canvas_restore_background(const esp_gsp_canvas_surface_t *surface)
{
    if (surface->pixel_format == ESP_GSP_CANVAS_PIXEL_RGB565) {
        for (uint16_t row = 0; row < surface->height; ++row) {
            const uint16_t *source = s_lane_background +
                (size_t)(surface->y + row) * NOTE_CANVAS_W + surface->x;
            uint8_t *target = (uint8_t *)surface->pixels +
                (size_t)row * surface->stride_bytes;
            memcpy(target, source, (size_t)surface->width * sizeof(*source));
        }
        return;
    }
    for (uint16_t row = 0; row < surface->height; ++row) {
        const uint16_t y = (uint16_t)(surface->y + row);
        for (uint16_t col = 0; col < surface->width; ++col) {
            const uint16_t x = (uint16_t)(surface->x + col);
            canvas_pixel(surface, x, y,
                         s_lane_background[(size_t)y * NOTE_CANVAS_W + x]);
        }
    }
}

static int note_x(const kodou_game_t *game, size_t index)
{
    const song_desc_t *active = song(game);
    const chart_note_t *note = &active->chart[game->notes[index].chart_index];
    int32_t remaining = (int32_t)note->time_ms - (int32_t)game->elapsed_ms;
    if (remaining > (int32_t)NOTE_TRAVEL_MS) remaining = NOTE_TRAVEL_MS;
    return NOTE_TARGET_X +
        (int)(((int32_t)(NOTE_START_X - NOTE_TARGET_X) * remaining) /
              (int32_t)NOTE_TRAVEL_MS);
}

static void note_lane_draw(const esp_gsp_canvas_surface_t *surface, void *ctx)
{
    kodou_game_t *game = ctx;
    canvas_restore_background(surface);

    for (size_t i = 0; i < sizeof(game->notes) / sizeof(game->notes[0]); ++i) {
        /* Draw the snapshot that produced the dirty rectangle. Recomputing the
         * position here would drift ahead of the damage span whenever a frame
         * is coalesced or delayed, clipping the leading edge of the note and
         * leaving its previous position behind. */
        if (!game->notes[i].rendered_active) continue;
        const int left = game->notes[i].rendered_x;
        const int top = NOTE_Y;
        int x1 = left > (int)surface->x ? left : (int)surface->x;
        int y1 = top > (int)surface->y ? top : (int)surface->y;
        int x2 = left + NOTE_SIZE;
        int y2 = top + NOTE_SIZE;
        const int surface_x2 = surface->x + surface->width;
        const int surface_y2 = surface->y + surface->height;
        if (x2 > surface_x2) x2 = surface_x2;
        if (y2 > surface_y2) y2 = surface_y2;
        if (x1 >= x2 || y1 >= y2) continue;

        const uint32_t *sprite = s_note_sprites[
            game->notes[i].type == KODOU_HIT_CENTER ? 0 : 1];
        if (surface->pixel_format == ESP_GSP_CANVAS_PIXEL_RGB565) {
            for (int y = y1; y < y2; ++y) {
                uint16_t *target = (uint16_t *)((uint8_t *)surface->pixels +
                    (size_t)(y - surface->y) * surface->stride_bytes);
                const uint32_t *source = sprite +
                    (size_t)(y - top) * NOTE_SIZE;
                for (int x = x1; x < x2; ++x) {
                    const uint32_t pixel = source[x - left];
                    if ((pixel & SPRITE_OPAQUE) != 0U) {
                        target[x - surface->x] = (uint16_t)pixel;
                    }
                }
            }
            continue;
        }
        for (int y = y1; y < y2; ++y) {
            for (int x = x1; x < x2; ++x) {
                const uint32_t pixel = sprite[
                    (size_t)(y - top) * NOTE_SIZE + (size_t)(x - left)];
                if ((pixel & SPRITE_OPAQUE) != 0U) {
                    canvas_pixel(surface, (uint16_t)x, (uint16_t)y,
                                 (uint16_t)pixel);
                }
            }
        }
    }
}

static void remember(kodou_game_t *game, esp_gsp_err_t err)
{
    if (err != ESP_GSP_OK) game->last_error = (int)err;
}

static void set_text(kodou_game_t *game, uint16_t bind_id,
                     const char *text)
{
    remember(game, esp_gsp_set_text(game->ui, bind_id, text));
}

static const char *tr(const kodou_game_t *game, const char *zh, const char *en)
{
    return game->english ? en : zh;
}

static void set_visible(kodou_game_t *game, uint16_t bind_id,
                        bool visible)
{
    remember(game, esp_gsp_set_visible(game->ui, bind_id, visible));
}

static void show_feedback(kodou_game_t *game, const char *text,
                          uint8_t hold_ticks)
{
    set_text(game, GSP_BIND_JUDGE_TEXT, text);
    game->judge_hold_ticks = hold_ticks;
}

typedef enum {
    JUDGE_BURST_NONE,
    JUDGE_BURST_PERFECT,
    JUDGE_BURST_GOOD,
    JUDGE_BURST_MISS,
    JUDGE_BURST_EMPTY,
    JUDGE_BURST_COMBO_BREAK,
} judge_burst_t;

static uint16_t judge_burst_bind(judge_burst_t burst)
{
    switch (burst) {
    case JUDGE_BURST_PERFECT:
        return GSP_BIND_PERFECT_BURST_VISIBLE;
    case JUDGE_BURST_GOOD:
        return GSP_BIND_GOOD_BURST_VISIBLE;
    case JUDGE_BURST_MISS:
    case JUDGE_BURST_EMPTY:
    case JUDGE_BURST_COMBO_BREAK:
        return GSP_BIND_MISS_BURST_VISIBLE;
    case JUDGE_BURST_NONE:
    default:
        return 0;
    }
}

static void hide_judge_burst(kodou_game_t *game)
{
    game->judge_burst_ticks = 0;
    const judge_burst_t active = (judge_burst_t)game->judge_burst_kind;
    const uint16_t key = judge_burst_bind(active);
    if (key != 0) set_visible(game, key, false);
    game->judge_burst_kind = JUDGE_BURST_NONE;
    set_visible(game, GSP_BIND_JUDGE_READOUT_VISIBLE, true);
}

static void show_judge_burst(kodou_game_t *game, judge_burst_t burst,
                             uint8_t hold_ticks)
{
    const judge_burst_t active = (judge_burst_t)game->judge_burst_kind;
    const uint16_t active_key = judge_burst_bind(active);
    const uint16_t next_key = judge_burst_bind(burst);
    if (burst != active) {
        const char *text = NULL;
        if (burst == JUDGE_BURST_MISS) text = tr(game, "失误", "Miss");
        else if (burst == JUDGE_BURST_EMPTY) text = tr(game, "空振", "Air");
        else if (burst == JUDGE_BURST_COMBO_BREAK) text = tr(game, "连击中断", "Break");
        if (text != NULL) {
            set_text(game, GSP_BIND_MISS_BURST_TEXT, text);
        }
    }
    if (active_key != next_key) {
        if (active_key != 0) set_visible(game, active_key, false);
        if (next_key != 0) set_visible(game, next_key, true);
    }
    game->judge_burst_kind = (uint8_t)burst;
    game->judge_burst_ticks = hold_ticks;
    set_visible(game, GSP_BIND_JUDGE_READOUT_VISIBLE, false);
}

static void publish_score(kodou_game_t *game, bool force)
{
    char text[20];
    if (force || game->score != game->published_score) {
        (void)snprintf(text, sizeof(text), "%07lu",
                       (unsigned long)(game->score % 10000000U));
        set_text(game, GSP_BIND_SCORE_TEXT, text);
        game->published_score = game->score;
    }
    if (force || game->combo != game->published_combo) {
        (void)snprintf(text, sizeof(text), "%lu",
                       (unsigned long)game->combo);
        set_text(game, GSP_BIND_COMBO_TEXT, text);
        game->published_combo = game->combo;
    }
}

static void hide_result(kodou_game_t *game)
{
    set_visible(game, GSP_BIND_RESULT_VISIBLE, false);
}

static void hide_combo_burst(kodou_game_t *game)
{
    game->combo_burst_ticks = 0;
    set_visible(game, GSP_BIND_COMBO_BURST_VISIBLE, false);
}

static void celebrate_combo(kodou_game_t *game)
{
    if (game->combo < 5U ||
        (game->combo != 5U && (game->combo % 10U) != 0U)) return;
    char text[24];
    (void)snprintf(text, sizeof(text),
                   game->english ? "%lu Combo!" : "%lu 连击！",
                   (unsigned long)game->combo);
    set_text(game, GSP_BIND_COMBO_BURST_TEXT, text);
    set_visible(game, GSP_BIND_COMBO_BURST_VISIBLE, true);
    game->combo_burst_ticks = 11U;
}

static void publish_song(kodou_game_t *game)
{
    const song_desc_t *active = song(game);
    set_text(game, GSP_BIND_SONG_TEXT,
             game->english ? active->meta_en : active->meta_zh);
    set_text(game, GSP_BIND_SONG_BUTTON_TEXT,
             game->english ? active->title_en : active->title_zh);
    set_text(game, GSP_BIND_SONG_SHANHAI_TEXT,
             game->english ? s_songs[KODOU_SONG_SHANHAI].meta_en
                           : s_songs[KODOU_SONG_SHANHAI].meta_zh);
    set_text(game, GSP_BIND_SONG_ZHUQUE_TEXT,
             game->english ? s_songs[KODOU_SONG_ZHUQUE].meta_en
                           : s_songs[KODOU_SONG_ZHUQUE].meta_zh);
    set_text(game, GSP_BIND_SONG_POZHEN_TEXT,
             game->english ? s_songs[KODOU_SONG_POZHEN].meta_en
                           : s_songs[KODOU_SONG_POZHEN].meta_zh);
}

static void publish_volume(kodou_game_t *game)
{
    char text[16];
    (void)snprintf(text, sizeof(text), "%u%%", (unsigned)game->volume_pct);
    set_text(game, GSP_BIND_VOLUME_TEXT, text);
    (void)snprintf(text, sizeof(text), "%u %%", (unsigned)game->volume_pct);
    set_text(game, GSP_BIND_VOL_VAL_TEXT, text);
    game->shown_volume_pct = (int)game->volume_pct;
}

static void publish_guitar_chord(kodou_game_t *game)
{
    static const uint16_t k_chord_binds[6] = {
        GSP_BIND_GUITAR_CHORD_0_VISIBLE,
        GSP_BIND_GUITAR_CHORD_1_VISIBLE,
        GSP_BIND_GUITAR_CHORD_2_VISIBLE,
        GSP_BIND_GUITAR_CHORD_3_VISIBLE,
        GSP_BIND_GUITAR_CHORD_4_VISIBLE,
        GSP_BIND_GUITAR_CHORD_5_VISIBLE,
    };
    static const uint16_t k_dot_binds[6][3] = {
        {GSP_BIND_GDOT_S0_F0_VISIBLE, GSP_BIND_GDOT_S0_F1_VISIBLE, GSP_BIND_GDOT_S0_F2_VISIBLE},
        {GSP_BIND_GDOT_S1_F0_VISIBLE, GSP_BIND_GDOT_S1_F1_VISIBLE, GSP_BIND_GDOT_S1_F2_VISIBLE},
        {GSP_BIND_GDOT_S2_F0_VISIBLE, GSP_BIND_GDOT_S2_F1_VISIBLE, GSP_BIND_GDOT_S2_F2_VISIBLE},
        {GSP_BIND_GDOT_S3_F0_VISIBLE, GSP_BIND_GDOT_S3_F1_VISIBLE, GSP_BIND_GDOT_S3_F2_VISIBLE},
        {GSP_BIND_GDOT_S4_F0_VISIBLE, GSP_BIND_GDOT_S4_F1_VISIBLE, GSP_BIND_GDOT_S4_F2_VISIBLE},
        {GSP_BIND_GDOT_S5_F0_VISIBLE, GSP_BIND_GDOT_S5_F1_VISIBLE, GSP_BIND_GDOT_S5_F2_VISIBLE},
    };
    static const int8_t k_frets[6][6] = {
        {-1, 3, 2, 0, 1, 0},
        {3, 2, 0, 0, 0, 3},
        {-1, 0, 2, 2, 1, 0},
        {1, 3, 3, 2, 1, 1},
        {0, 2, 2, 0, 0, 0},
        {-1, -1, 0, 2, 3, 2},
    };
    const uint8_t active = (uint8_t)(game->guitar_chord % 6U);
    for (uint8_t i = 0; i < 6U; ++i) {
        set_visible(game, k_chord_binds[i], i == active);
    }
    for (uint8_t s = 0; s < 6U; ++s) {
        const int8_t fret = k_frets[active][s];
        for (uint8_t f = 0; f < 3U; ++f) {
            set_visible(game, k_dot_binds[s][f], fret == (int8_t)(f + 1U));
        }
    }
}

static void publish_selection(kodou_game_t *game)
{
    char text[48];
    const song_desc_t *active = song(game);
    (void)snprintf(text, sizeof(text), game->english ? "Selected %s" : "已选 %s",
                   game->english ? active->title_en : active->title_zh);
    set_text(game, GSP_BIND_SELECTED_TEXT, text);
    set_visible(game, GSP_BIND_MARK_SHANHAI_VISIBLE,
                game->song == KODOU_SONG_SHANHAI);
    set_visible(game, GSP_BIND_MARK_ZHUQUE_VISIBLE,
                game->song == KODOU_SONG_ZHUQUE);
    set_visible(game, GSP_BIND_MARK_POZHEN_VISIBLE,
                game->song == KODOU_SONG_POZHEN);
}

static void format_mm(char *text, size_t size, int mm)
{
    (void)snprintf(text, size, "%d mm", mm);
}

static void publish_thresholds(kodou_game_t *game)
{
    const int strike = atomic_load(&game->tof_strike_mm);
    const int release = atomic_load(&game->tof_release_mm);
    const int interval = atomic_load(&game->tof_interval_ms);
    char text[16];
    if (strike != game->shown_strike_mm) {
        format_mm(text, sizeof(text), strike);
        set_text(game, GSP_BIND_STRIKE_TEXT, text);
        game->shown_strike_mm = strike;
    }
    if (release != game->shown_release_mm) {
        format_mm(text, sizeof(text), release);
        set_text(game, GSP_BIND_RELEASE_TEXT, text);
        game->shown_release_mm = release;
    }
    if (interval != game->shown_interval_ms) {
        (void)snprintf(text, sizeof(text), "%d ms", interval);
        set_text(game, GSP_BIND_INTERVAL_TEXT, text);
        game->shown_interval_ms = interval;
    }
}

static void publish_distance(kodou_game_t *game, uint16_t bind_id,
                             int mm, int online, int *shown_mm,
                             int *shown_online)
{
    if (mm == *shown_mm && online == *shown_online) return;
    char text[16];
    if (!online) {
        set_text(game, bind_id, tr(game, "无模块", "No pad"));
    } else if (mm > 0 && mm < 8000) {
        format_mm(text, sizeof(text), mm);
        set_text(game, bind_id, text);
    } else {
        set_text(game, bind_id, "--");
    }
    *shown_mm = mm;
    *shown_online = online;
}

static const char *phase_text(const kodou_game_t *game, int phase, bool flashing)
{
    if (flashing) return tr(game, "击打", "Hit");
    switch (phase) {
    case TOF_PHASE_READY: return tr(game, "可击打", "Ready");
    case TOF_PHASE_APPROACH: return tr(game, "再靠近", "Closer");
    case TOF_PHASE_HOLD: return tr(game, "请松开", "Let go");
    case TOF_PHASE_INTERVAL: return tr(game, "间隔中", "Wait");
    default: return "-";
    }
}

static void publish_side_state(kodou_game_t *game, int index)
{
    const uint16_t state_key = index == 0
        ? GSP_BIND_TOF_LEFT_STATE_TEXT : GSP_BIND_TOF_RIGHT_STATE_TEXT;
    const uint16_t flash_key = index == 0
        ? GSP_BIND_TEST_CENTER_FLASH_VISIBLE : GSP_BIND_TEST_RIM_FLASH_VISIBLE;
    const int valid = atomic_load(index == 0
        ? &game->tof_left_valid : &game->tof_right_valid);
    const int gen = atomic_load(&game->tof_hit_gen[index]);
    if (gen != game->shown_hit_gen[index]) {
        game->shown_hit_gen[index] = gen;
        game->test_flash_ticks[index] = TEST_FLASH_TICKS;
        if (!game->started && game->hooks.feedback) {
            const int strength = atomic_load(&game->tof_hit_strength[index]);
            game->hooks.feedback(index == 0 ? KODOU_HIT_CENTER : KODOU_HIT_RIM,
                                 KODOU_JUDGE_GOOD,
                                 (uint8_t)(strength > 0 ? strength : 72),
                                 game->hooks.ctx);
        }
    }
    const bool flashing = game->test_flash_ticks[index] > 0;
    if (flashing) game->test_flash_ticks[index]--;
    const int phase = atomic_load(&game->tof_phase[index]);
    const int dist_mm = atomic_load(index == 0
        ? &game->tof_left_mm : &game->tof_right_mm);
    const int strike_mm = atomic_load(&game->tof_strike_mm);
    const bool in_range = (valid && dist_mm > 0 && dist_mm <= strike_mm) ||
                          phase == TOF_PHASE_HOLD;
    const bool active_color = flashing || in_range;
    if (active_color != game->test_flash_on[index]) {
        set_visible(game, flash_key, active_color);
        game->test_flash_on[index] = active_color;
        game->shown_phase[index] = -1;
    }
    const int shown = flashing ? TOF_PHASE_HIT
                               : (!valid ? TOF_PHASE_ABSENT : phase);
    if (shown == game->shown_phase[index]) return;
    const char *txt = !valid && !flashing
        ? tr(game, "可击打", "Ready")
        : phase_text(game, phase, flashing);
    set_text(game, state_key, txt);
    game->shown_phase[index] = shown;
}

static void publish_tof(kodou_game_t *game)
{
    const int left_mm = atomic_load(&game->tof_left_mm);
    const int right_mm = atomic_load(&game->tof_right_mm);
    const int left_online = atomic_load(&game->tof_left_online);
    const int right_online = atomic_load(&game->tof_right_online);
    /* One module counts as single-sided whichever slot it sits in. Only the
     * presence of two modules earns the side-by-side layout. */
    const int single_side = (left_online && !right_online) ? 0
                          : (right_online && !left_online) ? 1
                          : -1;
    const bool single = game->single_tof_mode || single_side >= 0;
    set_visible(game, GSP_BIND_SINGLE_TOF_VISIBLE, single);
    set_visible(game, GSP_BIND_DUAL_TOF_VISIBLE, !single);
    if (single) {
        const int mm = single_side == 1 ? right_mm : left_mm;
        const int online = single_side == 1 ? right_online : left_online;
        char text[16];
        if (!online) {
            set_text(game, GSP_BIND_TOF_SINGLE_TEXT, tr(game, "无模块", "No pad"));
        } else if (mm > 0 && mm < 8000) {
            format_mm(text, sizeof(text), mm);
            set_text(game, GSP_BIND_TOF_SINGLE_TEXT, text);
        } else {
            set_text(game, GSP_BIND_TOF_SINGLE_TEXT, "--");
        }
    }
    publish_distance(game, GSP_BIND_TOF_LEFT_TEXT,
                     left_mm, left_online,
                     &game->shown_left_mm, &game->shown_left_valid);
    publish_distance(game, GSP_BIND_TOF_RIGHT_TEXT,
                     right_mm, right_online,
                     &game->shown_right_mm, &game->shown_right_valid);
    publish_side_state(game, 0);
    publish_side_state(game, 1);
    const int flash_side = single_side > 0 ? 1 : 0;
    const int single_flash = (single && game->test_flash_on[flash_side]) ? 1 : 0;
    if (single_flash != game->shown_single_flash) {
        set_visible(game, GSP_BIND_SINGLE_TOF_FLASH_VISIBLE, single_flash != 0);
        game->shown_single_flash = single_flash;
    }
    publish_thresholds(game);
}

static void show_view(kodou_game_t *game, int view)
{
    game->view = view;
    set_visible(game, GSP_BIND_MENU_VISIBLE, view != KODOU_VIEW_PLAY);
    set_visible(game, GSP_BIND_START_VISIBLE, view == KODOU_VIEW_START);
    set_visible(game, GSP_BIND_SELECT_VISIBLE, view == KODOU_VIEW_SELECT);
    set_visible(game, GSP_BIND_SETTINGS_VISIBLE, view == KODOU_VIEW_SETTINGS);
    set_visible(game, GSP_BIND_GUITAR_VISIBLE, view == KODOU_VIEW_GUITAR);
    set_visible(game, GSP_BIND_RESULT_VISIBLE, view == KODOU_VIEW_RESULT);
    if (view == KODOU_VIEW_SELECT) publish_selection(game);
    if (view == KODOU_VIEW_GUITAR) publish_guitar_chord(game);
    if (view == KODOU_VIEW_SETTINGS) {
        game->shown_left_mm = -1;
        game->shown_right_mm = -1;
        game->shown_left_valid = -1;
        game->shown_right_valid = -1;
        game->shown_single_flash = -1;
        game->shown_strike_mm = -1;
        game->shown_release_mm = -1;
        game->shown_interval_ms = -1;
        game->shown_phase[0] = -1;
        game->shown_phase[1] = -1;
        game->shown_hit_gen[0] = atomic_load(&game->tof_hit_gen[0]);
        game->shown_hit_gen[1] = atomic_load(&game->tof_hit_gen[1]);
        game->test_flash_ticks[0] = 0;
        game->test_flash_ticks[1] = 0;
        game->test_flash_on[0] = false;
        game->test_flash_on[1] = false;
        set_visible(game, GSP_BIND_TEST_CENTER_FLASH_VISIBLE, false);
        set_visible(game, GSP_BIND_TEST_RIM_FLASH_VISIBLE, false);
        publish_tof(game);
    }
}

static void adjust_threshold(kodou_game_t *game, bool strike, int delta)
{
    int strike_mm = atomic_load(&game->tof_strike_mm);
    int release_mm = atomic_load(&game->tof_release_mm);
    if (strike) {
        strike_mm += delta;
        if (strike_mm < 0) strike_mm = 0;
        atomic_store(&game->tof_strike_mm, strike_mm);
    } else {
        release_mm += delta;
        if (release_mm < 0) release_mm = 0;
        atomic_store(&game->tof_release_mm, release_mm);
    }
    publish_thresholds(game);
}

static void adjust_interval(kodou_game_t *game, int delta)
{
    int interval_ms = atomic_load(&game->tof_interval_ms) + delta;
    if (interval_ms < 0) interval_ms = 0;
    atomic_store(&game->tof_interval_ms, interval_ms);
    publish_thresholds(game);
}

static void adjust_volume_pct(kodou_game_t *game, int delta)
{
    int pct = (int)game->volume_pct + delta;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    game->volume_pct = (uint8_t)pct;
    game->volume_level = (uint8_t)(pct == 0 ? 0U : pct <= 60 ? 1U : pct <= 85 ? 2U : 3U);
    publish_volume(game);
    if (game->hooks.volume_pct_changed) {
        game->hooks.volume_pct_changed(game->volume_pct, game->hooks.ctx);
    } else if (game->hooks.volume_changed) {
        game->hooks.volume_changed(game->volume_level, game->hooks.ctx);
    }
}

static void apply_hold_adjust_action(kodou_game_t *game, uint8_t act, int mul)
{
    switch (act) {
    case HOLD_ADJ_STRIKE_DOWN:
        adjust_threshold(game, true, -TOF_THRESHOLD_STEP_MM * mul);
        break;
    case HOLD_ADJ_STRIKE_UP:
        adjust_threshold(game, true, TOF_THRESHOLD_STEP_MM * mul);
        break;
    case HOLD_ADJ_RELEASE_DOWN:
        adjust_threshold(game, false, -TOF_THRESHOLD_STEP_MM * mul);
        break;
    case HOLD_ADJ_RELEASE_UP:
        adjust_threshold(game, false, TOF_THRESHOLD_STEP_MM * mul);
        break;
    case HOLD_ADJ_INTERVAL_DOWN:
        adjust_interval(game, -TOF_INTERVAL_STEP_MS * mul);
        break;
    case HOLD_ADJ_INTERVAL_UP:
        adjust_interval(game, TOF_INTERVAL_STEP_MS * mul);
        break;
    case HOLD_ADJ_VOL_DOWN:
        adjust_volume_pct(game, -10 * mul);
        break;
    case HOLD_ADJ_VOL_UP:
        adjust_volume_pct(game, 10 * mul);
        break;
    default:
        break;
    }
}

static void hide_all_notes(kodou_game_t *game)
{
    memset(game->notes, 0, sizeof(game->notes));
    if (game->started) {
        remember(game, esp_gsp_canvas_invalidate(game->ui, GSP_BIND_NOTE_LANE));
    }
}

void kodou_game_start_song(kodou_game_t *game)
{
    if (!game || !game->ui) return;
    show_view(game, KODOU_VIEW_PLAY);
    hide_all_notes(game);
    atomic_store(&game->pending_hit, 0);
    game->elapsed_ms = 0;
    game->score = 0;
    game->combo = 0;
    game->best_combo = 0;
    game->perfect_count = 0;
    game->good_count = 0;
    game->miss_count = 0;
    game->chart_cursor = 0;
    game->started = true;
    game->completion_shown = false;
    game->start_cue_shown = false;
    game->start_clock_ms = game->hooks.clock_ms
        ? game->hooks.clock_ms(game->hooks.ctx) : 0;
    game->published_score = UINT32_MAX;
    game->published_combo = UINT32_MAX;
    game->published_progress = -1;
    hide_result(game);
    hide_combo_burst(game);
    hide_judge_burst(game);
    show_feedback(game, tr(game, "预备", "Ready"), 30);
    publish_score(game, true);
    remember(game, esp_gsp_set_value(game->ui, GSP_BIND_SONG_PROGRESS, 0));
    if (game->hooks.song_started) {
        game->hooks.song_started(game->song, game->hooks.ctx);
    }
}

void kodou_game_stop_song(kodou_game_t *game)
{
    if (!game || !game->ui) return;
    const bool was_started = game->started;
    game->started = false;
    atomic_store(&game->pending_hit, 0);
    hide_all_notes(game);
    hide_result(game);
    hide_combo_burst(game);
    hide_judge_burst(game);
    show_feedback(game, was_started ? tr(game, "演奏结束", "Ended")
                                   : tr(game, "选择乐曲", "Songs"), 255);
    if (was_started && game->hooks.song_stopped) {
        game->hooks.song_stopped(game->hooks.ctx);
    }
}

static int find_free_slot(const kodou_game_t *game, kodou_hit_t type)
{
    const size_t begin = type == KODOU_HIT_CENTER ? 0 : NOTE_SLOTS_PER_TYPE;
    const size_t end = begin + NOTE_SLOTS_PER_TYPE;
    for (size_t i = begin; i < end; ++i) {
        if (!game->notes[i].active) return (int)i;
    }
    return -1;
}

static void spawn_due_notes(kodou_game_t *game)
{
    const song_desc_t *active = song(game);
    while (game->chart_cursor < active->chart_count &&
           active->chart[game->chart_cursor].time_ms <=
               game->elapsed_ms + NOTE_TRAVEL_MS) {
        const chart_note_t *note = &active->chart[game->chart_cursor];
        int index = find_free_slot(game, (kodou_hit_t)note->type);
        if (index < 0) return;
        const uint8_t slot = note->type == KODOU_HIT_CENTER
            ? (uint8_t)index : (uint8_t)(index - NOTE_SLOTS_PER_TYPE);
        game->notes[index].active = true;
        game->notes[index].chart_index = game->chart_cursor;
        game->notes[index].type = (kodou_hit_t)note->type;
        game->notes[index].slot = slot;
        ++game->chart_cursor;
    }
}

static void retire_note(kodou_game_t *game, size_t index)
{
    if (!game->notes[index].active) return;
    game->notes[index].active = false;
}

static void mark_miss(kodou_game_t *game, size_t index)
{
    retire_note(game, index);
    const uint32_t broken_combo = game->combo;
    game->combo = 0;
    ++game->miss_count;
    const bool combo_broken = broken_combo >= 10U;
    show_judge_burst(game,
                     combo_broken ? JUDGE_BURST_COMBO_BREAK
                                  : JUDGE_BURST_MISS,
                     combo_broken ? 20U : 16U);
    publish_score(game, false);
}

static void update_notes(kodou_game_t *game)
{
    const song_desc_t *active = song(game);
    for (size_t i = 0; i < sizeof(game->notes) / sizeof(game->notes[0]); ++i) {
        if (!game->notes[i].active) continue;
        const chart_note_t *note = &active->chart[game->notes[i].chart_index];
        const int32_t delta = (int32_t)note->time_ms -
                              (int32_t)game->elapsed_ms;
        if (delta < -GOOD_WINDOW_MS) mark_miss(game, i);
    }
}

static void judge_hit(kodou_game_t *game, kodou_hit_t hit, uint8_t strength)
{
    if (!game->started) return;
    const song_desc_t *active = song(game);
    int closest = -1;
    int closest_delta = INT_MAX;
    for (size_t i = 0; i < sizeof(game->notes) / sizeof(game->notes[0]); ++i) {
        if (!game->notes[i].active) continue;
        const chart_note_t *note = &active->chart[game->notes[i].chart_index];
        const int delta = abs((int)note->time_ms - (int)game->elapsed_ms);
        if (delta < closest_delta) {
            closest = (int)i;
            closest_delta = delta;
        }
    }

    kodou_judgement_t judgement = KODOU_JUDGE_MISS;
    if (closest >= 0 && closest_delta <= GOOD_WINDOW_MS) {
        if (game->notes[closest].type == hit) {
            judgement = closest_delta <= PERFECT_WINDOW_MS
                ? KODOU_JUDGE_PERFECT : KODOU_JUDGE_GOOD;
            retire_note(game, (size_t)closest);
            ++game->combo;
            if (game->combo > game->best_combo) game->best_combo = game->combo;
            if (judgement == KODOU_JUDGE_PERFECT) {
                ++game->perfect_count;
            } else {
                ++game->good_count;
            }
            game->score += judgement == KODOU_JUDGE_PERFECT ? 1000U : 500U;
            game->score += game->combo * 8U;
            const bool perfect = judgement == KODOU_JUDGE_PERFECT;
            show_judge_burst(game,
                             perfect ? JUDGE_BURST_PERFECT : JUDGE_BURST_GOOD,
                             perfect ? 14U : 12U);
            celebrate_combo(game);
        } else {
            retire_note(game, (size_t)closest);
            const uint32_t broken_combo = game->combo;
            game->combo = 0;
            ++game->miss_count;
            const bool combo_broken = broken_combo >= 10U;
            show_judge_burst(game,
                             combo_broken ? JUDGE_BURST_COMBO_BREAK
                                          : JUDGE_BURST_MISS,
                             combo_broken ? 20U : 16U);
        }
    } else {
        game->combo = 0;
        show_judge_burst(game, JUDGE_BURST_EMPTY, 12U);
    }

    if (hit == KODOU_HIT_CENTER) {
        game->center_flash_ticks = 4;
        set_visible(game, GSP_BIND_CENTER_FLASH_VISIBLE, true);
    } else {
        game->rim_flash_ticks = 4;
        set_visible(game, GSP_BIND_RIM_FLASH_VISIBLE, true);
    }
    publish_score(game, false);
    if (game->hooks.feedback) {
        game->hooks.feedback(hit, judgement, strength, game->hooks.ctx);
    }
}

#ifndef KODOU_GSP_SIM
static uint8_t hit_settings_adjust_zone(int32_t x, int32_t y)
{
    int side = 0;
    if (x >= 200 && x < 276) side = -1;
    else if (x >= 376 && x < 452) side = 1;
    if (side == 0) return HOLD_ADJ_NONE;
    if (y >= 190 && y < 248) {
        return side < 0 ? HOLD_ADJ_STRIKE_DOWN : HOLD_ADJ_STRIKE_UP;
    }
    if (y >= 258 && y < 316) {
        return side < 0 ? HOLD_ADJ_RELEASE_DOWN : HOLD_ADJ_RELEASE_UP;
    }
    if (y >= 326 && y < 384) {
        return side < 0 ? HOLD_ADJ_INTERVAL_DOWN : HOLD_ADJ_INTERVAL_UP;
    }
    if (y >= 396 && y < 454) {
        return side < 0 ? HOLD_ADJ_VOL_DOWN : HOLD_ADJ_VOL_UP;
    }
    return HOLD_ADJ_NONE;
}

static void pointer_observer(esp_gsp_handle_t ui, int32_t x, int32_t y,
                             bool pressed, void *ctx)
{
    (void)ui;
    kodou_game_t *game = ctx;
    if (!pressed) {
        game->touch_down = false;
        game->hold_adjust_action = HOLD_ADJ_NONE;
        game->hold_adjust_ticks = 0;
        return;
    }
    if (game->view == KODOU_VIEW_SETTINGS) {
        const uint8_t act = hit_settings_adjust_zone(x, y);
        if (!game->touch_down) {
            game->touch_down = true;
            game->hold_adjust_action = act;
            game->hold_adjust_ticks = 0;
        } else if (act != game->hold_adjust_action) {
            game->hold_adjust_action = act;
            game->hold_adjust_ticks = 0;
        }
        return;
    }
    if (game->touch_down) return;
    game->touch_down = true;
    if (game->view != KODOU_VIEW_PLAY) return;
    if (y < 332 || y >= 450) return;
    if (x >= 18 && x < 223) {
        kodou_game_submit_hit(game, KODOU_HIT_CENTER, 80);
    } else if (x >= 257 && x < 462) {
        kodou_game_submit_hit(game, KODOU_HIT_RIM, 80);
    }
}
#endif

static void publish_imu(kodou_game_t *game)
{
    const int state = atomic_load(&game->imu_state);
    const bool state_changed = state != game->published_imu_state;
    if (state_changed) {
        const char *label = tr(game, "IMU 校准中", "Calibrating");
        if (state == KODOU_IMU_READY) {
            static const char *ready_zh[] = {"IMU 低", "IMU 中", "IMU 高"};
            static const char *ready_en[] = {"IMU Low", "IMU Mid", "IMU High"};
            const int level = atomic_load(&game->sensitivity);
            label = game->english ? ready_en[level] : ready_zh[level];
        } else if (state == KODOU_IMU_ERROR) {
            label = tr(game, "触屏模式", "Touch");
        }
        set_text(game, GSP_BIND_IMU_TEXT, label);
        game->published_imu_state = state;
    }
    /* The IMU runs at 100 Hz. This decorative meter only needs five visual
     * updates per second; keeping it off most 40 FPS chart frames avoids an
     * extra damage region while notes are moving. */
    if (!state_changed && ++game->motion_publish_ticks < 8U) return;
    game->motion_publish_ticks = 0U;
    const int raw_level = atomic_load(&game->motion_level);
    const int level = ((raw_level + 5) / 10) * 10;
    if (level != game->published_motion_level) {
        remember(game, esp_gsp_set_value(game->ui, GSP_BIND_MOTION_LEVEL, level));
        game->published_motion_level = level;
    }
}

static void animate_flashes(kodou_game_t *game)
{
    if (game->center_flash_ticks > 0 && --game->center_flash_ticks == 0) {
        set_visible(game, GSP_BIND_CENTER_FLASH_VISIBLE, false);
    }
    if (game->rim_flash_ticks > 0 && --game->rim_flash_ticks == 0) {
        set_visible(game, GSP_BIND_RIM_FLASH_VISIBLE, false);
    }
    if (game->judge_hold_ticks > 0) --game->judge_hold_ticks;
    if (game->judge_burst_ticks > 0) --game->judge_burst_ticks;
    if (game->combo_burst_ticks > 0 && --game->combo_burst_ticks == 0) {
        hide_combo_burst(game);
    }
}

static void autoplay(kodou_game_t *game)
{
    if (!game->demo_autoplay || atomic_load(&game->pending_hit) != 0) return;
    const song_desc_t *active = song(game);
    for (size_t i = 0; i < sizeof(game->notes) / sizeof(game->notes[0]); ++i) {
        if (!game->notes[i].active) continue;
        const chart_note_t *note = &active->chart[game->notes[i].chart_index];
        if (abs((int)note->time_ms - (int)game->elapsed_ms) <=
            PERFECT_WINDOW_MS) {
            kodou_game_submit_hit(game, game->notes[i].type, 82);
            return;
        }
    }
}

static void publish_result(kodou_game_t *game)
{
    const song_desc_t *active = song(game);
    const uint32_t weighted = (uint32_t)game->perfect_count * 100U +
                              (uint32_t)game->good_count * 60U;
    uint32_t accuracy = active->chart_count > 0U
        ? weighted / (uint32_t)active->chart_count : 0U;
    if (accuracy > 100U) accuracy = 100U;
    const char *grade = "D";
    if (accuracy >= 98U && game->miss_count == 0U) grade = "S+";
    else if (accuracy >= 92U) grade = "S";
    else if (accuracy >= 84U) grade = "A";
    else if (accuracy >= 72U) grade = "B";
    else if (accuracy >= 60U) grade = "C";
    char text[48];
    set_text(game, GSP_BIND_RESULT_GRADE_TEXT, grade);
    (void)snprintf(text, sizeof(text),
                   game->english ? "Score %07lu" : "分数 %07lu",
                   (unsigned long)(game->score % 10000000U));
    set_text(game, GSP_BIND_RESULT_SCORE_TEXT, text);
    (void)snprintf(text, sizeof(text),
                   game->english ? "Best %lu" : "最高连击 %lu",
                   (unsigned long)game->best_combo);
    set_text(game, GSP_BIND_RESULT_COMBO_TEXT, text);
    (void)snprintf(text, sizeof(text),
                   game->english ? "Accuracy %lu%%" : "准确率 %lu%%",
                   (unsigned long)accuracy);
    set_text(game, GSP_BIND_RESULT_ACCURACY_TEXT, text);
    (void)snprintf(text, sizeof(text),
                   game->english ? "PERFECT %u" : "完美 %u",
                   (unsigned)game->perfect_count);
    set_text(game, GSP_BIND_RESULT_STATS_PERFECT_TEXT, text);
    (void)snprintf(text, sizeof(text),
                   game->english ? "GOOD %u" : "良好 %u",
                   (unsigned)game->good_count);
    set_text(game, GSP_BIND_RESULT_STATS_GOOD_TEXT, text);
    (void)snprintf(text, sizeof(text),
                   game->english ? "MISS %u" : "失误 %u",
                   (unsigned)game->miss_count);
    set_text(game, GSP_BIND_RESULT_STATS_TEXT, text);
}

static void finish_song(kodou_game_t *game)
{
    hide_all_notes(game);
    hide_judge_burst(game);
    game->started = false;
    game->completion_shown = true;
    remember(game, esp_gsp_set_value(game->ui, GSP_BIND_SONG_PROGRESS, 100));
    game->published_progress = 100;
    show_feedback(game, tr(game, "演奏完成", "Clear"), 255);
    publish_result(game);
    show_view(game, KODOU_VIEW_RESULT);
    if (game->hooks.song_stopped) game->hooks.song_stopped(game->hooks.ctx);
}

static void invalidate_note_changes(kodou_game_t *game)
{
    /* Every note rides the same NOTE_Y band. Repaint the full width of that
     * band instead of the travel span: the render task consumes the damage
     * rectangle a tick or two after this call, so a span that tracks the note
     * can clip the sprite it stamps and leave the previous position behind as
     * a ghost. A full-width band costs two 34-line transfers and cannot clip. */
    bool dirty = false;

    for (size_t i = 0; i < sizeof(game->notes) / sizeof(game->notes[0]); ++i) {
        const bool was_active = game->notes[i].rendered_active;
        const bool is_active = game->notes[i].active;
        const int old_x = game->notes[i].rendered_x;
        const int new_x = is_active ? note_x(game, i) : old_x;

        game->notes[i].rendered_active = is_active;
        game->notes[i].rendered_x = (int16_t)new_x;

        if (was_active || is_active) {
            dirty = true;
        }
    }

    if (!dirty) {
        return;
    }

    const gsp_rect_t rect = {
        .x1 = 0,
        .y1 = NOTE_Y < 0 ? 0 : NOTE_Y,
        .x2 = NOTE_CANVAS_W,
        .y2 = NOTE_Y + NOTE_SIZE,
    };
    const esp_gsp_err_t err = esp_gsp_canvas_invalidate_dirty(
        game->ui, GSP_BIND_NOTE_LANE, rect);
    if (err != ESP_GSP_OK) {
        remember(game, err);
    }
}

#ifdef KODOU_GSP_SIM
static void apply_language(kodou_game_t *game);
static bool s_sim_settings;
static int s_sim_frame;
static int s_sim_preview_step;
static char s_sim_preview_view[32];
static bool s_sim_preview_english;

static void sim_feed_settings(kodou_game_t *game)
{
    if (s_sim_settings) {
        kodou_game_set_tof_online(game, KODOU_HIT_CENTER, true);
        kodou_game_set_tof_online(game, KODOU_HIT_RIM, true);
        kodou_game_set_tof_distance(game, KODOU_HIT_CENTER, 140, true);
        kodou_game_set_tof_distance(game, KODOU_HIT_RIM, 140, true);
    }
}
#endif

static void step_test_sweep(kodou_game_t *game)
{
    uint16_t strike_mm = TOF_STRIKE_DEFAULT_MM;
    uint16_t release_mm = TOF_RELEASE_DEFAULT_MM;
    kodou_game_get_tof_thresholds(game, &strike_mm, &release_mm);
    for (int i = 0; i < TOF_SIDE_COUNT; ++i) {
        if (game->test_sweep_ticks[i] == 0U) continue;
        const uint8_t t = game->test_sweep_ticks[i]--;
        const kodou_hit_t side = i == 0 ? KODOU_HIT_CENTER : KODOU_HIT_RIM;
        uint16_t sim_mm;
        if (t == 32U) {
            /* Ensure armed before striking */
            kodou_game_set_tof_distance(game, side,
                                        (uint16_t)(release_mm + 60U), true);
        }
        if (t >= 24U) {
            sim_mm = strike_mm > 40U ? (uint16_t)(strike_mm - 30U) : 20U;
        } else if (t >= 11U) {
            sim_mm = (uint16_t)((strike_mm + release_mm) / 2U);
        } else {
            sim_mm = (uint16_t)(release_mm + 70U);
        }
        kodou_game_set_tof_distance(game, side, sim_mm, true);
    }
}

static void game_tick(esp_gsp_handle_t ui, void *ctx)
{
    (void)ui;
    kodou_game_t *game = ctx;
#ifdef KODOU_GSP_SIM
    if (s_sim_preview_step == 1) {
        s_sim_preview_step = 2;
        game->english = s_sim_preview_english;
        apply_language(game);
        return;
    } else if (s_sim_preview_step == 2) {
        s_sim_preview_step = 0;
        const char *view = s_sim_preview_view;
        if (view[0] == '\0' || strcmp(view, "start") == 0 || strcmp(view, "home") == 0) {
            s_sim_settings = false;
            kodou_game_set_demo_autoplay(game, false);
            show_view(game, KODOU_VIEW_START);
        } else if (strcmp(view, "guitar") == 0) {
            s_sim_settings = false;
            kodou_game_set_demo_autoplay(game, false);
            show_view(game, KODOU_VIEW_GUITAR);
        } else if (strcmp(view, "play") == 0) {
            s_sim_settings = false;
            kodou_game_set_demo_autoplay(game, false);
            kodou_game_start_song(game);
            game->elapsed_ms = 2350U;
            game->start_cue_shown = true;
            game->score = 24800U;
            game->combo = 24U;
            spawn_due_notes(game);
            /* Retire any notes that already passed the target ring at elapsed_ms=2350 */
            for (size_t i = 0; i < sizeof(game->notes) / sizeof(game->notes[0]); ++i) {
                if (!game->notes[i].active) continue;
                const chart_note_t *n = &song(game)->chart[game->notes[i].chart_index];
                if (n->time_ms < game->elapsed_ms) {
                    game->notes[i].active = false;
                }
            }
            invalidate_note_changes(game);
            publish_score(game, true);
            remember(game, esp_gsp_set_value(game->ui, GSP_BIND_SONG_PROGRESS, 62));
            show_feedback(game, tr(game, "完美", "Perfect"), 250U);
        } else if (strcmp(view, "settings") == 0) {
            s_sim_settings = true;
            s_sim_frame = 0;
            game->single_tof_mode = false;
            kodou_game_set_demo_autoplay(game, false);
            show_view(game, KODOU_VIEW_SETTINGS);
        } else if (strcmp(view, "settings_single") == 0) {
            s_sim_settings = false;
            game->single_tof_mode = true;
            kodou_game_set_tof_online(game, KODOU_HIT_CENTER, true);
            kodou_game_set_tof_distance(game, KODOU_HIT_CENTER, 140, true);
            kodou_game_set_tof_distance(game, KODOU_HIT_RIM, 0, false);
            kodou_game_set_demo_autoplay(game, false);
            show_view(game, KODOU_VIEW_SETTINGS);
        } else if (strcmp(view, "select") == 0) {
            s_sim_settings = false;
            kodou_game_set_demo_autoplay(game, false);
            show_view(game, KODOU_VIEW_SELECT);
        } else if (strcmp(view, "result") == 0) {
            s_sim_settings = false;
            kodou_game_set_demo_autoplay(game, false);
            game->score = 98400;
            game->best_combo = 48;
            game->perfect_count = 43;
            game->good_count = 3;
            game->miss_count = 0;
            finish_song(game);
        }
        return;
    }
#endif
    publish_imu(game);
    animate_flashes(game);
    if (game->view == KODOU_VIEW_SETTINGS) {
#ifdef KODOU_GSP_SIM
        sim_feed_settings(game);
#else
        if (game->touch_down && game->hold_adjust_action != HOLD_ADJ_NONE) {
            if (game->hold_adjust_ticks < 200U) {
                game->hold_adjust_ticks++;
            }
            if (game->hold_adjust_ticks >= 12U &&
                ((game->hold_adjust_ticks - 12U) % 3U) == 0U) {
                const int mul = game->hold_adjust_ticks >= 48U ? 5
                              : game->hold_adjust_ticks >= 24U ? 2 : 1;
                apply_hold_adjust_action(game, game->hold_adjust_action, mul);
            }
        }
#endif
        step_test_sweep(game);
        publish_tof(game);
    }
    if (!game->started) return;
#ifdef KODOU_GSP_SIM
    if (strcmp(s_sim_preview_view, "play") == 0) {
        remember(game, esp_gsp_canvas_invalidate(game->ui, GSP_BIND_NOTE_LANE));
        return;
    }
#endif

    game->elapsed_ms = game->hooks.clock_ms
        ? game->hooks.clock_ms(game->hooks.ctx) - game->start_clock_ms
        : game->elapsed_ms + GAME_TICK_MS;
    const song_desc_t *active = song(game);
    if (game->elapsed_ms >= active->duration_ms) {
        finish_song(game);
        return;
    }

    spawn_due_notes(game);
    update_notes(game);
    autoplay(game);
    const int hit = atomic_exchange(&game->pending_hit, 0);
    const int strength = atomic_exchange(&game->pending_strength, 0);
    if (hit == KODOU_HIT_CENTER || hit == KODOU_HIT_RIM) {
        judge_hit(game, (kodou_hit_t)hit,
                  (uint8_t)(strength > 0 ? strength : 72));
    }

    const int progress = (int)((game->elapsed_ms * 100U) /
                               active->duration_ms);
    if (progress != game->published_progress) {
        remember(game, esp_gsp_set_value(
            game->ui, GSP_BIND_SONG_PROGRESS, progress));
        game->published_progress = progress;
    }
    invalidate_note_changes(game);
    if (!game->start_cue_shown && game->elapsed_ms >= 1450U) {
        show_feedback(game, tr(game, "开始", "Start"), 12);
        game->start_cue_shown = true;
    }
}

static void apply_language(kodou_game_t *game)
{
    const bool en = game->english;
    set_text(game, GSP_BIND_HOME_TITLE_TEXT, en ? "Instruments" : "乐器工坊");
    set_text(game, GSP_BIND_START_PLAY_TEXT, en ? "Taiko" : "太鼓");
    set_text(game, GSP_BIND_START_GUITAR_TEXT, en ? "Guitar" : "吉他");
    set_text(game, GSP_BIND_START_SETTINGS_TEXT, en ? "Settings" : "设置");
    set_text(game, GSP_BIND_GUITAR_BACK_TEXT, en ? "Back" : "返回");
    set_text(game, GSP_BIND_LANG_BUTTON_TEXT, en ? "中文" : "EN");
    set_text(game, GSP_BIND_SETTINGS_BACK_TEXT, en ? "Back" : "返回");
    set_text(game, GSP_BIND_SETTINGS_TITLE_TEXT, en ? "Settings" : "设置");
    set_text(game, GSP_BIND_TEST_CENTER_NAME_TEXT, en ? "Center" : "鼓心");
    set_text(game, GSP_BIND_TEST_RIM_NAME_TEXT, en ? "Rim" : "鼓边");
    set_text(game, GSP_BIND_STRIKE_CAPTION_TEXT, en ? "Strike" : "触发阈值");
    set_text(game, GSP_BIND_RELEASE_CAPTION_TEXT, en ? "Release" : "松开阈值");
    set_text(game, GSP_BIND_INTERVAL_CAPTION_TEXT, en ? "Interval" : "触发间隔");
    set_text(game, GSP_BIND_VOL_CAPTION_TEXT, en ? "Volume" : "音量控制");
    set_text(game, GSP_BIND_HINT_LINE1_TEXT, " ");
    set_text(game, GSP_BIND_HINT_LINE2_TEXT, " ");
    set_text(game, GSP_BIND_SELECT_BACK_TEXT, en ? "Back" : "返回");
    set_text(game, GSP_BIND_SELECT_TITLE_TEXT, en ? "Select Song" : "选择乐曲");
    set_text(game, GSP_BIND_SELECT_START_TEXT, en ? "Start Game" : "开始演奏");
    set_text(game, GSP_BIND_PLAY_BACK_TEXT, en ? "Back" : "返回");
    set_text(game, GSP_BIND_RESULT_TITLE_TEXT, en ? "Result" : "演奏结算");
    set_text(game, GSP_BIND_RESULT_BACK_TEXT, en ? "Back to Songs" : "返回选曲");
    set_text(game, GSP_BIND_PERFECT_LABEL_TEXT, en ? "Perfect" : "完美");
    set_text(game, GSP_BIND_GOOD_LABEL_TEXT, en ? "Good" : "良好");
    set_text(game, GSP_BIND_COMBO_CAPTION_TEXT, en ? "Combo" : "连击");
    game->shown_phase[0] = -1;
    game->shown_phase[1] = -1;
    game->shown_left_valid = -1;
    game->shown_right_valid = -1;
    game->published_imu_state = -1;
    publish_song(game);
    publish_volume(game);
    publish_selection(game);
    publish_score(game, true);
    publish_imu(game);
    if (game->completion_shown) publish_result(game);
    if (game->view == KODOU_VIEW_SETTINGS) publish_tof(game);
}

static void ui_event(esp_gsp_handle_t ui, const esp_gsp_event_t *event,
                     void *ctx)
{
    (void)ui;
    kodou_game_t *game = ctx;
    if (!event || event->type != ESP_GSP_EVENT_CALL) return;
    switch (event->action_id) {
    case GSP_ACT_ID_SENSITIVITY: {
        const int next = (atomic_load(&game->sensitivity) + 1) % 3;
        atomic_store(&game->sensitivity, next);
        game->published_imu_state = -1;
        if (game->hooks.sensitivity_changed) {
            game->hooks.sensitivity_changed(next, game->hooks.ctx);
        }
        break;
    }
    case GSP_ACT_ID_VOLUME:
        adjust_volume_pct(game, game->volume_pct >= 100U ? -100 : 20);
        break;
    case GSP_ACT_ID_VOL_DOWN:
        game->hold_adjust_action = HOLD_ADJ_VOL_DOWN;
        game->hold_adjust_ticks = 0;
        adjust_volume_pct(game, -10);
        break;
    case GSP_ACT_ID_VOL_UP:
        game->hold_adjust_action = HOLD_ADJ_VOL_UP;
        game->hold_adjust_ticks = 0;
        adjust_volume_pct(game, 10);
        break;
    case GSP_ACT_ID_OPEN_GUITAR:
        if (!game->started) show_view(game, KODOU_VIEW_GUITAR);
        break;
    case GSP_ACT_ID_PICK_CHORD_0:
    case GSP_ACT_ID_PICK_CHORD_1:
    case GSP_ACT_ID_PICK_CHORD_2:
    case GSP_ACT_ID_PICK_CHORD_3:
    case GSP_ACT_ID_PICK_CHORD_4:
    case GSP_ACT_ID_PICK_CHORD_5: {
        const uint8_t next_chord =
            event->action_id == GSP_ACT_ID_PICK_CHORD_1 ? 1U :
            event->action_id == GSP_ACT_ID_PICK_CHORD_2 ? 2U :
            event->action_id == GSP_ACT_ID_PICK_CHORD_3 ? 3U :
            event->action_id == GSP_ACT_ID_PICK_CHORD_4 ? 4U :
            event->action_id == GSP_ACT_ID_PICK_CHORD_5 ? 5U : 0U;
        game->guitar_chord = next_chord;
        publish_guitar_chord(game);
        kodou_game_submit_guitar_strum(game, 1);
        break;
    }
    case GSP_ACT_ID_GUITAR_STRUM: {
        const int dir = game->guitar_last_dir >= 0 ? -1 : 1;
        kodou_game_submit_guitar_strum(game, dir);
        break;
    }
    case GSP_ACT_ID_SONG_SELECT:
        if (!game->started) {
            hide_result(game);
            hide_combo_burst(game);
            game->song = (kodou_song_t)((game->song + 1) % KODOU_SONG_COUNT);
            game->score = 0;
            game->combo = 0;
            game->published_score = UINT32_MAX;
            game->published_combo = UINT32_MAX;
            publish_score(game, true);
            publish_song(game);
            publish_selection(game);
            remember(game, esp_gsp_set_value(
                game->ui, GSP_BIND_SONG_PROGRESS, 0));
            show_view(game, KODOU_VIEW_SELECT);
        }
        break;
    case GSP_ACT_ID_OPEN_SELECT:
        if (!game->started) show_view(game, KODOU_VIEW_SELECT);
        break;
    case GSP_ACT_ID_OPEN_SETTINGS:
        if (!game->started) show_view(game, KODOU_VIEW_SETTINGS);
        break;
    case GSP_ACT_ID_OPEN_START:
        if (!game->started) show_view(game, KODOU_VIEW_START);
        break;
    case GSP_ACT_ID_PICK_SHANHAI:
    case GSP_ACT_ID_PICK_ZHUQUE:
    case GSP_ACT_ID_PICK_POZHEN:
        if (!game->started) {
            game->song = event->action_id == GSP_ACT_ID_PICK_ZHUQUE
                ? KODOU_SONG_ZHUQUE
                : event->action_id == GSP_ACT_ID_PICK_POZHEN
                    ? KODOU_SONG_POZHEN : KODOU_SONG_SHANHAI;
            publish_song(game);
            publish_selection(game);
        }
        break;
    case GSP_ACT_ID_STRIKE_DOWN:
        game->hold_adjust_action = HOLD_ADJ_STRIKE_DOWN;
        game->hold_adjust_ticks = 0;
        adjust_threshold(game, true, -TOF_THRESHOLD_STEP_MM);
        break;
    case GSP_ACT_ID_STRIKE_UP:
        game->hold_adjust_action = HOLD_ADJ_STRIKE_UP;
        game->hold_adjust_ticks = 0;
        adjust_threshold(game, true, TOF_THRESHOLD_STEP_MM);
        break;
    case GSP_ACT_ID_RELEASE_DOWN:
        game->hold_adjust_action = HOLD_ADJ_RELEASE_DOWN;
        game->hold_adjust_ticks = 0;
        adjust_threshold(game, false, -TOF_THRESHOLD_STEP_MM);
        break;
    case GSP_ACT_ID_RELEASE_UP:
        game->hold_adjust_action = HOLD_ADJ_RELEASE_UP;
        game->hold_adjust_ticks = 0;
        adjust_threshold(game, false, TOF_THRESHOLD_STEP_MM);
        break;
    case GSP_ACT_ID_INTERVAL_DOWN:
        game->hold_adjust_action = HOLD_ADJ_INTERVAL_DOWN;
        game->hold_adjust_ticks = 0;
        adjust_interval(game, -TOF_INTERVAL_STEP_MS);
        break;
    case GSP_ACT_ID_INTERVAL_UP:
        game->hold_adjust_action = HOLD_ADJ_INTERVAL_UP;
        game->hold_adjust_ticks = 0;
        adjust_interval(game, TOF_INTERVAL_STEP_MS);
        break;
    case GSP_ACT_ID_TEST_CENTER:
        game->test_sweep_ticks[0] = 32U;
        step_test_sweep(game);
        publish_tof(game);
        break;
    case GSP_ACT_ID_TEST_RIM:
        game->test_sweep_ticks[1] = 32U;
        step_test_sweep(game);
        publish_tof(game);
        break;
    case GSP_ACT_ID_TOGGLE_LANGUAGE:
        game->english = !game->english;
        apply_language(game);
        break;
    case GSP_ACT_ID_GAME_START:
        show_view(game, KODOU_VIEW_PLAY);
        kodou_game_start_song(game);
        break;
    case GSP_ACT_ID_GAME_STOP:
    case GSP_ACT_ID_RESULT_BACK:
        kodou_game_stop_song(game);
        show_view(game, KODOU_VIEW_SELECT);
        break;
    default:
        break;
    }
}

esp_gsp_err_t kodou_game_init(esp_gsp_handle_t ui, kodou_game_t *game,
                              const kodou_game_hooks_t *hooks)
{
    if (!ui || !game) return ESP_GSP_ERR_INVALID_ARG;
    memset(game, 0, sizeof(*game));
    game->ui = ui;
    game->song = KODOU_SONG_SHANHAI;
    game->volume_level = 3U;
    game->volume_pct = 80U;
    game->guitar_chord = 0U;
    game->guitar_last_dir = 1;
    game->single_tof_mode = false;
    game->shown_volume_pct = -1;
    atomic_init(&game->sensitivity, 1);
    game->published_imu_state = -1;
    game->published_motion_level = -1;
    game->published_score = UINT32_MAX;
    game->published_combo = UINT32_MAX;
    if (hooks) game->hooks = *hooks;
    atomic_init(&game->pending_hit, 0);
    atomic_init(&game->pending_strength, 0);
    atomic_init(&game->imu_state, KODOU_IMU_CALIBRATING);
    atomic_init(&game->motion_level, 0);
    atomic_init(&game->tof_strike_mm, TOF_STRIKE_DEFAULT_MM);
    atomic_init(&game->tof_release_mm, TOF_RELEASE_DEFAULT_MM);
    atomic_init(&game->tof_interval_ms, TOF_INTERVAL_DEFAULT_MS);
    atomic_init(&game->tof_left_mm, 0);
    atomic_init(&game->tof_right_mm, 0);
    atomic_init(&game->tof_left_valid, 0);
    atomic_init(&game->tof_right_valid, 0);
    atomic_init(&game->tof_left_online, 0);
    atomic_init(&game->tof_right_online, 0);
    game->shown_left_mm = -1;
    game->shown_right_mm = -1;
    game->shown_left_valid = -1;
    game->shown_right_valid = -1;
    game->shown_single_flash = -1;
    game->shown_strike_mm = -1;
    game->shown_release_mm = -1;
    game->shown_interval_ms = -1;
    prepare_lane_cache();
    esp_gsp_err_t err = esp_gsp_canvas_set_draw_cb(
        ui, GSP_BIND_NOTE_LANE, note_lane_draw, game);
    if (err != ESP_GSP_OK) return err;
    err = esp_gsp_on_event(ui, ui_event, game);
    if (err != ESP_GSP_OK) return err;
    err = esp_gsp_set_press_feedback_enabled(ui, false);
    if (err != ESP_GSP_OK) return err;
#ifndef KODOU_GSP_SIM
    /* The device runtime exposes raw pointer samples, so drum hits can be
     * queued on touch-down instead of waiting for a button release.  The
     * 1.2.0 PC bridge does not export this optional observer yet. */
    err = esp_gsp_set_pointer_observer(ui, pointer_observer, game);
    if (err != ESP_GSP_OK) return err;
#endif
    game->timer = esp_gsp_timer_create(ui, GAME_TICK_MS, game_tick, game);
    if (!game->timer) return ESP_GSP_ERR_NO_MEM;
    hide_all_notes(game);
    for (int side = 0; side < TOF_SIDE_COUNT; ++side) {
        atomic_init(&game->tof_phase[side], TOF_PHASE_ABSENT);
        atomic_init(&game->tof_hit_gen[side], 0);
        atomic_init(&game->tof_hit_strength[side], 0);
        game->shown_phase[side] = -1;
    }
#ifdef KODOU_GSP_SIM
    s_sim_preview_step = 1;
#else
    hide_result(game);
    hide_combo_burst(game);
    hide_judge_burst(game);
    apply_language(game);
    show_view(game, KODOU_VIEW_START);
#endif
    return ESP_GSP_OK;
}

void kodou_game_deinit(kodou_game_t *game)
{
    if (!game || !game->ui) return;
    if (game->timer) {
        (void)esp_gsp_timer_delete(game->ui, game->timer);
        game->timer = NULL;
    }
#ifndef KODOU_GSP_SIM
    (void)esp_gsp_set_pointer_observer(game->ui, NULL, NULL);
#endif
    (void)esp_gsp_canvas_stop(game->ui, GSP_BIND_NOTE_LANE);
}

void kodou_game_submit_hit(kodou_game_t *game, kodou_hit_t hit,
                           uint8_t strength)
{
    if (!game || !game->started ||
        (hit != KODOU_HIT_CENTER && hit != KODOU_HIT_RIM)) return;
    atomic_store(&game->pending_strength, strength);
    int expected = 0;
    (void)atomic_compare_exchange_strong(&game->pending_hit, &expected, hit);
}

void kodou_game_set_imu_state(kodou_game_t *game, kodou_imu_state_t state)
{
    if (game) atomic_store(&game->imu_state, state);
}

void kodou_game_set_motion_level(kodou_game_t *game, int level)
{
    if (!game) return;
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    atomic_store(&game->motion_level, level);
}

void kodou_game_set_demo_autoplay(kodou_game_t *game, bool enabled)
{
    if (game) game->demo_autoplay = enabled;
}

static int tof_index(kodou_hit_t side)
{
    if (side == KODOU_HIT_CENTER) return 0;
    if (side == KODOU_HIT_RIM) return 1;
    return -1;
}

static uint8_t tof_strength(uint16_t distance_mm, uint16_t strike_mm)
{
    if (strike_mm <= 50U || distance_mm <= 50U) return TOF_MAX_STRENGTH;
    const uint16_t span = (uint16_t)(strike_mm - 50U);
    const uint16_t closeness = (uint16_t)(strike_mm - distance_mm);
    return (uint8_t)(TOF_MIN_STRENGTH +
                     ((TOF_MAX_STRENGTH - TOF_MIN_STRENGTH) * closeness) / span);
}

void kodou_game_set_tof_distance(kodou_game_t *game, kodou_hit_t side,
                                 uint16_t distance_mm, bool valid)
{
    if (!game) return;
    const int index = tof_index(side);
    if (index < 0) return;
    if (index == 0) {
        /* An invalid sample carries no distance: the driver fills the field
         * with whatever the last range status left behind. Keep the last
         * trustworthy reading so the readout holds steady, and let the
         * validity flag alone decide whether a hit may fire. */
        if (valid) {
            atomic_store(&game->tof_left_mm, distance_mm);
        }
        atomic_store(&game->tof_left_valid, valid ? 1 : 0);
    } else {
        if (valid) {
            atomic_store(&game->tof_right_mm, distance_mm);
        }
        atomic_store(&game->tof_right_valid, valid ? 1 : 0);
    }

    uint16_t strike_mm = TOF_STRIKE_DEFAULT_MM;
    uint16_t release_mm = TOF_RELEASE_DEFAULT_MM;
    kodou_game_get_tof_thresholds(game, &strike_mm, &release_mm);
    const uint16_t interval_ms = kodou_game_get_tof_interval_ms(game);
    int phase = TOF_PHASE_ABSENT;
    if (!valid) {
        game->tof_armed[index] = true;
    } else if (distance_mm >= release_mm) {
        game->tof_armed[index] = true;
        phase = TOF_PHASE_READY;
    } else if (!game->tof_armed[index] || distance_mm > strike_mm) {
        phase = game->tof_armed[index] ? TOF_PHASE_APPROACH : TOF_PHASE_HOLD;
    } else {
        const uint32_t now = game->hooks.clock_ms
            ? game->hooks.clock_ms(game->hooks.ctx) : 0U;
        if (game->tof_has_hit[index] &&
            (uint32_t)(now - game->tof_last_hit_ms[index]) < interval_ms) {
            phase = TOF_PHASE_INTERVAL;
        } else {
            const uint8_t strength = tof_strength(distance_mm, strike_mm);
            game->tof_armed[index] = false;
            game->tof_has_hit[index] = true;
            game->tof_last_hit_ms[index] = now;
            phase = TOF_PHASE_HIT;
            atomic_store(&game->tof_hit_strength[index], strength);
            atomic_fetch_add(&game->tof_hit_gen[index], 1);
            if (game->started) kodou_game_submit_hit(game, side, strength);
        }
    }
    atomic_store(&game->tof_phase[index], phase);
}

void kodou_game_set_tof_online(kodou_game_t *game, kodou_hit_t side,
                               bool online)
{
    if (!game) return;
    const int index = tof_index(side);
    if (index < 0) return;
    if (index == 0) {
        atomic_store(&game->tof_left_online, online ? 1 : 0);
        if (!online) atomic_store(&game->tof_left_valid, 0);
    } else {
        atomic_store(&game->tof_right_online, online ? 1 : 0);
        if (!online) atomic_store(&game->tof_right_valid, 0);
    }
}

void kodou_game_get_tof_thresholds(const kodou_game_t *game,
                                   uint16_t *strike_mm, uint16_t *release_mm)
{
    const int strike = game ? atomic_load(&game->tof_strike_mm)
                            : TOF_STRIKE_DEFAULT_MM;
    const int release = game ? atomic_load(&game->tof_release_mm)
                             : TOF_RELEASE_DEFAULT_MM;
    if (strike_mm) *strike_mm = (uint16_t)strike;
    if (release_mm) *release_mm = (uint16_t)release;
}

uint16_t kodou_game_get_tof_interval_ms(const kodou_game_t *game)
{
    const int interval = game ? atomic_load(&game->tof_interval_ms)
                              : TOF_INTERVAL_DEFAULT_MS;
    return (uint16_t)interval;
}

int kodou_game_get_sensitivity(const kodou_game_t *game)
{
    return game ? atomic_load(&game->sensitivity) : 1;
}

void kodou_game_submit_guitar_strum(kodou_game_t *game, int direction)
{
    if (!game) return;
    const int dir = direction >= 0 ? 1 : -1;
    game->guitar_last_dir = (int8_t)dir;
    if (game->hooks.guitar_strum) {
        game->hooks.guitar_strum((int)game->guitar_chord, dir, game->hooks.ctx);
    }
}

int kodou_game_get_guitar_chord(const kodou_game_t *game)
{
    return game ? (int)game->guitar_chord : 0;
}

#ifdef KODOU_GSP_SIM
void kodou_game_sim_preview_settings(kodou_game_t *game, bool english)
{
    if (!game) return;
    s_sim_settings = true;
    s_sim_frame = 0;
    game->english = english;
    apply_language(game);
    show_view(game, KODOU_VIEW_SETTINGS);
}

void kodou_game_sim_preview_view(kodou_game_t *game, const char *view,
                                 bool english)
{
    if (!game) return;
    s_sim_preview_english = english;
    if (view) {
        strncpy(s_sim_preview_view, view, sizeof(s_sim_preview_view) - 1);
        s_sim_preview_view[sizeof(s_sim_preview_view) - 1] = '\0';
    } else {
        s_sim_preview_view[0] = '\0';
    }
    s_sim_preview_step = 1;
}
#endif
