// SPDX-License-Identifier: Apache-2.0

#include "guitar_tune.h"

#define DISTANCE_STEP_MM 10
#define GAP_STEP_MS      10
#define VOLUME_STEP_PCT  10

static guitar_tune_t s_tune = {
    .near_mm = 40,
    .far_mm = 220,
    .gap_ms = 120,
    .volume_pct = 80,
};

guitar_tune_t guitar_tune_defaults(void)
{
    return (guitar_tune_t){
        .near_mm = 40,
        .far_mm = 220,
        .gap_ms = 120,
        .volume_pct = 80,
    };
}

void guitar_tune_sanitize(guitar_tune_t *tune)
{
    if (!tune) {
        return;
    }
    if (tune->near_mm < 0) {
        tune->near_mm = 0;
    }
    if (tune->far_mm < 0) {
        tune->far_mm = 0;
    }
    if (tune->gap_ms < 0) {
        tune->gap_ms = 0;
    }
    if (tune->volume_pct < 0) {
        tune->volume_pct = 0;
    }
    if (tune->volume_pct > 100) {
        tune->volume_pct = 100;
    }
}

void guitar_tune_set(const guitar_tune_t *tune)
{
    if (!tune) {
        return;
    }
    s_tune = *tune;
    guitar_tune_sanitize(&s_tune);
}

guitar_tune_t guitar_tune_get(void)
{
    return s_tune;
}

void guitar_tune_step(int which, int delta)
{
    guitar_tune_t next = s_tune;
    if (which == GUITAR_TUNE_NEAR) {
        next.near_mm += delta * DISTANCE_STEP_MM;
    } else if (which == GUITAR_TUNE_FAR) {
        next.far_mm += delta * DISTANCE_STEP_MM;
    } else if (which == GUITAR_TUNE_GAP) {
        next.gap_ms += delta * GAP_STEP_MS;
    } else if (which == GUITAR_TUNE_VOLUME) {
        next.volume_pct += delta * VOLUME_STEP_PCT;
    }
    guitar_tune_sanitize(&next);
    s_tune = next;
}
