// SPDX-License-Identifier: Apache-2.0

#include "board_display.h"

#include "bsp/esp_mosaico.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "gsp_board";

esp_err_t board_display_init(esp_display_present_target_config_t *out_target)
{
    ESP_RETURN_ON_FALSE(out_target != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "present target is null");

    const bsp_display_config_t config = BSP_DISPLAY_DEFAULT_CONFIG();
    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(bsp_display_new(&config, &panel), TAG,
                        "initialize CO5300");
    ESP_RETURN_ON_FALSE(panel != NULL && bsp_display_get_panel_io() != NULL,
                        ESP_ERR_INVALID_STATE, TAG, "panel/io missing");

    *out_target = (esp_display_present_target_config_t) {
        .hw = {
            .panel = panel,
            .io = bsp_display_get_panel_io(),
            .panel_type = ESP_DISPLAY_PRESENT_PANEL_IO,
            .input_pixel_format = ESP_DISPLAY_PRESENT_PIXEL_FORMAT_RGB565,
            .rotation = ESP_DISPLAY_PRESENT_ROTATE_0,
            .swap_bytes = true,
        },
        /* Plain GRAM output. The TE path composes a full 460 KB frame in PSRAM
         * and pushes it in one transfer; on this board the BSP panel IO copies
         * any PSRAM source through an internal bounce buffer, so a full-frame
         * push cannot allocate and the first render fails. */
        .fb = {
            .mode = ESP_DISPLAY_PRESENT_MODE_NONE,
        },
        /* Staging bands stay in internal DMA memory so the panel IO reads them
         * without a bounce copy. 34 lines is the default 32 KB band, and two
         * buffers let the DMA for one band run while the next is filled. */
        .drawbuf = {
            .lines = 34,
            .in_psram = false,
            .buffers = 2,
        },
    };
    ESP_LOGI(TAG, "GSP present target %dx%d RGB565 mode=gram_dma drawbuf=34x2",
             BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}

esp_err_t board_touch_init(esp_lcd_touch_handle_t *out_touch)
{
    ESP_RETURN_ON_FALSE(out_touch != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "touch output is null");
    const esp_err_t err = bsp_touch_new(BSP_LCD_ROTATION_DEFAULT, out_touch);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch unavailable (%s); continuing without it",
                 esp_err_to_name(err));
        *out_touch = NULL;
        return ESP_OK;
    }
    return ESP_OK;
}
