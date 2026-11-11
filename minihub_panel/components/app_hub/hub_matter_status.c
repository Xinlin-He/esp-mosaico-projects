// SPDX-License-Identifier: Apache-2.0
#include "hub_matter_status.h"

#include <stdio.h>
#include <string.h>

static void copy_detail(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src || !src[0]) {
        snprintf(dst, dst_len, "%s", "Unknown error");
        return;
    }
    snprintf(dst, dst_len, "%s", src);
}

void hub_matter_classify_post(int transport_ok, int http_status,
                              const char *result, const char *commission,
                              const char *message, uint32_t node_id,
                              hub_matter_start_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (!transport_ok || http_status <= 0) {
        out->kind = HUB_MATTER_START_UNCERTAIN;
        snprintf(out->detail, sizeof(out->detail), "%s", "Status unknown");
        return;
    }
    if (result && strcmp(result, "ok") == 0 && commission &&
            strcmp(commission, "started") == 0 && node_id >= 2U) {
        out->kind = HUB_MATTER_START_ACCEPTED;
        out->node_id = node_id;
        out->detail[0] = '\0';
        return;
    }
    /* A node id on a rejected or incomplete body is not this request. */
    out->kind = HUB_MATTER_START_REJECTED;
    out->node_id = 0;
    if (message && message[0]) {
        copy_detail(out->detail, sizeof(out->detail), message);
    } else if (commission && commission[0] && strcmp(commission, "started") != 0) {
        copy_detail(out->detail, sizeof(out->detail), commission);
    } else {
        copy_detail(out->detail, sizeof(out->detail), NULL);
    }
}

panel_submit_t panel_submit_result(int hub_result_busy, int add_state_preparing,
                                   int add_state_waiting)
{
    if (!hub_result_busy) {
        return PANEL_SUBMIT_ENTER;
    }
    if (add_state_preparing || add_state_waiting) {
        return PANEL_SUBMIT_ALREADY;
    }
    return PANEL_SUBMIT_QUEUE_FULL;
}
