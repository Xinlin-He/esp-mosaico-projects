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
        return PANEL_BOOT_WIFI;
    }
    if (prefs->step == PANEL_PREFS_STEP_MODE) {
        return PANEL_BOOT_ROLE;
    }
    if (prefs->step == PANEL_PREFS_STEP_PRE_LANGUAGE_WIFI) {
        return PANEL_BOOT_WIFI;
    }
    if (prefs->step == PANEL_PREFS_STEP_PRE_WIFI_MODE) {
        return PANEL_BOOT_ROLE;
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
    if (from_settings || prefs->setup_done == PANEL_PREFS_SETUP_DONE) {
        return 0;
    }
    prefs->step = PANEL_PREFS_STEP_MODE;
    return 0;
}

int panel_prefs_commit_mode(panel_prefs_t *prefs)
{
    if (!prefs) {
        return -1;
    }
    if (prefs->setup_done == PANEL_PREFS_SETUP_DONE) {
        return 0;
    }
    prefs->step = PANEL_PREFS_STEP_WIFI;
    return 0;
}

int panel_prefs_commit_wifi(panel_prefs_t *prefs)
{
    if (!prefs) {
        return -1;
    }
    if (prefs->setup_done == PANEL_PREFS_SETUP_DONE) {
        return 0;
    }
    if (prefs->step == PANEL_PREFS_STEP_PRE_LANGUAGE_WIFI) {
        prefs->step = PANEL_PREFS_STEP_MODE;
        return 0;
    }
    if (prefs->step == PANEL_PREFS_STEP_WIFI) {
        prefs->setup_done = PANEL_PREFS_SETUP_DONE;
    }
    return 0;
}

int panel_prefs_store_result(panel_prefs_t *live, const panel_prefs_t *pending,
                             int saved_ok)
{
    if (!live || !pending) {
        return -1;
    }
    if (!saved_ok) {
        return 0;
    }
    *live = *pending;
    return 1;
}
