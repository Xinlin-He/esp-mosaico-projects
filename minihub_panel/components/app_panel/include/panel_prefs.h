// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

/*
 * First-run progress stored beside the language choice.
 * setup_done == 1 is the historical "finished" value and stays home.
 * step 2 means language is confirmed and Wi-Fi is next.
 * step 3 means Wi-Fi was accepted and mode is next.
 */
#define PANEL_PREFS_SETUP_DONE 1U
#define PANEL_PREFS_STEP_LANGUAGE 2U
#define PANEL_PREFS_STEP_WIFI 3U

typedef struct {
    uint8_t language;
    uint8_t setup_done;
    uint8_t step;
} panel_prefs_t;

typedef enum {
    PANEL_BOOT_WELCOME = 0,
    PANEL_BOOT_WIFI,
    PANEL_BOOT_ROLE,
    PANEL_BOOT_HOME,
} panel_boot_t;

panel_boot_t panel_prefs_boot(const panel_prefs_t *prefs);

/* Preview does not write. Continue commits. Settings changes language only. */
int panel_prefs_commit_language(panel_prefs_t *prefs, uint8_t language,
                                int from_settings);

int panel_prefs_commit_wifi(panel_prefs_t *prefs);

int panel_prefs_commit_done(panel_prefs_t *prefs);
