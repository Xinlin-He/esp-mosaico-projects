// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#include "esp_gsp.h"

typedef enum {
    KODOU_HIT_CENTER = 1,
    KODOU_HIT_RIM = 2,
} kodou_hit_t;

typedef enum {
    KODOU_SONG_SHANHAI = 0,
    KODOU_SONG_ZHUQUE,
    KODOU_SONG_POZHEN,
    KODOU_SONG_COUNT,
} kodou_song_t;

typedef enum {
    KODOU_JUDGE_MISS = 0,
    KODOU_JUDGE_GOOD,
    KODOU_JUDGE_PERFECT,
} kodou_judgement_t;

typedef enum {
    KODOU_IMU_CALIBRATING = 0,
    KODOU_IMU_READY,
    KODOU_IMU_ERROR,
} kodou_imu_state_t;

typedef struct {
    void (*feedback)(kodou_hit_t hit, kodou_judgement_t judgement,
                     uint8_t strength, void *ctx);
    void (*sensitivity_changed)(int level, void *ctx);
    void (*volume_changed)(int level, void *ctx);
    void (*volume_pct_changed)(uint8_t volume_pct, void *ctx);
    void (*guitar_strum)(int chord, int direction, void *ctx);
    void (*song_started)(kodou_song_t song, void *ctx);
    void (*song_stopped)(void *ctx);
    uint32_t (*clock_ms)(void *ctx);
    void *ctx;
} kodou_game_hooks_t;

typedef struct {
    esp_gsp_handle_t ui;
    void *timer;
    kodou_game_hooks_t hooks;
    atomic_int pending_hit;
    atomic_int pending_strength;
    atomic_int imu_state;
    atomic_int motion_level;
    uint32_t elapsed_ms;
    uint32_t start_clock_ms;
    uint32_t score;
    uint32_t combo;
    uint32_t best_combo;
    uint16_t perfect_count;
    uint16_t good_count;
    uint16_t miss_count;
    uint16_t chart_cursor;
    kodou_song_t song;
    atomic_int sensitivity;
    uint8_t volume_level;
    uint8_t volume_pct;
    uint8_t guitar_chord;
    int8_t guitar_last_dir;
    bool single_tof_mode;
    int shown_volume_pct;
    uint8_t center_flash_ticks;
    uint8_t rim_flash_ticks;
    uint8_t judge_hold_ticks;
    uint8_t judge_burst_ticks;
    uint8_t judge_burst_kind;
    uint8_t combo_burst_ticks;
    uint8_t motion_publish_ticks;
    bool touch_down;
    bool demo_autoplay;
    bool started;
    bool completion_shown;
    bool start_cue_shown;
    int published_imu_state;
    int published_motion_level;
    uint32_t published_score;
    uint32_t published_combo;
    int published_progress;
    int last_error;
    int view;
    atomic_int tof_strike_mm;
    atomic_int tof_release_mm;
    atomic_int tof_interval_ms;
    atomic_int tof_left_mm;
    atomic_int tof_right_mm;
    atomic_int tof_left_valid;
    atomic_int tof_right_valid;
    atomic_int tof_left_online;
    atomic_int tof_right_online;
    int shown_left_mm;
    int shown_right_mm;
    int shown_left_valid;
    int shown_right_valid;
    int shown_single_flash;
    int shown_strike_mm;
    int shown_release_mm;
    int shown_interval_ms;
    bool tof_armed[2];
    bool tof_has_hit[2];
    uint32_t tof_last_hit_ms[2];
    atomic_int tof_phase[2];
    atomic_int tof_hit_gen[2];
    atomic_int tof_hit_strength[2];
    int shown_phase[2];
    int shown_hit_gen[2];
    uint8_t test_flash_ticks[2];
    uint8_t test_sweep_ticks[2];
    bool test_flash_on[2];
    uint8_t hold_adjust_action;
    uint8_t hold_adjust_ticks;
    bool english;
    struct {
        bool active;
        uint16_t chart_index;
        kodou_hit_t type;
        uint8_t slot;
        bool rendered_active;
        int16_t rendered_x;
    } notes[10];
} kodou_game_t;

esp_gsp_err_t kodou_game_init(esp_gsp_handle_t ui, kodou_game_t *game,
                              const kodou_game_hooks_t *hooks);
void kodou_game_deinit(kodou_game_t *game);
void kodou_game_submit_hit(kodou_game_t *game, kodou_hit_t hit,
                           uint8_t strength);
void kodou_game_set_imu_state(kodou_game_t *game, kodou_imu_state_t state);
void kodou_game_set_motion_level(kodou_game_t *game, int level);
void kodou_game_set_demo_autoplay(kodou_game_t *game, bool enabled);
void kodou_game_start_song(kodou_game_t *game);
void kodou_game_stop_song(kodou_game_t *game);
int kodou_game_get_sensitivity(const kodou_game_t *game);
void kodou_game_set_tof_distance(kodou_game_t *game, kodou_hit_t side,
                                 uint16_t distance_mm, bool valid);
/* Slot presence is independent of the current ranging result: a module that
 * cannot report a distance yet is still plugged in. */
void kodou_game_set_tof_online(kodou_game_t *game, kodou_hit_t side,
                               bool online);
void kodou_game_get_tof_thresholds(const kodou_game_t *game,
                                   uint16_t *strike_mm, uint16_t *release_mm);
uint16_t kodou_game_get_tof_interval_ms(const kodou_game_t *game);
void kodou_game_submit_guitar_strum(kodou_game_t *game, int direction);
int kodou_game_get_guitar_chord(const kodou_game_t *game);
#ifdef KODOU_GSP_SIM
void kodou_game_sim_preview_settings(kodou_game_t *game, bool english);
void kodou_game_sim_preview_view(kodou_game_t *game, const char *view,
                                 bool english);
#endif
