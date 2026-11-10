// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start MiniHub Host on this Mosaico app. Safe to call once after NVS. */
esp_err_t app_minihub_start(void);

#ifdef __cplusplus
}
#endif
