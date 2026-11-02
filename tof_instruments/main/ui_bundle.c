// SPDX-License-Identifier: Apache-2.0

#include "ui_bundle.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#include "bundle_gsp.h"
#include "esp_check.h"
#include "esp_gsp_deployable.h"
#include "esp_log.h"

static const char *TAG = "ui_bundle";
static esp_gsp_deployable_bundle_t *s_deployable;

esp_err_t ui_bundle_open(esp_gsp_config_t *out_config)
{
    if (out_config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_deployable != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t bundle_size = (size_t)(bundle_end - bundle_start);
    esp_err_t err = esp_gsp_deployable_bundle_open(
        bundle_start, bundle_size, true, &s_deployable);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not open embedded deployable GSPB: 0x%x", err);
        return err;
    }

    esp_gsp_deployable_info_t info = {
        .struct_size = sizeof(esp_gsp_deployable_info_t),
    };
    err = esp_gsp_deployable_bundle_get_info(s_deployable, &info);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not read deployable GSPB metadata: 0x%x", err);
        esp_gsp_deployable_bundle_close(s_deployable);
        s_deployable = NULL;
        return err;
    }

    err = esp_gsp_deployable_bundle_make_config(s_deployable, out_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not create GSP configuration: 0x%x", err);
        esp_gsp_deployable_bundle_close(s_deployable);
        s_deployable = NULL;
        return err;
    }

    ESP_LOGI(TAG,
             "Opened embedded rodata GSPB: %u bytes, scenes=%u, content_id=0x%08" PRIx32,
             (unsigned)bundle_size, (unsigned)info.scene_count,
             info.content_id);
    return ESP_OK;
}
