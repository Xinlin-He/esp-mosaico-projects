// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "game_ui.h"

/* Starts the unified ToF sensor task supporting single or dual VL53L0X modules:
 * - Feeds live left (center) and right (rim) ToF readings to kodou_game_t
 * - Detects guitar strum gestures (down/up direction) based on the active window
 */
esp_err_t instrument_tof_start(kodou_game_t *game);

/* Dequeues a pending guitar strum direction (+1 = down-stroke, -1 = up-stroke). */
bool instrument_tof_take_strum(int *direction);

/* Copies the latest human-readable ToF status string (in Chinese or English). */
void instrument_tof_copy_status(char *out, size_t out_len, bool english);

/* Compatibility aliases */
static inline esp_err_t drum_tof_start(kodou_game_t *game)
{
    return instrument_tof_start(game);
}
static inline bool guitar_tof_take_strum(int *direction)
{
    return instrument_tof_take_strum(direction);
}
static inline void guitar_tof_copy_status(char *out, size_t out_len)
{
    instrument_tof_copy_status(out, out_len, false);
}
