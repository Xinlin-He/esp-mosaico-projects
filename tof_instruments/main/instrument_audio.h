// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "game_ui.h"

/* Starts the unified speaker & mixer task (both Taiko drums/backing and 6-string Guitar PCM). */
esp_err_t instrument_audio_start(void);

/* Drum backing & hit controls */
esp_err_t instrument_audio_start_song(kodou_song_t song);
esp_err_t instrument_audio_stop_song(void);
esp_err_t instrument_audio_drum_hit(kodou_hit_t hit, kodou_judgement_t judgement,
                                    uint8_t strength);

/* Guitar 6-string strum control (direction > 0: down-stroke, < 0: up-stroke) */
void instrument_audio_guitar_strum(int chord, int direction);

/* Global volume control:
 * - instrument_audio_set_volume_pct(0..100) sets exact percentage
 * - instrument_audio_set_volume_level(0..3) maps 0/1/2/3 -> 0%/60%/80%/100%
 */
esp_err_t instrument_audio_set_volume_pct(uint8_t volume_pct);
esp_err_t instrument_audio_set_volume_level(uint8_t level);

/* Compatibility aliases for drum_audio / guitar_audio */
static inline esp_err_t kodou_audio_start(void)
{
    return instrument_audio_start();
}
static inline esp_err_t kodou_audio_start_song(kodou_song_t song)
{
    return instrument_audio_start_song(song);
}
static inline esp_err_t kodou_audio_stop_song(void)
{
    return instrument_audio_stop_song();
}
static inline esp_err_t kodou_audio_set_volume(uint8_t level)
{
    return instrument_audio_set_volume_level(level);
}
static inline esp_err_t kodou_audio_hit(kodou_hit_t hit, kodou_judgement_t judgement,
                                        uint8_t strength)
{
    return instrument_audio_drum_hit(hit, judgement, strength);
}
static inline esp_err_t guitar_audio_start(void)
{
    return instrument_audio_start();
}
static inline void guitar_audio_strum(int chord, int direction)
{
    instrument_audio_guitar_strum(chord, direction);
}
