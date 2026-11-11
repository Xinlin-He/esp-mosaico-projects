// SPDX-License-Identifier: Apache-2.0
#include "hub_matter_status.h"
#include "panel_prefs.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect_int(const char *name, int got, int want)
{
    if (got != want) {
        printf("FAIL %s got %d want %d\n", name, got, want);
        ++fails;
    } else {
        printf("PASS %s\n", name);
    }
}

static void expect_str(const char *name, const char *got, const char *want)
{
    if (strcmp(got ? got : "", want) != 0) {
        printf("FAIL %s got \"%s\"\n", name, got ? got : "(null)");
        ++fails;
    } else {
        printf("PASS %s\n", name);
    }
}

static void test_language_order(void)
{
    panel_prefs_t prefs = {0};
    expect_int("fresh welcome", panel_prefs_boot(&prefs), PANEL_BOOT_WELCOME);
    expect_int("snapshot step 0 stays welcome", prefs.step, 0);

    expect_int("invalid language rejected",
               panel_prefs_commit_language(&prefs, 2, 0), -1);
    expect_int("invalid language keeps step", prefs.step, 0);
    expect_int("invalid language keeps welcome",
               panel_prefs_boot(&prefs), PANEL_BOOT_WELCOME);

    expect_int("commit en", panel_prefs_commit_language(&prefs, 0, 0), 0);
    expect_int("after language mode", panel_prefs_boot(&prefs), PANEL_BOOT_ROLE);
    expect_int("language step is mode", prefs.step, PANEL_PREFS_STEP_MODE);
    expect_int("language kept", prefs.language, 0);
    expect_int("not finished", prefs.setup_done, 0);

    panel_prefs_t pending = prefs;
    expect_int("save failure does not copy",
               panel_prefs_store_result(&prefs, &pending, 0), 0);
    pending.language = 1;
    pending.step = PANEL_PREFS_STEP_WIFI;
    expect_int("failed save keeps language", prefs.language, 0);
    expect_int("failed save keeps mode step", prefs.step, PANEL_PREFS_STEP_MODE);

    expect_int("commit zh", panel_prefs_commit_language(&prefs, 1, 0), 0);
    expect_int("language again stays mode", panel_prefs_boot(&prefs),
               PANEL_BOOT_ROLE);
    expect_int("zh stored", prefs.language, 1);

    expect_int("commit mode", panel_prefs_commit_mode(&prefs), 0);
    expect_int("after mode wifi", panel_prefs_boot(&prefs), PANEL_BOOT_WIFI);
    expect_int("wifi step stored", prefs.step, PANEL_PREFS_STEP_WIFI);
    expect_int("mode does not finish", prefs.setup_done, 0);

    expect_int("commit wifi", panel_prefs_commit_wifi(&prefs), 0);
    expect_int("after wifi home", panel_prefs_boot(&prefs), PANEL_BOOT_HOME);
    expect_int("wifi finishes setup", prefs.setup_done, PANEL_PREFS_SETUP_DONE);

    panel_prefs_t saved = {0};
    expect_int("successful save copies",
               panel_prefs_store_result(&saved, &prefs, 1), 1);
    expect_int("saved language", saved.language, 1);
    expect_int("saved done", saved.setup_done, PANEL_PREFS_SETUP_DONE);

    panel_prefs_t done = prefs;
    expect_int("settings keeps done",
               panel_prefs_commit_language(&done, 0, 1), 0);
    expect_int("settings keeps home", panel_prefs_boot(&done), PANEL_BOOT_HOME);
    expect_int("settings language", done.language, 0);
    expect_int("settings keeps step", done.step, prefs.step);
    expect_int("settings mode does not rewind",
               panel_prefs_commit_mode(&done), 0);
    expect_int("settings mode still home", panel_prefs_boot(&done),
               PANEL_BOOT_HOME);

    panel_prefs_t legacy = {.setup_done = PANEL_PREFS_SETUP_DONE, .step = 4};
    expect_int("legacy setup_done is home", panel_prefs_boot(&legacy),
               PANEL_BOOT_HOME);

    panel_prefs_t old_wifi = {.step = PANEL_PREFS_STEP_PRE_LANGUAGE_WIFI};
    expect_int("unreleased step 2 stays wifi", panel_prefs_boot(&old_wifi),
               PANEL_BOOT_WIFI);
    expect_int("unreleased wifi does not finish",
               panel_prefs_commit_wifi(&old_wifi), 0);
    expect_int("unreleased wifi moves to mode", panel_prefs_boot(&old_wifi),
               PANEL_BOOT_ROLE);
    expect_int("unreleased wifi not done", old_wifi.setup_done, 0);

    panel_prefs_t old_mode = {.step = PANEL_PREFS_STEP_PRE_WIFI_MODE};
    expect_int("unreleased step 3 stays mode", panel_prefs_boot(&old_mode),
               PANEL_BOOT_ROLE);
    expect_int("confirm language rewrites to mode step",
               panel_prefs_commit_language(&old_mode, 1, 0), 0);
    expect_int("rewritten step is current mode", old_mode.step,
               PANEL_PREFS_STEP_MODE);
}

static void test_matter_feedback(void)
{
    hub_matter_start_t start;
    hub_matter_classify_post(1, 200, "ok", "started", NULL, 7, &start);
    expect_int("accepted", start.kind, HUB_MATTER_START_ACCEPTED);
    expect_int("node", (int)start.node_id, 7);

    hub_matter_classify_post(1, 200, "error", "ESP_ERR_INVALID_STATE",
                             "matter commissioner not ready", 9, &start);
    expect_int("rejected", start.kind, HUB_MATTER_START_REJECTED);
    expect_str("raw message", start.detail, "matter commissioner not ready");
    expect_int("rejected does not keep a foreign node", (int)start.node_id, 0);

    hub_matter_classify_post(1, 200, "error", NULL, "", 4, &start);
    expect_int("missing is unknown error", start.kind, HUB_MATTER_START_REJECTED);
    expect_str("unknown error", start.detail, "Unknown error");
    expect_int("missing error has no node", (int)start.node_id, 0);

    char long_msg[200];
    memset(long_msg, 'B', sizeof(long_msg) - 1);
    long_msg[sizeof(long_msg) - 1] = '\0';
    hub_matter_classify_post(1, 200, "error", NULL, long_msg, 0, &start);
    expect_int("long detail fits", (int)strlen(start.detail),
               HUB_MATTER_DETAIL_LEN - 1);
    expect_int("long detail kept tail marker", start.detail[0], 'B');

    hub_matter_classify_post(0, 0, NULL, NULL, NULL, 0, &start);
    expect_int("timeout uncertain", start.kind, HUB_MATTER_START_UNCERTAIN);
    expect_str("does not invent failure", start.detail, "Status unknown");
    expect_int("timeout has no node", (int)start.node_id, 0);

    hub_matter_classify_post(0, 0, "ok", "started", NULL, 9, &start);
    expect_int("transport failure ignores a guessed start", start.kind,
               HUB_MATTER_START_UNCERTAIN);
    expect_int("transport failure clears guessed node", (int)start.node_id, 0);

    expect_int("queued enters", panel_submit_result(0, 0, 0), PANEL_SUBMIT_ENTER);
    expect_int("queue full stays out", panel_submit_result(1, 0, 0),
               PANEL_SUBMIT_QUEUE_FULL);
    expect_int("preparing is one submit", panel_submit_result(1, 1, 0),
               PANEL_SUBMIT_ALREADY);
    expect_int("waiting is one submit", panel_submit_result(1, 0, 1),
               PANEL_SUBMIT_ALREADY);
}

int main(void)
{
    test_language_order();
    test_matter_feedback();
    printf(fails ? "FAILS %d\n" : "OK\n", fails);
    return fails ? 1 : 0;
}
