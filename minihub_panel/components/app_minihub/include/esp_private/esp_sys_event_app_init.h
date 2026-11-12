/*
 * IDF 7b9cc1ac has esp_sys_event, but its event list has no PRE_APP_MAIN
 * stage and does not ship this header. Official MiniHub main.c still uses
 * ESP_PRE_APP_MAIN_HANDLER_REGISTER to open a function that must run after
 * the scheduler is up and before the upstream host entry.
 *
 * The macro keeps that function body. app_minihub_start() calls it directly
 * because this IDF has nowhere earlier, after the scheduler, to register it.
 * There is one such handler, so the priority argument is not reimplemented.
 */
#pragma once

#include "esp_err.h"

#define ESP_PRE_APP_MAIN_HANDLER_REGISTER(fn, prio) \
    esp_err_t fn(void *user_arg, void *ctx)

esp_err_t minihub_flash_dispatcher_init(void *user_arg, void *ctx);

static inline esp_err_t minihub_pre_app_main_run(void)
{
    return minihub_flash_dispatcher_init(0, 0);
}
