// SPDX-License-Identifier: Apache-2.0
#pragma once

/* Live strum window and global volume. All thresholds are unconstrained above 0. */
typedef struct guitar_tune {
    int near_mm;
    int far_mm;
    int gap_ms;
    int volume_pct;
} guitar_tune_t;

#define GUITAR_TUNE_NEAR   0
#define GUITAR_TUNE_FAR    1
#define GUITAR_TUNE_GAP    2
#define GUITAR_TUNE_VOLUME 3

guitar_tune_t guitar_tune_defaults(void);
void guitar_tune_sanitize(guitar_tune_t *tune);
void guitar_tune_set(const guitar_tune_t *tune);
guitar_tune_t guitar_tune_get(void);
void guitar_tune_step(int which, int delta);
