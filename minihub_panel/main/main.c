// SPDX-License-Identifier: Apache-2.0
#include "app_display.h"
#include "app_hub.h"
#include "app_minihub.h"
#include "app_panel.h"
#include "cJSON.h"
#include "esp_bt.h"
#include "esp_flash_dispatcher.h"
#include "esp_heap_caps.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "iris_ota_support.h"
#include "iris_screen_mirror.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include "ui_bundle.h"

static const char *TAG = "minihub_panel";

/* The composition root retains owned handles, never controller internals. */
static struct {
    esp_gsp_handle_t ui;
    app_hub_handle_t hub;
    app_panel_handle_t panel;
} application;

static void *json_psram_malloc(size_t size)
{
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return ptr ? ptr : heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

void app_main(void)
{
    /*
     * The Web device catalog builds large cJSON trees while Wi-Fi modem sleep
     * may need internal RAM to recreate its PHY timer. cJSON hooks are global,
     * so install them once before any application component can parse JSON.
     */
    cJSON_Hooks json_hooks = {
        .malloc_fn = json_psram_malloc,
        .free_fn = free,
    };
    cJSON_InitHooks(&json_hooks);

    const esp_flash_dispatcher_config_t flash_dispatcher = {
        .task_stack_size = 4096,
        .task_priority = 10,
        .task_core_id = tskNO_AFFINITY,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(esp_flash_dispatcher_init(&flash_dispatcher));

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    ESP_ERROR_CHECK(nvs_flash_init());
    /* Recovery must remain reachable even when the UI bundle cannot be loaded. */
    iris_ota_support_start();

    /* Initialize and enable the BLE controller before GSP/Wi-Fi claim internal
     * DMA RAM, so its link-layer mempool always gets contiguous internal DMA. */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_bt_controller_enable(ESP_BT_MODE_BLE));

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
    ESP_ERROR_CHECK(iris_screen_mirror_init());
    ESP_ERROR_CHECK(esp_gsp_esp_lcd_start(&config, &display, &application.ui));
    ESP_ERROR_CHECK(iris_screen_mirror_attach(application.ui));
    ESP_ERROR_CHECK(app_hub_network_init() == APP_HUB_OK ?
                    ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(app_hub_start(&application.hub) == APP_HUB_OK ?
                    ESP_OK : ESP_FAIL);

    const esp_err_t hub_err = app_minihub_start();
    if (hub_err != ESP_OK) {
        ESP_LOGE(TAG, "MiniHub Host unavailable (0x%x); panel stays on localhost",
                 hub_err);
    } else {
        /* IMU publish runs MiniHub rules_exec → Matter OnOff on that
         * task; shake coredumps in CHIPCryptoPALPSA. Handheld classify
         * is also not accurate enough. Re-enable via app_imu_start(). */
        ESP_LOGI(TAG, "Onboard IMU disabled");
    }

    ESP_ERROR_CHECK(app_panel_create(application.ui, application.hub,
                                     &application.panel));
    ESP_LOGI(TAG, "MiniHub panel ready (local Host)");
}
