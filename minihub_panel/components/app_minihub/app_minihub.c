// SPDX-License-Identifier: Apache-2.0

#include "app_minihub.h"

#include "esp_log.h"
#include "esp_private/esp_sys_event_app_init.h"

static const char *TAG = "app_minihub";
static bool s_started;

/* application/minihub/main/main.c is compiled unchanged with only its public
 * entry symbol renamed. All Host initialization remains owned by upstream. */
extern void minihub_official_main(void);

esp_err_t app_minihub_start(void)
{
    if (s_started) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting unchanged upstream MiniHub Host");
    esp_err_t pre_err = minihub_pre_app_main_run();
    if (pre_err != ESP_OK) {
        return pre_err;
    }
    minihub_official_main();
    s_started = true;
    return ESP_OK;
}
