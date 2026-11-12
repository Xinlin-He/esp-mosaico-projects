// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#define HUB_MATTER_DETAIL_LEN 128

typedef enum {
    HUB_MATTER_START_ACCEPTED = 0,
    HUB_MATTER_START_REJECTED,
    HUB_MATTER_START_UNCERTAIN,
} hub_matter_start_kind_t;

typedef struct {
    hub_matter_start_kind_t kind;
    uint32_t node_id;
    char detail[HUB_MATTER_DETAIL_LEN];
} hub_matter_start_t;

/*
 * Classify one POST /api/matter/devices outcome.
 * transport_ok is false when the client timed out or never got a response.
 * A later "running" status is not proof this request started: there is no
 * request id. Correlation requires the node id returned for this response.
 */
void hub_matter_classify_post(int transport_ok, int http_status,
                              const char *result, const char *commission,
                              const char *message, uint32_t node_id,
                              hub_matter_start_t *out);

/* Queue full must not look like pairing. An in-flight add is one submission. */
typedef enum {
    PANEL_SUBMIT_ENTER = 0,
    PANEL_SUBMIT_QUEUE_FULL,
    PANEL_SUBMIT_ALREADY,
} panel_submit_t;

panel_submit_t panel_submit_result(int hub_result_busy, int add_state_preparing,
                                   int add_state_waiting);
