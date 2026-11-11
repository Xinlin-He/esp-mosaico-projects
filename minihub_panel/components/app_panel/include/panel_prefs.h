// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

/*
 * First-run progress stored beside the language choice.
 *
 * setup_done == 1 is the historical finished value. Boot goes home and
 * ignores step, including any step left beside it.
 *
 * The snapshot never stored setup_step. step 0 is an interrupted run from
 * that tree and still opens the welcome page.
 *
 * 0c11f03 (not a flashed release) stored two markers for a different order:
 *   2: language saved, Wi-Fi was next
 *   3: Wi-Fi accepted, mode was next
 * Boot still opens those pages. It does not swap them.
 *
 * The product order is language, then mode, then Wi-Fi.
 *   4: language saved, mode is next
 *   5: mode saved, Wi-Fi is next
 * Accepting Wi-Fi after step 5 sets setup_done. Hub is then local-ready.
 * Panel mode stores the remote hub before this Wi-Fi step.
 */
#define PANEL_PREFS_SETUP_DONE 1U
#define PANEL_PREFS_STEP_PRE_LANGUAGE_WIFI 2U
#define PANEL_PREFS_STEP_PRE_WIFI_MODE 3U
#define PANEL_PREFS_STEP_MODE 4U
#define PANEL_PREFS_STEP_WIFI 5U

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

/* Mode is saved before Wi-Fi. A finished setup is left unchanged. */
int panel_prefs_commit_mode(panel_prefs_t *prefs);

/*
 * Wi-Fi is the last first-run step when mode is already saved.
 * The unreleased marker 2 has no saved mode, so accepting Wi-Fi moves to
 * mode instead of marking setup finished.
 */
int panel_prefs_commit_wifi(panel_prefs_t *prefs);

/* Copy pending prefs only after the worker reports a successful save. */
int panel_prefs_store_result(panel_prefs_t *live, const panel_prefs_t *pending,
                             int saved_ok);
