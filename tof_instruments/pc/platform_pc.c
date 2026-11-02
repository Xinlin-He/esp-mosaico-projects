// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game_ui.h"
#include "gsp_sim_bridge.h"

static kodou_game_t s_game;
static uint32_t s_clock_ms;

static uint32_t clock_ms(void *ctx)
{
    (void)ctx;
    /* Keep headless captures deterministic: simulator frames may execute much
     * faster than wall time, but the game timer still represents 25 ms. */
    s_clock_ms += 25U;
    return s_clock_ms;
}

static void feedback(kodou_hit_t hit, kodou_judgement_t judgement,
                     uint8_t strength, void *ctx)
{
    (void)ctx;
    fprintf(stderr, "kodou hit=%d judgement=%d strength=%u\n",
            (int)hit, (int)judgement, (unsigned)strength);
}

esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t ui)
{
    s_clock_ms = 0;
    const kodou_game_hooks_t hooks = {
        .feedback = feedback,
        .clock_ms = clock_ms,
    };
    esp_gsp_err_t err = kodou_game_init(ui, &s_game, &hooks);
    if (err != ESP_GSP_OK) return err;
    kodou_game_set_imu_state(&s_game, KODOU_IMU_READY);
    kodou_game_set_motion_level(&s_game, 42);
    const char *view = getenv("KODOU_SIM_VIEW");
    const char *lang = getenv("KODOU_SIM_LANG");
    const bool english = lang != NULL && strcmp(lang, "en") == 0;
    kodou_game_sim_preview_view(&s_game, view, english);
    return ESP_GSP_OK;
}

void gsp_bridge_app_deinit(esp_gsp_handle_t ui)
{
    (void)ui;
    kodou_game_deinit(&s_game);
    fprintf(stderr,
            "kodou_backend: complete=%d score=%lu combo=%lu best=%lu "
            "perfect=%u good=%u miss=%u error=%d\n",
            s_game.completion_shown ? 1 : 0,
            (unsigned long)s_game.score, (unsigned long)s_game.combo,
            (unsigned long)s_game.best_combo, (unsigned)s_game.perfect_count,
            (unsigned)s_game.good_count, (unsigned)s_game.miss_count,
            s_game.last_error);
}
