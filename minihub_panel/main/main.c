// SPDX-License-Identifier: Apache-2.0
#include "app_display.h"
#include "app_hub.h"
#include "app_minihub.h"
#include "app_panel.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "iris_ota_support.h"
#include "iris_screen_mirror.h"
#include "nvs_flash.h"
#include "ui_bundle.h"

static const char *TAG = "minihub_panel";

/* The composition root retains owned handles, never controller internals. */
static struct {
    esp_gsp_handle_t ui;
    app_hub_handle_t hub;
    app_panel_handle_t panel;
} application;

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    ESP_ERROR_CHECK(nvs_flash_init());
    /* Recovery must remain reachable even when the UI bundle cannot be loaded. */
    iris_ota_support_start();

    /* Run the unchanged upstream Host first. It owns Wi-Fi, BLE, Matter,
     * adapters, Web and their required startup order. */
    ESP_ERROR_CHECK(app_minihub_start());

    esp_gsp_config_t config;
    const esp_err_t err = ui_bundle_open(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GSP UI unavailable (0x%x); Recovery RPC remains active", err);
        return;
    }
    esp_gsp_esp_lcd_config_t display;
    const esp_err_t display_err = app_display_init(&display);
    if (display_err != ESP_OK) {
        ESP_LOGE(TAG, "Display unavailable (0x%x); Recovery RPC remains active", display_err);
        return;
    }
    const esp_err_t mirror_err = iris_screen_mirror_init();
    if (mirror_err != ESP_OK) {
        ESP_LOGE(TAG, "Screen mirror unavailable (%s); MiniHub remains active",
                 esp_err_to_name(mirror_err));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_mark_healthy());
        return;
    }
    const esp_err_t gsp_err =
        esp_gsp_esp_lcd_start(&config, &display, &application.ui);
    if (gsp_err != ESP_OK) {
        ESP_LOGE(TAG, "GSP display start failed (%s); MiniHub remains active",
                 esp_err_to_name(gsp_err));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_mark_healthy());
        return;
    }
    const esp_err_t attach_err = iris_screen_mirror_attach(application.ui);
    if (attach_err != ESP_OK) {
        ESP_LOGE(TAG, "Screen mirror attach failed (%s); MiniHub remains active",
                 esp_err_to_name(attach_err));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_mark_healthy());
        return;
    }

    /* The panel is only a localhost REST client; upstream owns networking. */
    ESP_ERROR_CHECK(app_hub_start(&application.hub) == APP_HUB_OK ?
                    ESP_OK : ESP_FAIL);

    ESP_ERROR_CHECK(app_panel_create(application.ui, application.hub,
                                     &application.panel));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_mark_healthy());
    ESP_LOGI(TAG, "MiniHub panel ready (local Host)");
}
