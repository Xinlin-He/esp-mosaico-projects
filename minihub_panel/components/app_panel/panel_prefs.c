// SPDX-License-Identifier: Apache-2.0
#include "panel_prefs.h"

panel_boot_t panel_prefs_boot(const panel_prefs_t *prefs)
{
    if (!prefs) {
        return PANEL_BOOT_WELCOME;
    }
    if (prefs->setup_done == PANEL_PREFS_SETUP_DONE) {
        return PANEL_BOOT_HOME;
    }
    if (prefs->step == PANEL_PREFS_STEP_WIFI) {
        return PANEL_BOOT_ROLE;
    }
    if (prefs->step == PANEL_PREFS_STEP_LANGUAGE) {
        return PANEL_BOOT_WIFI;
    }
    return PANEL_BOOT_WELCOME;
}

int panel_prefs_commit_language(panel_prefs_t *prefs, uint8_t language,
                                int from_settings)
{
    if (!prefs || language > 1U) {
        return -1;
    }
    prefs->language = language;
    if (from_settings) {
        return 0;
    }
    if (prefs->setup_done == PANEL_PREFS_SETUP_DONE) {
        return 0;
    }
    if (prefs->step < PANEL_PREFS_STEP_LANGUAGE) {
        prefs->step = PANEL_PREFS_STEP_LANGUAGE;
    }
    return 0;
}

int panel_prefs_commit_wifi(panel_prefs_t *prefs)
{
    if (!prefs || prefs->setup_done == PANEL_PREFS_SETUP_DONE) {
        return prefs ? 0 : -1;
    }
    if (prefs->step < PANEL_PREFS_STEP_WIFI) {
        prefs->step = PANEL_PREFS_STEP_WIFI;
    }
    return 0;
}

int panel_prefs_commit_done(panel_prefs_t *prefs)
{
    if (!prefs) {
        return -1;
    }
    prefs->setup_done = PANEL_PREFS_SETUP_DONE;
    return 0;
}
