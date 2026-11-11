// SPDX-License-Identifier: Apache-2.0

#include "app_hub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"

#ifndef CONFIG_MINIHUB_MATTER_COMMISSION_WAIT_MS
#define CONFIG_MINIHUB_MATTER_COMMISSION_WAIT_MS 300000
#endif

#define HUB_MAX_POINTS 64
#define HUB_MAX_SCENES 8
#define HUB_MAX_RULES 16
#define HUB_HTTP_BODY_CAPACITY (64 * 1024)
#define HUB_URL_CAPACITY 256
#define HUB_COOKIE_CAPACITY 160
#define HUB_TOKEN_CAPACITY 97
#define HUB_COMMAND_QUEUE_LENGTH 8
#define HUB_MATTER_ADD_TIMEOUT_MS CONFIG_MINIHUB_MATTER_COMMISSION_WAIT_MS
#define HUB_SYNC_INTERVAL_MS 3000
#define HUB_METADATA_SYNC_CYCLES 10
#define HUB_HTTP_TIMEOUT_MS 5000
#define HUB_MUTEX_TIMEOUT_MS 100
#define HUB_OFFLINE_STRIKES 3
#define HUB_NVS_NAMESPACE "minihub_panel"
#define HUB_NVS_TOKEN "api_token"
#define HUB_NVS_ROLE "role"
#define HUB_NVS_REMOTE_URL "remote_url"
#define HUB_LOCAL_URL "http://127.0.0.1"

typedef enum {
    HUB_CMD_DEVICE = 0,
    HUB_CMD_MATTER_COMMISSION,
    HUB_CMD_MATTER_CANCEL,
    HUB_CMD_ADD_DEVICE,
    HUB_CMD_ADD_CANCEL,
    HUB_CMD_DELETE_POINT,
    HUB_CMD_SCENE_INVOKE,
    HUB_CMD_SCENE_CREATE,
    HUB_CMD_SCENE_DELETE,
    HUB_CMD_RULE_SET_ENABLED,
    HUB_CMD_RULE_CREATE,
    HUB_CMD_RULE_DELETE,
    HUB_CMD_WIFI_SCAN,
    HUB_CMD_SYSTEM_WIFI,
    HUB_CMD_SYSTEM_REBOOT,
} hub_cmd_kind_t;

static const char *TAG = "app_hub";

typedef struct {
    hub_cmd_kind_t kind;
    char point_id[APP_HUB_POINT_ID_LEN];
    char parameter[APP_HUB_PARAMETER_LEN];
    int32_t value;
    bool has_value;
    bool enabled;
    app_hub_add_request_t add;
    char name[APP_HUB_NAME_LEN];
    char source_value[APP_HUB_VALUE_LEN];
    char target_point[APP_HUB_POINT_ID_LEN];
    char scene_points[APP_HUB_PAGE_SIZE][APP_HUB_POINT_ID_LEN];
    bool scene_values[APP_HUB_PAGE_SIZE];
    uint8_t scene_count;
    char wifi_ssid[APP_HUB_WIFI_SSID_LEN];
    char wifi_password[APP_HUB_CREDENTIAL_LEN + 1];
    bool wifi_open;
} hub_command_t;

typedef struct {
    char *body;
    size_t length;
    size_t capacity;
    char *cookie;
    size_t cookie_capacity;
} http_capture_t;

typedef struct {
    int status;
    char *body;
    char cookie[HUB_COOKIE_CAPACITY];
} http_response_t;

struct app_hub {
    SemaphoreHandle_t mutex;
    QueueHandle_t command_queue;
    TaskHandle_t worker;
    app_hub_point_t points[HUB_MAX_POINTS];
    size_t point_count;
    size_t online_count;
    uint32_t generation;
    bool hub_connected;
    app_hub_role_t role;
    char base_url[APP_HUB_REMOTE_URL_LEN];
    char api_token[HUB_TOKEN_CAPACITY];
    char status[APP_HUB_STATUS_LEN];
    char cookie[HUB_COOKIE_CAPACITY];
    app_hub_commission_snapshot_t commission;
    bool commission_active;
    app_hub_add_snapshot_t add;
    app_hub_add_request_t active_add;
    char *add_adapters_backup;
    size_t add_baseline_points;
    TickType_t add_deadline;
    volatile bool add_cancel_requested;
    app_hub_scene_t scenes[HUB_MAX_SCENES];
    size_t scene_count;
    uint32_t scenes_generation;
    bool scenes_ready;
    bool scene_pending;
    char scenes_status[APP_HUB_STATUS_LEN];
    char *scenes_json;
    app_hub_rule_t rules[HUB_MAX_RULES];
    size_t rule_count;
    uint32_t rules_generation;
    bool rules_ready;
    bool rule_pending;
    char rules_status[APP_HUB_STATUS_LEN];
    char *rules_json;
    app_hub_system_snapshot_t system;
    char local_ip[APP_HUB_IP_LEN];
    app_hub_wifi_snapshot_t wifi;
};

static esp_err_t save_connection_config(app_hub_role_t role,
                                        const char *url,
                                        const char *token)
{
    nvs_handle_t nvs;
    ESP_RETURN_ON_ERROR(nvs_open(HUB_NVS_NAMESPACE, NVS_READWRITE, &nvs), TAG,
                        "nvs open");
    esp_err_t err = nvs_set_u8(nvs, HUB_NVS_ROLE, (uint8_t)role);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, HUB_NVS_REMOTE_URL, url != NULL ? url : "");
    }
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, HUB_NVS_TOKEN, token != NULL ? token : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static void set_status(struct app_hub *hub, bool connected, const char *status)
{
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return;
    }
    hub->hub_connected = connected;
    strlcpy(hub->status, status, sizeof(hub->status));
    ++hub->generation;
    xSemaphoreGive(hub->mutex);
}

static void set_add_status(struct app_hub *hub, app_hub_add_state_t state,
                           uint8_t progress, const char *status,
                           const char *point_id)
{
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return;
    }
    hub->add.state = state;
    hub->add.progress = progress;
    if (status) {
        strlcpy(hub->add.status, status, sizeof(hub->add.status));
    }
    if (point_id) {
        strlcpy(hub->add.point_id, point_id, sizeof(hub->add.point_id));
    }
    ++hub->add.generation;
    xSemaphoreGive(hub->mutex);
}

static esp_err_t http_event(esp_http_client_event_t *event)
{
    http_capture_t *capture = event->user_data;
    if (!capture) {
        return ESP_OK;
    }
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        const size_t room = capture->capacity - capture->length - 1;
        const size_t copy = (size_t)event->data_len < room ? (size_t)event->data_len : room;
        if (copy > 0) {
            memcpy(capture->body + capture->length, event->data, copy);
            capture->length += copy;
            capture->body[capture->length] = '\0';
        }
    } else if (event->event_id == HTTP_EVENT_ON_HEADER && capture->cookie &&
               event->header_key && event->header_value &&
               strcasecmp(event->header_key, "Set-Cookie") == 0) {
        const char *end = strchr(event->header_value, ';');
        size_t length = end ? (size_t)(end - event->header_value) :
                        strlen(event->header_value);
        if (length >= capture->cookie_capacity) {
            length = capture->cookie_capacity - 1;
        }
        memcpy(capture->cookie, event->header_value, length);
        capture->cookie[length] = '\0';
    }
    return ESP_OK;
}

static void response_release(http_response_t *response)
{
    if (response) {
        free(response->body);
        response->body = NULL;
        response->status = 0;
    }
}

static esp_err_t request(struct app_hub *hub,
                         esp_http_client_method_t method,
                         const char *endpoint, const char *json_body,
                         http_response_t *out_response)
{
    if (!hub || !endpoint || !out_response) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_response = (http_response_t) {
        0
    };

    char base_url[APP_HUB_REMOTE_URL_LEN];
    char token[HUB_TOKEN_CAPACITY] = {0};
    char cookie[HUB_COOKIE_CAPACITY] = {0};
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    strlcpy(base_url, hub->base_url, sizeof(base_url));
    if (CONFIG_MINIHUB_PANEL_API_TOKEN[0] != '\0') {
        strlcpy(token, CONFIG_MINIHUB_PANEL_API_TOKEN, sizeof(token));
    } else if (hub->role == APP_HUB_ROLE_PANEL) {
        strlcpy(token, hub->api_token, sizeof(token));
    }
    strlcpy(cookie, hub->cookie, sizeof(cookie));
    xSemaphoreGive(hub->mutex);

    char url[HUB_URL_CAPACITY];
    int written = snprintf(url, sizeof(url), "%s%s", base_url, endpoint);
    if (written < 0 || (size_t)written >= sizeof(url)) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* 64KB belongs in SPIRAM. Falling back to internal RAM starves GSP. */
    char *body = heap_caps_calloc(1, HUB_HTTP_BODY_CAPACITY,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    http_capture_t capture = {
        .body = body,
        .capacity = HUB_HTTP_BODY_CAPACITY,
        .cookie = out_response->cookie,
        .cookie_capacity = sizeof(out_response->cookie),
    };
    const esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event,
        .user_data = &capture,
        .timeout_ms = HUB_HTTP_TIMEOUT_MS,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(body);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_set_method(client, method);
    if (err == ESP_OK && token[0] != '\0') {
        char authorization[192];
        written = snprintf(authorization, sizeof(authorization), "Bearer %s",
                           token);
        if (written < 0 || (size_t)written >= sizeof(authorization)) {
            err = ESP_ERR_INVALID_SIZE;
        } else {
            err = esp_http_client_set_header(client, "Authorization", authorization);
        }
    } else if (err == ESP_OK && cookie[0] != '\0') {
        err = esp_http_client_set_header(client, "Cookie", cookie);
    }
    if (err == ESP_OK && json_body) {
        err = esp_http_client_set_header(client, "Content-Type", "application/json");
    }
    if (err == ESP_OK && json_body) {
        err = esp_http_client_set_post_field(client, json_body, strlen(json_body));
    }
    if (err == ESP_OK) {
        err = esp_http_client_perform(client);
    }
    const int status = esp_http_client_get_status_code(client);
    if (status > 0) {
        out_response->status = status;
        out_response->body = body;
        body = NULL;
        if (status == 401) {
            err = ESP_OK;
        }
    }
    esp_http_client_cleanup(client);
    free(body);
    return err;
}

static bool has_api_token(const struct app_hub *hub)
{
    return CONFIG_MINIHUB_PANEL_API_TOKEN[0] != '\0' ||
           (hub->role == APP_HUB_ROLE_PANEL && hub->api_token[0] != '\0');
}

static esp_err_t login(struct app_hub *hub)
{
    if (has_api_token(hub) || hub->cookie[0] != '\0') {
        return ESP_OK;
    }
    if (CONFIG_MINIHUB_PANEL_HUB_PASSWORD[0] == '\0') {
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "username", CONFIG_MINIHUB_PANEL_HUB_USERNAME);
    cJSON_AddStringToObject(root, "password", CONFIG_MINIHUB_PANEL_HUB_PASSWORD);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }

    http_response_t response = {0};
    esp_err_t err = request(hub, HTTP_METHOD_POST, "/api/auth/login", payload,
                            &response);
    free(payload);
    if (err == ESP_OK && response.cookie[0] != '\0') {
        strlcpy(hub->cookie, response.cookie, sizeof(hub->cookie));
    }
    if (err == ESP_OK && response.status != 200) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    response_release(&response);
    return err;
}

static const cJSON *point_power_value(const cJSON *point)
{
    const cJSON *props = cJSON_GetObjectItemCaseSensitive(point, "props");
    const cJSON *params = cJSON_IsObject(props) ?
                          cJSON_GetObjectItemCaseSensitive(props, "params") : NULL;
    const cJSON *power = cJSON_IsObject(params) ?
                         cJSON_GetObjectItemCaseSensitive(params, "power") : NULL;
    if (cJSON_IsObject(power)) {
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(power, "value");
        return value ? value : power;
    }
    return power;
}

static bool point_param_int(const cJSON *point, const char *name,
                            int32_t *out_value)
{
    const cJSON *props = cJSON_GetObjectItemCaseSensitive(point, "props");
    const cJSON *params = cJSON_IsObject(props) ?
                          cJSON_GetObjectItemCaseSensitive(props, "params") : NULL;
    const cJSON *param = cJSON_IsObject(params) ?
                         cJSON_GetObjectItemCaseSensitive(params, name) : NULL;
    const cJSON *value = cJSON_IsObject(param) ?
                         cJSON_GetObjectItemCaseSensitive(param, "value") : param;
    if (cJSON_IsNumber(value)) {
        *out_value = (int32_t)value->valuedouble;
        return true;
    }
    if (cJSON_IsString(value) && value->valuestring) {
        char *end = NULL;
        const long parsed = strtol(value->valuestring, &end, 10);
        if (end != value->valuestring && *end == '\0') {
            *out_value = (int32_t)parsed;
            return true;
        }
    }
    return false;
}

static bool json_is_on(const cJSON *item)
{
    if (cJSON_IsTrue(item)) {
        return true;
    }
    if (cJSON_IsNumber(item)) {
        return item->valuedouble != 0;
    }
    if (cJSON_IsString(item) && item->valuestring) {
        return strcasecmp(item->valuestring, "on") == 0 ||
               strcasecmp(item->valuestring, "true") == 0 ||
               strcmp(item->valuestring, "1") == 0;
    }
    return false;
}

static void format_value(const cJSON *item, char *out, size_t out_size)
{
    if (!item) {
        strlcpy(out, "--", out_size);
    } else if (cJSON_IsString(item) && item->valuestring) {
        strlcpy(out, item->valuestring, out_size);
    } else if (cJSON_IsTrue(item)) {
        strlcpy(out, "ON", out_size);
    } else if (cJSON_IsFalse(item)) {
        strlcpy(out, "OFF", out_size);
    } else if (cJSON_IsNumber(item)) {
        snprintf(out, out_size, "%.2f", item->valuedouble);
    } else {
        strlcpy(out, "available", out_size);
    }
}

static bool type_is_controllable(const char *type)
{
    return strcmp(type, "light") == 0 || strcmp(type, "switch") == 0 ||
           strcmp(type, "plug") == 0 || strcmp(type, "outlet") == 0;
}

static esp_err_t parse_points(struct app_hub *hub, const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    app_hub_point_t *parsed = heap_caps_calloc(
        HUB_MAX_POINTS, sizeof(*parsed), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!parsed) {
        parsed = calloc(HUB_MAX_POINTS, sizeof(*parsed));
    }
    if (!parsed) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    size_t count = 0;
    size_t online_count = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, root) {
        if (count >= HUB_MAX_POINTS || !cJSON_IsObject(item)) {
            break;
        }
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "point_id");
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
        if (!cJSON_IsString(id) || !id->valuestring ||
                !cJSON_IsString(type) || !type->valuestring) {
            continue;
        }
        app_hub_point_t *point = &parsed[count];
        strlcpy(point->point_id, id->valuestring, sizeof(point->point_id));
        const cJSON *props = cJSON_GetObjectItemCaseSensitive(item, "props");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        if (!cJSON_IsString(name) && cJSON_IsObject(props)) {
            name = cJSON_GetObjectItemCaseSensitive(props, "name");
        }
        strlcpy(point->name, cJSON_IsString(name) && name->valuestring ?
                name->valuestring : id->valuestring, sizeof(point->name));
        strlcpy(point->type, type->valuestring, sizeof(point->type));
        const cJSON *online = cJSON_GetObjectItemCaseSensitive(item, "online");
        point->online = !cJSON_IsFalse(online);
        const cJSON *power = point_power_value(item);
        const cJSON *value = power ? power :
                             cJSON_GetObjectItemCaseSensitive(item, "value");
        const cJSON *no_control = cJSON_IsObject(props) ?
            cJSON_GetObjectItemCaseSensitive(props, "no_control") : NULL;
        const cJSON *commissioning = cJSON_IsObject(props) ?
            cJSON_GetObjectItemCaseSensitive(props, "commissioning") : NULL;
        const cJSON *cphase = cJSON_IsObject(props) ?
            cJSON_GetObjectItemCaseSensitive(props, "commission_phase") : NULL;
        const cJSON *nid = cJSON_IsObject(props) ?
            cJSON_GetObjectItemCaseSensitive(props, "node_id") : NULL;
        point->on = json_is_on(value);
        point->commissioning = cJSON_IsTrue(commissioning);
        point->commission_failed = cJSON_IsString(cphase) &&
                                   cphase->valuestring &&
                                   strcmp(cphase->valuestring, "failed") == 0;
        if (cJSON_IsString(nid) && nid->valuestring) {
            point->node_id = (uint32_t)strtoul(nid->valuestring, NULL, 10);
        } else if (cJSON_IsNumber(nid)) {
            point->node_id = (uint32_t)nid->valuedouble;
        }
        point->controllable = !cJSON_IsTrue(no_control) &&
                              !point->commissioning &&
                              (type_is_controllable(point->type) || power != NULL);
        if (point->commission_failed) {
            strlcpy(point->value, "FAIL", sizeof(point->value));
        } else if (point->commissioning) {
            strlcpy(point->value, "PAIR", sizeof(point->value));
        } else {
            format_value(value, point->value, sizeof(point->value));
        }
        point->has_brightness = point_param_int(item, "brightness",
                                                &point->brightness);
        point->has_hue = point_param_int(item, "hue", &point->hue);
        point->has_saturation = point_param_int(item, "saturation",
                                                &point->saturation);
        point->has_cct = point_param_int(item, "cct", &point->cct);
        ++count;
    }
    cJSON_Delete(root);

    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        free(parsed);
        return ESP_ERR_TIMEOUT;
    }
    online_count = 0;
    for (size_t i = 0; i < count; ++i) {
        uint8_t strikes = 0;
        bool was_shown_online = false;
        for (size_t j = 0; j < hub->point_count; ++j) {
            if (strcmp(hub->points[j].point_id, parsed[i].point_id) != 0) {
                continue;
            }
            strikes = hub->points[j].offline_strikes;
            was_shown_online = hub->points[j].online;
            break;
        }
        if (parsed[i].online) {
            parsed[i].offline_strikes = 0;
        } else if (was_shown_online || strikes > 0) {
            if (strikes < 255) {
                ++strikes;
            }
            parsed[i].offline_strikes = strikes;
            if (strikes < HUB_OFFLINE_STRIKES) {
                parsed[i].online = true;
            }
        } else {
            parsed[i].offline_strikes = HUB_OFFLINE_STRIKES;
        }
        if (parsed[i].online) {
            ++online_count;
        }
    }
    memcpy(hub->points, parsed, count * sizeof(*parsed));
    if (count < HUB_MAX_POINTS) {
        memset(&hub->points[count], 0,
               (HUB_MAX_POINTS - count) * sizeof(*parsed));
    }
    hub->point_count = count;
    hub->online_count = online_count;
    hub->hub_connected = true;
    if (hub->commission_active) {
        if (hub->commission.stage == APP_HUB_COMMISSION_STAGE_BINDING) {
            snprintf(hub->status, sizeof(hub->status), "Binding...");
        } else if (hub->commission.stage == APP_HUB_COMMISSION_STAGE_ACTIVATING) {
            snprintf(hub->status, sizeof(hub->status), "Activating...");
        } else {
            snprintf(hub->status, sizeof(hub->status), "Discovering...");
        }
    } else {
        snprintf(hub->status, sizeof(hub->status), "MiniHub online");
    }
    ++hub->generation;
    xSemaphoreGive(hub->mutex);
    free(parsed);
    return ESP_OK;
}

static esp_err_t sync_points(struct app_hub *hub)
{
    esp_err_t err = login(hub);
    if (err != ESP_OK) {
        set_status(hub, false, "MiniHub login failed");
        return err;
    }
    http_response_t response = {0};
    err = request(hub, HTTP_METHOD_GET, "/api/points", NULL, &response);
    if (err != ESP_OK) {
        set_status(hub, false, "MiniHub unreachable");
        return err;
    }
    if (response.status == 401) {
        if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            hub->cookie[0] = '\0';
            xSemaphoreGive(hub->mutex);
        }
        set_status(hub, false, "Invalid token");
        response_release(&response);
        return ESP_ERR_INVALID_STATE;
    }
    if (response.status != 200) {
        set_status(hub, false, "MiniHub returned an error");
        response_release(&response);
        return ESP_ERR_INVALID_RESPONSE;
    }
    err = parse_points(hub, response.body);
    response_release(&response);
    if (err != ESP_OK) {
        set_status(hub, false, "Invalid Point response");
    }
    return err;
}

static void fill_scene_actions(app_hub_scene_t *scene, const cJSON *item)
{
    scene->action_count = 0;
    const cJSON *steps = cJSON_GetObjectItemCaseSensitive(item, "steps");
    if (!cJSON_IsArray(steps) || cJSON_GetArraySize(steps) <= 0) {
        return;
    }
    const cJSON *step0 = cJSON_GetArrayItem(steps, 0);
    const cJSON *actions = cJSON_IsObject(step0) ?
                           cJSON_GetObjectItemCaseSensitive(step0, "actions") :
                           NULL;
    if (!cJSON_IsArray(actions)) {
        return;
    }
    const cJSON *action;
    cJSON_ArrayForEach(action, actions) {
        if (scene->action_count >= APP_HUB_PAGE_SIZE || !cJSON_IsObject(action)) {
            break;
        }
        const cJSON *point_id =
            cJSON_GetObjectItemCaseSensitive(action, "point_id");
        const cJSON *cmd = cJSON_GetObjectItemCaseSensitive(action, "cmd");
        if (!cJSON_IsString(point_id) || !point_id->valuestring) {
            continue;
        }
        strlcpy(scene->action_points[scene->action_count],
                point_id->valuestring,
                sizeof(scene->action_points[scene->action_count]));
        scene->action_on[scene->action_count] =
            cJSON_IsString(cmd) && cmd->valuestring &&
            strcmp(cmd->valuestring, "turn_on") == 0;
        ++scene->action_count;
    }
}

static void fill_scene_actions_from_command(app_hub_scene_t *scene,
                                            const hub_command_t *command)
{
    scene->action_count = 0;
    if (!command) {
        return;
    }
    const uint8_t n = command->scene_count < APP_HUB_PAGE_SIZE ?
                      command->scene_count : APP_HUB_PAGE_SIZE;
    for (uint8_t i = 0; i < n; ++i) {
        strlcpy(scene->action_points[i], command->scene_points[i],
                sizeof(scene->action_points[i]));
        scene->action_on[i] = command->scene_values[i];
    }
    scene->action_count = n;
}

static const cJSON *document_array(const cJSON *root, const char *key)
{
    if (cJSON_IsArray(root)) {
        return root;
    }
    return cJSON_IsObject(root) ?
           cJSON_GetObjectItemCaseSensitive(root, key) : NULL;
}

static esp_err_t sync_scenes(struct app_hub *hub)
{
    http_response_t response = {0};
    esp_err_t err = request(hub, HTTP_METHOD_GET, "/api/scenes", NULL,
                            &response);
    if (err != ESP_OK || response.status != 200 || !response.body) {
        response_release(&response);
        return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
    }
    cJSON *root = cJSON_Parse(response.body);
    char *document = response.body ? strdup(response.body) : NULL;
    response_release(&response);
    const cJSON *array = document_array(root, "scenes");
    if (!cJSON_IsArray(array)) {
        cJSON_Delete(root);
        free(document);
        return ESP_ERR_INVALID_RESPONSE;
    }
    app_hub_scene_t *parsed = calloc(HUB_MAX_SCENES, sizeof(*parsed));
    if (!parsed) {
        cJSON_Delete(root);
        free(document);
        return ESP_ERR_NO_MEM;
    }
    size_t count = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, array) {
        if (count >= HUB_MAX_SCENES || !cJSON_IsObject(item)) {
            break;
        }
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        const cJSON *steps = cJSON_GetObjectItemCaseSensitive(item, "steps");
        if (!cJSON_IsString(id) || !id->valuestring) {
            continue;
        }
        strlcpy(parsed[count].id, id->valuestring, sizeof(parsed[count].id));
        strlcpy(parsed[count].name,
                cJSON_IsString(name) && name->valuestring ?
                name->valuestring : id->valuestring,
                sizeof(parsed[count].name));
        parsed[count].step_count = cJSON_IsArray(steps) ?
                                   (uint8_t)cJSON_GetArraySize(steps) : 0;
        fill_scene_actions(&parsed[count], item);
        ++count;
    }
    cJSON_Delete(root);
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        free(document);
        free(parsed);
        return ESP_ERR_TIMEOUT;
    }
    const bool keep_status =
        hub->scenes_status[0] != '\0' &&
        strcmp(hub->scenes_status, "Tap a scene to run") != 0 &&
        strcmp(hub->scenes_status, "No scenes") != 0 &&
        strcmp(hub->scenes_status, "Loading scenes...") != 0 &&
        strcmp(hub->scenes_status, "Creating scene...") != 0 &&
        strcmp(hub->scenes_status, "Deleting scene...") != 0 &&
        strcmp(hub->scenes_status, "Starting scene...") != 0;
    free(hub->scenes_json);
    hub->scenes_json = document;
    memcpy(hub->scenes, parsed, HUB_MAX_SCENES * sizeof(*parsed));
    hub->scene_count = count;
    hub->scenes_ready = true;
    if (!keep_status) {
        strlcpy(hub->scenes_status, count ? "Tap a scene to run" : "No scenes",
                sizeof(hub->scenes_status));
    }
    ++hub->scenes_generation;
    xSemaphoreGive(hub->mutex);
    free(parsed);
    return ESP_OK;
}

static esp_err_t sync_rules(struct app_hub *hub)
{
    http_response_t response = {0};
    esp_err_t err = request(hub, HTTP_METHOD_GET, "/api/rules", NULL,
                            &response);
    if (err != ESP_OK || response.status != 200 || !response.body) {
        response_release(&response);
        return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
    }
    cJSON *root = cJSON_Parse(response.body);
    const cJSON *array = document_array(root, "rules");
    if (!cJSON_IsArray(array)) {
        cJSON_Delete(root);
        response_release(&response);
        return ESP_ERR_INVALID_RESPONSE;
    }
    app_hub_rule_t *parsed = calloc(HUB_MAX_RULES, sizeof(*parsed));
    if (!parsed) {
        cJSON_Delete(root);
        response_release(&response);
        return ESP_ERR_NO_MEM;
    }
    size_t count = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, array) {
        if (count >= HUB_MAX_RULES || !cJSON_IsObject(item)) {
            break;
        }
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(item, "enabled");
        if (!cJSON_IsString(id) || !id->valuestring) {
            continue;
        }
        strlcpy(parsed[count].id, id->valuestring, sizeof(parsed[count].id));
        strlcpy(parsed[count].name,
                cJSON_IsString(name) && name->valuestring ?
                name->valuestring : id->valuestring,
                sizeof(parsed[count].name));
        parsed[count].enabled = !cJSON_IsFalse(enabled);
        ++count;
    }
    char *document = strdup(response.body);
    response_release(&response);
    cJSON_Delete(root);
    if (!document) {
        free(parsed);
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        free(document);
        free(parsed);
        return ESP_ERR_TIMEOUT;
    }
    free(hub->rules_json);
    hub->rules_json = document;
    memcpy(hub->rules, parsed, HUB_MAX_RULES * sizeof(*parsed));
    hub->rule_count = count;
    hub->rules_ready = true;
    strlcpy(hub->rules_status, count ? "Tap to enable or disable" : "No automations",
            sizeof(hub->rules_status));
    ++hub->rules_generation;
    xSemaphoreGive(hub->mutex);
    free(parsed);
    return ESP_OK;
}

static uint32_t json_u32(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_IsObject(object) ?
                        cJSON_GetObjectItemCaseSensitive(object, key) : NULL;
    return cJSON_IsNumber(item) && item->valuedouble > 0 ?
           (uint32_t)item->valuedouble : 0;
}

static esp_err_t sync_system(struct app_hub *hub)
{
    http_response_t response = {0};
    esp_err_t err = request(hub, HTTP_METHOD_GET, "/api/system", NULL,
                            &response);
    if (err != ESP_OK || response.status != 200 || !response.body) {
        response_release(&response);
        return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
    }
    cJSON *root = cJSON_Parse(response.body);
    response_release(&response);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    app_hub_system_snapshot_t *parsed = calloc(1, sizeof(*parsed));
    if (!parsed) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    *parsed = (app_hub_system_snapshot_t) {
        .ready = true,
        .hub_online = true,
        .uptime_s = json_u32(root, "uptime_s"),
    };
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *ram = cJSON_GetObjectItemCaseSensitive(root, "ram");
    const cJSON *storage = cJSON_GetObjectItemCaseSensitive(root, "storage");
    const cJSON *local_ip = cJSON_GetObjectItemCaseSensitive(root, "local_ip");
    const cJSON *net = cJSON_GetObjectItemCaseSensitive(root, "net");
    const cJSON *wifi = cJSON_IsObject(net) ?
                        cJSON_GetObjectItemCaseSensitive(net, "wifi") : NULL;
    const cJSON *wifi_ssid = cJSON_IsObject(wifi) ?
                             cJSON_GetObjectItemCaseSensitive(wifi, "ssid") : NULL;
    const cJSON *wifi_present = cJSON_IsObject(wifi) ?
                                cJSON_GetObjectItemCaseSensitive(wifi, "present") : NULL;
    const cJSON *wifi_up = cJSON_IsObject(wifi) ?
                           cJSON_GetObjectItemCaseSensitive(wifi, "up") : NULL;
    const cJSON *wifi_has_password = cJSON_IsObject(wifi) ?
                                     cJSON_GetObjectItemCaseSensitive(
                                         wifi, "has_password") : NULL;
    strlcpy(parsed->version,
            cJSON_IsString(version) && version->valuestring ?
            version->valuestring : "--", sizeof(parsed->version));
    strlcpy(parsed->ip,
            cJSON_IsString(local_ip) && local_ip->valuestring ?
            local_ip->valuestring : "", sizeof(parsed->ip));
    strlcpy(parsed->wifi_ssid,
            cJSON_IsString(wifi_ssid) && wifi_ssid->valuestring ?
            wifi_ssid->valuestring : "", sizeof(parsed->wifi_ssid));
    parsed->wifi_present = cJSON_IsTrue(wifi_present);
    parsed->wifi_connected = cJSON_IsTrue(wifi_up);
    parsed->wifi_has_password = cJSON_IsTrue(wifi_has_password);
    parsed->internal_free_kb = json_u32(ram, "internal_free") / 1024U;
    parsed->psram_free_kb = json_u32(ram, "psram_free") / 1024U;
    parsed->storage_free_kb = json_u32(storage, "free") / 1024U;
    strlcpy(parsed->status, "MiniHub healthy", sizeof(parsed->status));
    cJSON_Delete(root);
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        free(parsed);
        return ESP_ERR_TIMEOUT;
    }
    parsed->generation = hub->system.generation + 1;
    if (!parsed->ip[0]) {
        strlcpy(parsed->ip, hub->local_ip, sizeof(parsed->ip));
    }
    hub->system = *parsed;
    xSemaphoreGive(hub->mutex);
    free(parsed);
    return ESP_OK;
}

static bool remove_array_item_by_string(cJSON *array, const char *key,
                                        const char *value);

static void set_scene_result(struct app_hub *hub, const char *status)
{
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        hub->scene_pending = false;
        strlcpy(hub->scenes_status, status, sizeof(hub->scenes_status));
        ++hub->scenes_generation;
        xSemaphoreGive(hub->mutex);
    }
}

static bool process_scene_invoke(struct app_hub *hub,
                                 const hub_command_t *command)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        set_scene_result(hub, "Could not run scene");
        return false;
    }
    cJSON_AddStringToObject(root, "scene_id", command->point_id);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    http_response_t response = {0};
    esp_err_t err = payload ?
                    request(hub, HTTP_METHOD_POST, "/api/scenes/invoke",
                            payload, &response) : ESP_ERR_NO_MEM;
    free(payload);
    const bool ok = err == ESP_OK && response.status >= 200 &&
                    response.status < 300;
    response_release(&response);
    set_scene_result(hub, ok ? "Scene started" : "Scene failed");
    if (ok) {
        (void)sync_points(hub);
    }
    return ok;
}

static app_hub_scene_t *upsert_local_scene(struct app_hub *hub, const char *id,
                                           const char *name, uint8_t step_count)
{
    app_hub_scene_t *slot = NULL;
    for (size_t i = 0; i < hub->scene_count; ++i) {
        if (strcmp(hub->scenes[i].id, id) == 0) {
            slot = &hub->scenes[i];
            break;
        }
    }
    if (!slot) {
        if (hub->scene_count >= HUB_MAX_SCENES) {
            return NULL;
        }
        slot = &hub->scenes[hub->scene_count++];
        memset(slot, 0, sizeof(*slot));
        strlcpy(slot->id, id, sizeof(slot->id));
    }
    strlcpy(slot->name, name, sizeof(slot->name));
    slot->step_count = step_count;
    return slot;
}

static void drop_local_scene(struct app_hub *hub, const char *id)
{
    for (size_t i = 0; i < hub->scene_count; ++i) {
        if (strcmp(hub->scenes[i].id, id) != 0) {
            continue;
        }
        if (i + 1U < hub->scene_count) {
            memmove(&hub->scenes[i], &hub->scenes[i + 1],
                    (hub->scene_count - i - 1U) * sizeof(hub->scenes[0]));
        }
        --hub->scene_count;
        memset(&hub->scenes[hub->scene_count], 0, sizeof(hub->scenes[0]));
        return;
    }
}

static bool scene_id_present(const struct app_hub *hub, const char *id)
{
    for (size_t i = 0; i < hub->scene_count; ++i) {
        if (strcmp(hub->scenes[i].id, id) == 0) {
            return true;
        }
    }
    return false;
}

static void fail_scene_create(struct app_hub *hub, const char *id)
{
    if (id && id[0] &&
            xSemaphoreTake(hub->mutex,
                           pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        drop_local_scene(hub, id);
        ++hub->scenes_generation;
        xSemaphoreGive(hub->mutex);
    }
    set_scene_result(hub, "Scene create failed");
}

static void process_scene_create(struct app_hub *hub,
                                 const hub_command_t *command)
{
    char *document = NULL;
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        document = hub->scenes_json ? strdup(hub->scenes_json) : NULL;
        xSemaphoreGive(hub->mutex);
    }
    cJSON *root = document ? cJSON_Parse(document) : cJSON_CreateObject();
    free(document);
    if (!root) {
        fail_scene_create(hub, command->point_id);
        return;
    }
    cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "scenes");
    if (!cJSON_IsArray(array)) {
        array = cJSON_CreateArray();
        cJSON_AddItemToObject(root, "scenes", array);
    }
    cJSON *scene = cJSON_CreateObject();
    cJSON *steps = cJSON_CreateArray();
    cJSON *step = cJSON_CreateObject();
    cJSON *actions = cJSON_CreateArray();
    if (!array || !scene || !steps || !step || !actions) {
        cJSON_Delete(scene);
        cJSON_Delete(steps);
        cJSON_Delete(step);
        cJSON_Delete(actions);
        cJSON_Delete(root);
        fail_scene_create(hub, command->point_id);
        return;
    }
    char id[APP_HUB_SCENE_ID_LEN];
    strlcpy(id, command->point_id[0] ? command->point_id : "panel_scene",
            sizeof(id));
    cJSON_AddStringToObject(scene, "id", id);
    cJSON_AddStringToObject(scene, "name", command->name);
    for (size_t i = 0; i < command->scene_count; ++i) {
        cJSON *action = cJSON_CreateObject();
        cJSON_AddStringToObject(action, "point_id", command->scene_points[i]);
        cJSON_AddStringToObject(action, "cmd",
                                command->scene_values[i] ?
                                "turn_on" : "turn_off");
        cJSON_AddItemToArray(actions, action);
    }
    cJSON_AddItemToObject(step, "actions", actions);
    cJSON_AddNumberToObject(step, "loop", 1);
    cJSON_AddNumberToObject(step, "delay_after_ms", 0);
    cJSON_AddItemToArray(steps, step);
    cJSON_AddItemToObject(scene, "steps", steps);
    cJSON_AddNumberToObject(scene, "max_duration_ms", 0);
    cJSON_AddItemToArray(array, scene);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    http_response_t response = {0};
    esp_err_t err = payload ?
                    request(hub, HTTP_METHOD_POST, "/api/scenes/import",
                            payload, &response) : ESP_ERR_NO_MEM;
    free(payload);
    const bool ok = err == ESP_OK && response.status >= 200 &&
                    response.status < 300;
    ESP_LOGI(TAG, "scene import id=%s name=%s http=%d err=%s",
             id, command->name, response.status, esp_err_to_name(err));
    response_release(&response);
    if (!ok) {
        fail_scene_create(hub, id);
        return;
    }
    (void)sync_scenes(hub);
    bool missing = true;
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) ==
            pdTRUE) {
        missing = !scene_id_present(hub, id);
        if (missing) {
            app_hub_scene_t *slot = upsert_local_scene(
                hub, id, command->name,
                command->scene_count ? 1U : 0U);
            if (slot) {
                fill_scene_actions_from_command(slot, command);
            }
            ++hub->scenes_generation;
        }
        xSemaphoreGive(hub->mutex);
    }
    if (missing) {
        ESP_LOGW(TAG, "scene %s missing after import GET, keeping local copy",
                 id);
    }
    set_scene_result(hub, "Scene created");
}

static void process_scene_delete(struct app_hub *hub,
                                 const hub_command_t *command)
{
    char *document = NULL;
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        document = hub->scenes_json ? strdup(hub->scenes_json) : NULL;
        xSemaphoreGive(hub->mutex);
    }
    cJSON *root = document ? cJSON_Parse(document) : NULL;
    free(document);
    cJSON *array = root ? cJSON_GetObjectItemCaseSensitive(root, "scenes") : NULL;
    const bool removed = cJSON_IsArray(array) &&
                         remove_array_item_by_string(array, "id",
                                                     command->point_id);
    char *payload = (root && removed) ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    http_response_t response = {0};
    esp_err_t err = payload ?
                    request(hub, HTTP_METHOD_POST, "/api/scenes/import",
                            payload, &response) : ESP_ERR_INVALID_RESPONSE;
    free(payload);
    const bool ok = err == ESP_OK && response.status >= 200 &&
                    response.status < 300;
    response_release(&response);
    (void)sync_scenes(hub);
    set_scene_result(hub, ok ? "Scene deleted" : "Scene delete failed");
}

static void set_rule_result(struct app_hub *hub, const char *status)
{
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        hub->rule_pending = false;
        strlcpy(hub->rules_status, status, sizeof(hub->rules_status));
        ++hub->rules_generation;
        xSemaphoreGive(hub->mutex);
    }
}

static void process_rule_enabled(struct app_hub *hub,
                                 const hub_command_t *command)
{
    char *document = NULL;
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        document = hub->rules_json ? strdup(hub->rules_json) : NULL;
        xSemaphoreGive(hub->mutex);
    }
    cJSON *root = document ? cJSON_Parse(document) : NULL;
    free(document);
    const cJSON *array = document_array(root, "rules");
    bool found = false;
    cJSON *item;
    cJSON_ArrayForEach(item, array) {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
        if (cJSON_IsString(id) && id->valuestring &&
                strcmp(id->valuestring, command->point_id) == 0) {
            cJSON_ReplaceItemInObjectCaseSensitive(
                item, "enabled", cJSON_CreateBool(command->enabled));
            found = true;
            break;
        }
    }
    char *payload = found ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    http_response_t response = {0};
    esp_err_t err = payload ?
                    request(hub, HTTP_METHOD_POST, "/api/rules/import",
                            payload, &response) : ESP_ERR_INVALID_STATE;
    free(payload);
    const bool ok = err == ESP_OK && response.status >= 200 &&
                    response.status < 300;
    response_release(&response);
    if (ok && sync_rules(hub) == ESP_OK) {
        set_rule_result(hub, command->enabled ? "Automation enabled" :
                        "Automation disabled");
    } else {
        set_rule_result(hub, "Automation update failed");
        (void)sync_rules(hub);
    }
}

static void process_rule_delete(struct app_hub *hub,
                                const hub_command_t *command)
{
    char *document = NULL;
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        document = hub->rules_json ? strdup(hub->rules_json) : NULL;
        xSemaphoreGive(hub->mutex);
    }
    cJSON *root = document ? cJSON_Parse(document) : NULL;
    free(document);
    cJSON *array = root ? cJSON_GetObjectItemCaseSensitive(root, "rules") : NULL;
    const bool removed = cJSON_IsArray(array) &&
                         remove_array_item_by_string(array, "id",
                                                     command->point_id);
    char *payload = (root && removed) ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    http_response_t response = {0};
    esp_err_t err = payload ?
                    request(hub, HTTP_METHOD_POST, "/api/rules/import",
                            payload, &response) : ESP_ERR_INVALID_RESPONSE;
    free(payload);
    const bool ok = err == ESP_OK && response.status >= 200 &&
                    response.status < 300;
    response_release(&response);
    (void)sync_rules(hub);
    set_rule_result(hub, ok ? "Automation deleted" : "Automation delete failed");
}

static void process_rule_create(struct app_hub *hub,
                                const hub_command_t *command)
{
    char *document = NULL;
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        document = hub->rules_json ? strdup(hub->rules_json) : NULL;
        xSemaphoreGive(hub->mutex);
    }
    cJSON *root = document ? cJSON_Parse(document) : cJSON_CreateObject();
    free(document);
    if (!root) {
        set_rule_result(hub, "Automation create failed");
        return;
    }
    cJSON *array = cJSON_GetObjectItemCaseSensitive(root, "rules");
    if (!cJSON_IsArray(array)) {
        array = cJSON_CreateArray();
        cJSON_AddItemToObject(root, "rules", array);
    }
    cJSON *rule = cJSON_CreateObject();
    cJSON *when = cJSON_CreateObject();
    cJSON *then = cJSON_CreateArray();
    cJSON *action = cJSON_CreateObject();
    if (!array || !rule || !when || !then || !action) {
        cJSON_Delete(rule);
        cJSON_Delete(when);
        cJSON_Delete(then);
        cJSON_Delete(action);
        cJSON_Delete(root);
        set_rule_result(hub, "Automation create failed");
        return;
    }
    char id[APP_HUB_RULE_ID_LEN];
    snprintf(id, sizeof(id), "panel_%08lx",
             (unsigned long)xTaskGetTickCount());
    cJSON_AddStringToObject(rule, "id", id);
    cJSON_AddStringToObject(rule, "name", command->name);
    cJSON_AddBoolToObject(rule, "enabled", true);
    cJSON_AddStringToObject(when, "type", "point");
    cJSON_AddStringToObject(when, "point_id", command->point_id);
    cJSON_AddStringToObject(when, "op", "eq");
    cJSON_AddStringToObject(when, "value", command->source_value);
    cJSON_AddItemToObject(rule, "when", when);
    cJSON_AddStringToObject(action, "type", "command");
    cJSON_AddStringToObject(action, "point_id", command->target_point);
    cJSON_AddStringToObject(action, "cmd",
                            command->enabled ? "turn_on" : "turn_off");
    cJSON_AddItemToArray(then, action);
    cJSON_AddItemToObject(rule, "then", then);
    cJSON_AddItemToArray(array, rule);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    http_response_t response = {0};
    esp_err_t err = payload ?
                    request(hub, HTTP_METHOD_POST, "/api/rules/import",
                            payload, &response) : ESP_ERR_NO_MEM;
    free(payload);
    const bool ok = err == ESP_OK && response.status >= 200 &&
                    response.status < 300;
    response_release(&response);
    if (ok && sync_rules(hub) == ESP_OK) {
        set_rule_result(hub, "Automation created");
    } else {
        set_rule_result(hub, "Automation create failed");
        (void)sync_rules(hub);
    }
}

static void process_system_reboot(struct app_hub *hub)
{
    http_response_t response = {0};
    esp_err_t err = request(hub, HTTP_METHOD_POST, "/api/system/reboot",
                            "{\"confirm\":true}", &response);
    if (err == ESP_OK && response.status >= 200 && response.status < 300) {
        set_status(hub, false, "MiniHub restarting");
    }
    response_release(&response);
}

static void process_system_wifi(struct app_hub *hub,
                                const hub_command_t *command)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        set_status(hub, true, "Wi-Fi update failed");
        return;
    }
    cJSON_AddStringToObject(root, "ssid", command->wifi_ssid);
    cJSON_AddStringToObject(root, "password", command->wifi_password);
    cJSON_AddBoolToObject(root, "open", command->wifi_open);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    http_response_t response = {0};
    const esp_err_t err = payload ?
                          request(hub, HTTP_METHOD_POST, "/api/net/wifi",
                                  payload, &response) : ESP_ERR_NO_MEM;
    free(payload);
    const bool ok = err == ESP_OK && response.status >= 200 &&
                    response.status < 300;
    response_release(&response);
    set_status(hub, true, ok ? "Wi-Fi reconnecting" : "Wi-Fi update failed");
    if (ok) {
        (void)sync_system(hub);
    }
}

static void process_wifi_scan(struct app_hub *hub)
{
    wifi_scan_config_t config = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    esp_err_t err = esp_wifi_scan_start(&config, true);
    wifi_ap_record_t records[16] = {0};
    uint16_t count = sizeof(records) / sizeof(records[0]);
    if (err == ESP_OK) {
        err = esp_wifi_scan_get_ap_records(&count, records);
    }

    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return;
    }
    if (err == ESP_OK) {
        hub->wifi.count = 0;
        for (uint16_t i = 0;
                i < count && hub->wifi.count < APP_HUB_WIFI_AP_LIMIT; ++i) {
            if (records[i].ssid[0] == '\0') {
                continue;
            }
            bool duplicate = false;
            for (size_t j = 0; j < hub->wifi.count; ++j) {
                if (strcmp(hub->wifi.aps[j].ssid,
                           (const char *)records[i].ssid) == 0) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
            app_hub_wifi_ap_t *ap = &hub->wifi.aps[hub->wifi.count++];
            strlcpy(ap->ssid, (const char *)records[i].ssid, sizeof(ap->ssid));
            ap->rssi = records[i].rssi;
            ap->secured = records[i].authmode != WIFI_AUTH_OPEN;
        }
    }
    hub->wifi.scanning = false;
    ++hub->wifi.generation;
    xSemaphoreGive(hub->mutex);
}

static cJSON *adapter_slot(cJSON *root, const char *name)
{
    cJSON *adapters = cJSON_GetObjectItemCaseSensitive(root, "adapters");
    if (!cJSON_IsObject(adapters)) {
        adapters = cJSON_CreateObject();
        if (!adapters) {
            return NULL;
        }
        cJSON_AddItemToObject(root, "adapters", adapters);
    }
    cJSON *slot = cJSON_GetObjectItemCaseSensitive(adapters, name);
    if (!cJSON_IsObject(slot)) {
        slot = cJSON_CreateObject();
        if (!slot) {
            return NULL;
        }
        cJSON_ReplaceItemInObjectCaseSensitive(adapters, name, slot);
        if (cJSON_GetObjectItemCaseSensitive(adapters, name) != slot) {
            cJSON_AddItemToObject(adapters, name, slot);
        }
    }
    return slot;
}

static void json_set_bool(cJSON *object, const char *key, bool value)
{
    cJSON *item = cJSON_CreateBool(value);
    if (!cJSON_ReplaceItemInObjectCaseSensitive(object, key, item)) {
        cJSON_AddItemToObject(object, key, item);
    }
}

static void json_set_number(cJSON *object, const char *key, double value)
{
    cJSON *item = cJSON_CreateNumber(value);
    if (!cJSON_ReplaceItemInObjectCaseSensitive(object, key, item)) {
        cJSON_AddItemToObject(object, key, item);
    }
}

static void json_set_string(cJSON *object, const char *key, const char *value)
{
    cJSON *item = cJSON_CreateString(value ? value : "");
    if (!cJSON_ReplaceItemInObjectCaseSensitive(object, key, item)) {
        cJSON_AddItemToObject(object, key, item);
    }
}

static cJSON *load_adapters_document(struct app_hub *hub)
{
    http_response_t response = {0};
    esp_err_t err = request(hub, HTTP_METHOD_GET, "/api/config/adapters",
                            NULL, &response);
    cJSON *root = NULL;
    if (err == ESP_OK && response.status == 200) {
        root = cJSON_Parse(response.body);
    }
    response_release(&response);
    return root;
}

static esp_err_t save_adapters_document(struct app_hub *hub, cJSON *root)
{
    json_set_bool(root, "reload", true);
    char *payload = cJSON_PrintUnformatted(root);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }
    http_response_t response = {0};
    esp_err_t err = request(hub, HTTP_METHOD_PUT, "/api/config/adapters",
                            payload, &response);
    free(payload);
    if (err == ESP_OK && (response.status < 200 || response.status >= 300)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    response_release(&response);
    return err;
}

static cJSON *ensure_array(cJSON *object, const char *key)
{
    cJSON *array = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsArray(array)) {
        return array;
    }
    array = cJSON_CreateArray();
    if (!array) {
        return NULL;
    }
    if (!cJSON_ReplaceItemInObjectCaseSensitive(object, key, array)) {
        cJSON_AddItemToObject(object, key, array);
    }
    return array;
}

static void replace_array_item_by_string(cJSON *array, const char *key,
                                         const char *value, cJSON *replacement)
{
    int index = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        const cJSON *candidate = cJSON_GetObjectItemCaseSensitive(item, key);
        if (cJSON_IsString(candidate) && candidate->valuestring &&
                strcmp(candidate->valuestring, value) == 0) {
            cJSON_ReplaceItemInArray(array, index, replacement);
            return;
        }
        ++index;
    }
    cJSON_AddItemToArray(array, replacement);
}

static bool remove_array_item_by_string(cJSON *array, const char *key,
                                        const char *value)
{
    int index = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, array) {
        const cJSON *candidate = cJSON_GetObjectItemCaseSensitive(item, key);
        if (cJSON_IsString(candidate) && candidate->valuestring &&
                strcmp(candidate->valuestring, value) == 0) {
            cJSON_DeleteItemFromArray(array, index);
            return true;
        }
        ++index;
    }
    return false;
}

static esp_err_t configure_add_adapter(struct app_hub *hub,
                                       const app_hub_add_request_t *add)
{
    if (add->protocol != APP_HUB_PROTOCOL_MQTT &&
            add->protocol != APP_HUB_PROTOCOL_MODBUS_TCP) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    cJSON *root = load_adapters_document(hub);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    char *backup = cJSON_PrintUnformatted(root);
    if (!backup) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }
    free(hub->add_adapters_backup);
    hub->add_adapters_backup = backup;
    cJSON *slot = NULL;
    if (add->protocol == APP_HUB_PROTOCOL_MQTT) {
        slot = adapter_slot(root, "mqtt");
        cJSON *devices = slot ? ensure_array(slot, "devices") : NULL;
        cJSON *device = cJSON_CreateObject();
        if (!slot || !devices || !device) {
            cJSON_Delete(device);
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        json_set_bool(slot, "enabled", true);
        json_set_string(slot, "broker", add->host);
        json_set_number(slot, "port", add->port ? add->port : 1883);
        json_set_string(slot, "username", add->username);
        json_set_string(slot, "password", add->password);
        json_set_string(device, "point_id", add->point_id);
        json_set_string(device, "type", add->type[0] ? add->type : "switch");
        json_set_string(device, "state_topic", add->state_topic);
        json_set_string(device, "command_topic", add->command_topic);
        replace_array_item_by_string(devices, "point_id", add->point_id, device);
    } else if (add->protocol == APP_HUB_PROTOCOL_MODBUS_TCP) {
        slot = adapter_slot(root, "modbus");
        cJSON *gateways = slot ? ensure_array(slot, "gateways") : NULL;
        cJSON *gateway = cJSON_CreateObject();
        cJSON *maps = cJSON_CreateArray();
        cJSON *map = cJSON_CreateObject();
        if (!slot || !gateways || !gateway || !maps || !map) {
            cJSON_Delete(gateway);
            cJSON_Delete(maps);
            cJSON_Delete(map);
            cJSON_Delete(root);
            return ESP_ERR_NO_MEM;
        }
        json_set_bool(slot, "enabled", true);
        json_set_number(slot, "poll_ms", 2000);
        json_set_string(gateway, "id", add->point_id);
        json_set_string(gateway, "transport", "tcp");
        json_set_string(gateway, "host", add->host);
        json_set_number(gateway, "port", add->port ? add->port : 502);
        json_set_number(gateway, "timeout_ms", 1000);
        json_set_number(gateway, "unit_id", add->unit_id ? add->unit_id : 1);
        json_set_string(map, "point_id", add->point_id);
        json_set_string(map, "type", add->type[0] ? add->type : "sensor");
        json_set_number(map, "unit_id", add->unit_id ? add->unit_id : 1);
        json_set_string(map, "fc", add->function[0] ? add->function : "holding");
        json_set_number(map, "address", add->address);
        json_set_number(map, "count", 1);
        json_set_string(map, "datatype", add->datatype[0] ? add->datatype : "u16");
        json_set_number(map, "scale", add->scale == 0.0f ? 1.0f : add->scale);
        json_set_bool(map, "writable", add->writable);
        cJSON_AddItemToArray(maps, map);
        cJSON_AddItemToObject(gateway, "maps", maps);
        replace_array_item_by_string(gateways, "id", add->point_id, gateway);
    }
    esp_err_t err = save_adapters_document(hub, root);
    cJSON_Delete(root);
    return err;
}

static esp_err_t post_simple_json(struct app_hub *hub, const char *endpoint,
                                  cJSON *payload_root)
{
    char *payload = cJSON_PrintUnformatted(payload_root);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }
    http_response_t response = {0};
    esp_err_t err = request(hub, HTTP_METHOD_POST, endpoint, payload, &response);
    free(payload);
    if (err == ESP_OK && (response.status < 200 || response.status >= 300)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    response_release(&response);
    return err;
}

static bool radio_provision_ready(struct app_hub *hub,
                                  app_hub_protocol_t protocol)
{
    http_response_t response = {0};
    esp_err_t err = request(hub, HTTP_METHOD_GET,
                            "/api/radio/provision", NULL, &response);
    cJSON *root = err == ESP_OK && response.status == 200 ?
                  cJSON_Parse(response.body) : NULL;
    response_release(&response);
    if (!root) {
        return false;
    }
    const cJSON *busy = cJSON_GetObjectItemCaseSensitive(root, "busy");
    const cJSON *available = cJSON_GetObjectItemCaseSensitive(
        root, protocol == APP_HUB_PROTOCOL_ZIGBEE ?
        "zigbee_available" : "ble_mesh_available");
    const cJSON *mesh_running = cJSON_GetObjectItemCaseSensitive(
        root, "ble_mesh_running");
    const bool ready = !cJSON_IsTrue(busy) &&
                       cJSON_IsTrue(available) &&
                       (protocol != APP_HUB_PROTOCOL_BLE_MESH ||
                        cJSON_IsTrue(mesh_running));
    cJSON_Delete(root);
    return ready;
}

static esp_err_t wait_radio_ready(struct app_hub *hub,
                                  app_hub_protocol_t protocol)
{
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (hub->add_cancel_requested) {
            return ESP_ERR_INVALID_STATE;
        }
        if (radio_provision_ready(hub, protocol)) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    return ESP_ERR_TIMEOUT;
}

static void append_utf8(char *destination, size_t size, size_t *used,
                        const char *source, size_t source_limit)
{
    size_t consumed = 0;
    while (source && source[consumed] && consumed < source_limit) {
        const unsigned char lead = (unsigned char)source[consumed];
        const size_t glyph = lead < 0x80 ? 1 :
                             ((lead & 0xe0) == 0xc0 ? 2 :
                              ((lead & 0xf0) == 0xe0 ? 3 :
                               ((lead & 0xf8) == 0xf0 ? 4 : 1)));
        if (consumed + glyph > source_limit || *used + glyph >= size) {
            break;
        }
        memcpy(destination + *used, source + consumed, glyph);
        *used += glyph;
        consumed += glyph;
    }
    destination[*used] = '\0';
}

static void format_display_name(char *destination, size_t size,
                                const char *room, const char *name)
{
    size_t used = 0;
    destination[0] = '\0';
    if (room && room[0]) {
        append_utf8(destination, size, &used, room, 12);
        append_utf8(destination, size, &used, " · ", 4);
    }
    append_utf8(destination, size, &used, name, SIZE_MAX);
}

static esp_err_t rename_added_point(struct app_hub *hub, const char *point_id)
{
    if (!hub->active_add.name[0] || !point_id || !point_id[0]) {
        return ESP_OK;
    }
    char display_name[49];
    format_display_name(display_name, sizeof(display_name),
                        hub->active_add.room, hub->active_add.name);
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "point_id", point_id);
    cJSON_AddStringToObject(root, "name", display_name);
    const esp_err_t err = post_simple_json(
        hub, "/api/points/rename", root);
    cJSON_Delete(root);
    return err;
}

static const char *find_added_point(struct app_hub *hub)
{
    if ((hub->active_add.protocol == APP_HUB_PROTOCOL_MQTT ||
            hub->active_add.protocol == APP_HUB_PROTOCOL_MODBUS_TCP) &&
            hub->active_add.point_id[0]) {
        for (size_t i = 0; i < hub->point_count; ++i) {
            if (strcmp(hub->points[i].point_id,
                       hub->active_add.point_id) == 0) {
                return hub->points[i].point_id;
            }
        }
        return NULL;
    }
    if (hub->point_count > hub->add_baseline_points) {
        return hub->points[hub->point_count - 1].point_id;
    }
    return NULL;
}

static void process_matter_cancel(struct app_hub *hub,
                                  const hub_command_t *command);
static void process_matter_commission(struct app_hub *hub,
                                      const hub_command_t *command);
static void cancel_and_delete_matter_node(struct app_hub *hub,
                                          uint32_t node_id,
                                          const char *point_id);

static void rollback_add_adapter(struct app_hub *hub)
{
    const app_hub_add_request_t *add = &hub->active_add;
    if (add->protocol != APP_HUB_PROTOCOL_MQTT &&
            add->protocol != APP_HUB_PROTOCOL_MODBUS_TCP) {
        return;
    }
    if (hub->add_adapters_backup) {
        cJSON *original = cJSON_Parse(hub->add_adapters_backup);
        if (original) {
            (void)save_adapters_document(hub, original);
            cJSON_Delete(original);
        }
        free(hub->add_adapters_backup);
        hub->add_adapters_backup = NULL;
        return;
    }
    if ((add->protocol != APP_HUB_PROTOCOL_MQTT &&
            add->protocol != APP_HUB_PROTOCOL_MODBUS_TCP) ||
            !add->point_id[0]) {
        return;
    }
    cJSON *root = load_adapters_document(hub);
    const char *slot_name = add->protocol == APP_HUB_PROTOCOL_MQTT ?
                            "mqtt" : "modbus";
    const char *array_name = add->protocol == APP_HUB_PROTOCOL_MQTT ?
                             "devices" : "gateways";
    cJSON *slot = root ? adapter_slot(root, slot_name) : NULL;
    cJSON *array = slot ? cJSON_GetObjectItemCaseSensitive(
                             slot, array_name) : NULL;
    if (cJSON_IsArray(array) &&
            remove_array_item_by_string(array,
                                        add->protocol == APP_HUB_PROTOCOL_MQTT ?
                                        "point_id" : "id",
                                        add->point_id)) {
        (void)save_adapters_document(hub, root);
    }
    cJSON_Delete(root);
}

static void process_add_cancel(struct app_hub *hub,
                               const hub_command_t *command)
{
    if (hub->add.protocol == APP_HUB_PROTOCOL_MATTER) {
        process_matter_cancel(hub, command);
    } else if (hub->add.protocol == APP_HUB_PROTOCOL_ZIGBEE ||
               hub->add.protocol == APP_HUB_PROTOCOL_BLE_MESH) {
        cJSON *permit = cJSON_CreateObject();
        if (permit) {
            cJSON_AddNumberToObject(permit, "seconds", 0);
            (void)post_simple_json(
                hub, hub->add.protocol == APP_HUB_PROTOCOL_ZIGBEE ?
                "/api/zigbee/permit_join" :
                "/api/ble_mesh/permit_provision", permit);
            cJSON_Delete(permit);
        }
    } else {
        rollback_add_adapter(hub);
    }
    hub->add_cancel_requested = false;
    set_add_status(hub, APP_HUB_ADD_CANCELLED, 0, "Add cancelled", NULL);
}

static void process_add_device(struct app_hub *hub,
                               const app_hub_add_request_t *add)
{
    set_add_status(hub, APP_HUB_ADD_PREPARING, 10, "Preparing adapter", NULL);
    free(hub->add_adapters_backup);
    hub->add_adapters_backup = NULL;
    hub->active_add = *add;
    hub->add_baseline_points = hub->point_count;
    esp_err_t err = ESP_OK;
    if (hub->add_cancel_requested) {
        set_add_status(hub, APP_HUB_ADD_CANCELLED, 0, "Add cancelled", NULL);
        return;
    }
    if ((add->protocol == APP_HUB_PROTOCOL_ZIGBEE ||
            add->protocol == APP_HUB_PROTOCOL_BLE_MESH) &&
            (hub->commission_active ||
             hub->commission.state == APP_HUB_COMMISSION_RUNNING)) {
        set_add_status(hub, APP_HUB_ADD_FAILED, 0,
                       "Matter pairing is in progress", NULL);
        return;
    }
    if (add->protocol == APP_HUB_PROTOCOL_MATTER) {
        if (!app_hub_matter_pairing_code_ok(add->pairing_code)) {
            set_add_status(hub, APP_HUB_ADD_FAILED, 0,
                           "Invalid Matter pairing code", NULL);
            return;
        }
        hub_command_t command = { .kind = HUB_CMD_MATTER_COMMISSION };
        strlcpy(command.point_id, add->pairing_code, sizeof(command.point_id));
        process_matter_commission(hub, &command);
        if (hub->commission.state == APP_HUB_COMMISSION_FAILED) {
            err = ESP_FAIL;
        }
    } else {
        const bool radio = add->protocol == APP_HUB_PROTOCOL_ZIGBEE ||
                           add->protocol == APP_HUB_PROTOCOL_BLE_MESH;
        if (radio && !radio_provision_ready(hub, add->protocol)) {
            set_add_status(hub, APP_HUB_ADD_FAILED, 0,
                           add->protocol == APP_HUB_PROTOCOL_ZIGBEE ?
                           "Zigbee provisioning is unavailable" :
                           "BLE Mesh provisioning is unavailable",
                           NULL);
            return;
        }
        if (!radio) {
            err = configure_add_adapter(hub, add);
        }
        if (err == ESP_OK && radio) {
            err = wait_radio_ready(hub, add->protocol);
            if (add->protocol == APP_HUB_PROTOCOL_ZIGBEE &&
                    err == ESP_OK &&
                    add->ieee[0] && add->install_code[0]) {
                cJSON *install = cJSON_CreateObject();
                cJSON_AddStringToObject(install, "ieee", add->ieee);
                cJSON_AddStringToObject(install, "install_code", add->install_code);
                err = post_simple_json(hub, "/api/zigbee/install_code", install);
                cJSON_Delete(install);
            }
            if (err == ESP_OK && !hub->add_cancel_requested) {
                cJSON *permit = cJSON_CreateObject();
                cJSON_AddNumberToObject(permit, "seconds", 120);
                err = post_simple_json(
                    hub, add->protocol == APP_HUB_PROTOCOL_ZIGBEE ?
                    "/api/zigbee/permit_join" :
                    "/api/ble_mesh/permit_provision", permit);
                cJSON_Delete(permit);
            }
        }
    }
    if (hub->add_cancel_requested) {
        set_add_status(hub, APP_HUB_ADD_CANCELLED, 0, "Add cancelled", NULL);
        return;
    }
    if (err != ESP_OK) {
        rollback_add_adapter(hub);
        set_add_status(hub, APP_HUB_ADD_FAILED, 0,
                       "Could not start device add", NULL);
        return;
    }
    hub->add_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(
                           add->protocol == APP_HUB_PROTOCOL_MATTER ?
                           HUB_MATTER_ADD_TIMEOUT_MS : 120000);
    set_add_status(hub, APP_HUB_ADD_WAITING, 35,
                   add->protocol == APP_HUB_PROTOCOL_MQTT ||
                   add->protocol == APP_HUB_PROTOCOL_MODBUS_TCP ?
                   "Waiting for adapter reload" : "Waiting for device", NULL);
}

static void update_add_progress(struct app_hub *hub)
{
    if (hub->add_cancel_requested) {
        return;
    }
    if (hub->add.state != APP_HUB_ADD_WAITING) {
        return;
    }
    if (hub->add.protocol == APP_HUB_PROTOCOL_MATTER) {
        if (hub->commission.state == APP_HUB_COMMISSION_FAILED) {
            set_add_status(hub, APP_HUB_ADD_FAILED, 0,
                           hub->commission.error[0] ? hub->commission.error :
                           "Matter commissioning failed", NULL);
            return;
        }
        if (hub->commission.state != APP_HUB_COMMISSION_OK) {
            if ((int32_t)(xTaskGetTickCount() - hub->add_deadline) >= 0) {
                cancel_and_delete_matter_node(hub, hub->commission.node_id, NULL);
                set_add_status(hub, APP_HUB_ADD_FAILED, 0,
                               hub->commission.error[0] ? hub->commission.error :
                               "Device add timed out", NULL);
            }
            return;
        }
    }
    const char *point_id = find_added_point(hub);
    if (point_id) {
        const esp_err_t rename_err = rename_added_point(hub, point_id);
        free(hub->add_adapters_backup);
        hub->add_adapters_backup = NULL;
        set_add_status(hub, APP_HUB_ADD_OK, 100,
                       rename_err == ESP_OK ? "Device added" :
                       "Added; name could not be saved", point_id);
        (void)sync_points(hub);
    } else if ((int32_t)(xTaskGetTickCount() - hub->add_deadline) >= 0) {
        rollback_add_adapter(hub);
        set_add_status(hub, APP_HUB_ADD_FAILED, 0, "Device add timed out", NULL);
    }
}

static void cancel_and_delete_matter_node(struct app_hub *hub,
                                          uint32_t node_id,
                                          const char *point_id);

static app_hub_commission_stage_t parse_commission_stage(const char *stage)
{
    if (!stage) {
        return APP_HUB_COMMISSION_STAGE_NONE;
    }
    if (strcmp(stage, "discovering") == 0) {
        return APP_HUB_COMMISSION_STAGE_DISCOVERING;
    }
    if (strcmp(stage, "binding") == 0) {
        return APP_HUB_COMMISSION_STAGE_BINDING;
    }
    if (strcmp(stage, "activating") == 0) {
        return APP_HUB_COMMISSION_STAGE_ACTIVATING;
    }
    return APP_HUB_COMMISSION_STAGE_NONE;
}

static void sync_matter_status(struct app_hub *hub)
{
    http_response_t response = {0};
    if (request(hub, HTTP_METHOD_GET, "/api/matter/status", NULL,
                &response) != ESP_OK) {
        return;
    }
    if (response.status != 200 || !response.body) {
        response_release(&response);
        return;
    }
    cJSON *root = cJSON_Parse(response.body);
    response_release(&response);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return;
    }

    const cJSON *comm = cJSON_GetObjectItemCaseSensitive(root, "commission");
    const cJSON *stage = cJSON_GetObjectItemCaseSensitive(root, "commission_stage");
    const cJSON *node = cJSON_GetObjectItemCaseSensitive(root, "commission_node_id");
    const cJSON *last_err = cJSON_GetObjectItemCaseSensitive(root, "last_error");
    const cJSON *res_err = cJSON_GetObjectItemCaseSensitive(root, "commission_result");

    app_hub_commission_state_t state = APP_HUB_COMMISSION_IDLE;
    if (cJSON_IsString(comm) && comm->valuestring) {
        if (strcmp(comm->valuestring, "running") == 0) {
            state = APP_HUB_COMMISSION_RUNNING;
        } else if (strcmp(comm->valuestring, "ok") == 0) {
            state = APP_HUB_COMMISSION_OK;
        } else if (strcmp(comm->valuestring, "failed") == 0) {
            state = APP_HUB_COMMISSION_FAILED;
        }
    }
    const app_hub_commission_stage_t parsed_stage =
        cJSON_IsString(stage) ? parse_commission_stage(stage->valuestring) :
        APP_HUB_COMMISSION_STAGE_NONE;
    const uint32_t node_id = cJSON_IsNumber(node) ?
                             (uint32_t)node->valuedouble : 0U;
    const char *err_str = "";
    if (cJSON_IsString(last_err) && last_err->valuestring &&
            last_err->valuestring[0]) {
        err_str = last_err->valuestring;
    } else if (cJSON_IsString(res_err) && res_err->valuestring &&
               res_err->valuestring[0]) {
        err_str = res_err->valuestring;
    }

    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        hub->commission.state = state;
        hub->commission.stage = parsed_stage;
        if (node_id > 0) {
            hub->commission.node_id = node_id;
        }
        strlcpy(hub->commission.error, err_str, sizeof(hub->commission.error));
        if (state != APP_HUB_COMMISSION_RUNNING) {
            hub->commission_active = false;
        }
        ++hub->generation;
        xSemaphoreGive(hub->mutex);
    }
    cJSON_Delete(root);
}

static void process_matter_commission(struct app_hub *hub,
                                      const hub_command_t *command)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    cJSON_AddStringToObject(root, "pairing_code", command->point_id);
    cJSON_AddStringToObject(root, "endpoints", "1");
    cJSON_AddBoolToObject(root, "merge", true);
    cJSON_AddBoolToObject(root, "reload", true);
    /* Official MiniHub resolves omitted BLE-Wi-Fi credentials from its own
     * provisioning store, so the panel does not need direct Wi-Fi access. */
    cJSON_AddStringToObject(root, "discovery", "ble_wifi");
    cJSON_AddStringToObject(root, "transport", "wifi");
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return;
    }

    http_response_t response = {0};
    esp_err_t err = request(hub, HTTP_METHOD_POST, "/api/matter/devices",
                            payload, &response);
    free(payload);

    bool started = false;
    uint32_t node_id = 0;
    char err_msg[APP_HUB_STATUS_LEN] = "Commission request failed";
    if (err == ESP_OK && response.body) {
        cJSON *resp = cJSON_Parse(response.body);
        if (cJSON_IsObject(resp)) {
            const cJSON *res = cJSON_GetObjectItemCaseSensitive(resp, "result");
            const cJSON *comm = cJSON_GetObjectItemCaseSensitive(resp, "commission");
            const cJSON *nid = cJSON_GetObjectItemCaseSensitive(resp, "node_id");
            const cJSON *msg = cJSON_GetObjectItemCaseSensitive(resp, "message");
            if (cJSON_IsNumber(nid)) {
                node_id = (uint32_t)nid->valuedouble;
            }
            if (cJSON_IsString(res) && res->valuestring &&
                    strcmp(res->valuestring, "ok") == 0 &&
                    cJSON_IsString(comm) && comm->valuestring &&
                    strcmp(comm->valuestring, "started") == 0) {
                started = true;
            } else if (cJSON_IsString(msg) && msg->valuestring &&
                       msg->valuestring[0]) {
                strlcpy(err_msg, msg->valuestring, sizeof(err_msg));
            } else if (cJSON_IsString(comm) && comm->valuestring &&
                       comm->valuestring[0]) {
                strlcpy(err_msg, comm->valuestring, sizeof(err_msg));
            }
        }
        cJSON_Delete(resp);
    }
    response_release(&response);

    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        hub->commission.node_id = node_id;
        if (started) {
            hub->commission.state = APP_HUB_COMMISSION_RUNNING;
            hub->commission.stage = APP_HUB_COMMISSION_STAGE_DISCOVERING;
            hub->commission.error[0] = '\0';
            hub->commission_active = true;
        } else {
            hub->commission.state = APP_HUB_COMMISSION_FAILED;
            hub->commission.stage = APP_HUB_COMMISSION_STAGE_NONE;
            strlcpy(hub->commission.error, err_msg,
                    sizeof(hub->commission.error));
            hub->commission_active = false;
        }
        ++hub->generation;
        xSemaphoreGive(hub->mutex);
    }
}

static void cancel_and_delete_matter_node(struct app_hub *hub,
                                          uint32_t node_id,
                                          const char *point_id)
{
    if (node_id >= 2) {
        cJSON *croot = cJSON_CreateObject();
        if (croot) {
            cJSON_AddNumberToObject(croot, "node_id", (double)node_id);
            cJSON_AddBoolToObject(croot, "cancel", true);
            char *cpayload = cJSON_PrintUnformatted(croot);
            cJSON_Delete(croot);
            if (cpayload) {
                http_response_t cresp;
                (void)request(hub, HTTP_METHOD_POST, "/api/matter/commission",
                              cpayload, &cresp);
                response_release(&cresp);
                free(cpayload);
            }
        }
    }

    char resolved_point[APP_HUB_POINT_ID_LEN] = {0};
    if (point_id && point_id[0] != '\0') {
        strlcpy(resolved_point, point_id, sizeof(resolved_point));
    } else if (node_id >= 2) {
        snprintf(resolved_point, sizeof(resolved_point),
                 "wifi.matter_n%lu_ep1", (unsigned long)node_id);
    }

    if (resolved_point[0] != '\0') {
        cJSON *droot = cJSON_CreateObject();
        if (droot) {
            cJSON_AddStringToObject(droot, "point_id", resolved_point);
            cJSON_AddBoolToObject(droot, "reload", true);
            char *dpayload = cJSON_PrintUnformatted(droot);
            cJSON_Delete(droot);
            if (dpayload) {
                http_response_t dresp;
                (void)request(hub, HTTP_METHOD_POST, "/api/points/remove",
                              dpayload, &dresp);
                response_release(&dresp);
                free(dpayload);
            }
        }
    }
}

static void process_matter_cancel(struct app_hub *hub,
                                  const hub_command_t *command)
{
    uint32_t node_id = (uint32_t)command->value;
    if (node_id < 2 &&
            xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        node_id = hub->commission.node_id;
        xSemaphoreGive(hub->mutex);
    }
    cancel_and_delete_matter_node(hub, node_id, NULL);
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        hub->commission.state = APP_HUB_COMMISSION_IDLE;
        hub->commission.stage = APP_HUB_COMMISSION_STAGE_NONE;
        hub->commission.error[0] = '\0';
        hub->commission_active = false;
        ++hub->generation;
        xSemaphoreGive(hub->mutex);
    }
}

static void process_delete_point(struct app_hub *hub,
                                 const hub_command_t *command)
{
    const uint32_t node_id = (uint32_t)command->value;
    cancel_and_delete_matter_node(hub, node_id, command->point_id);
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        if (node_id >= 2 && hub->commission.node_id == node_id) {
            hub->commission.state = APP_HUB_COMMISSION_IDLE;
            hub->commission.stage = APP_HUB_COMMISSION_STAGE_NONE;
            hub->commission.error[0] = '\0';
            hub->commission_active = false;
        }
        ++hub->generation;
        xSemaphoreGive(hub->mutex);
    }
}

static esp_err_t process_command(struct app_hub *hub, const hub_command_t *command)
{
    app_hub_point_t point = {0};
    bool found = false;
    if (xSemaphoreTake(hub->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    for (size_t i = 0; i < hub->point_count; ++i) {
        if (strcmp(hub->points[i].point_id, command->point_id) == 0) {
            point = hub->points[i];
            found = true;
            break;
        }
    }
    xSemaphoreGive(hub->mutex);
    if (!found || !point.controllable) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    const char *endpoint = "/api/devices/control";
    if (command->has_value) {
        cJSON_AddStringToObject(root, "device", point.type);
        cJSON_AddStringToObject(root, "parameter", command->parameter);
        cJSON_AddNumberToObject(root, "value", command->value);
        cJSON_AddStringToObject(root, "point_id", point.point_id);
    } else {
        /* Matter lights have no props.params.power, so /api/devices/control
         * ignores point_id and hits the first catalog light. */
        endpoint = "/api/command";
        cJSON_AddStringToObject(root, "point_id", point.point_id);
        cJSON_AddStringToObject(root, "cmd", point.on ? "turn_off" : "turn_on");
    }
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }

    http_response_t response = {0};
    esp_err_t err = request(hub, HTTP_METHOD_POST, endpoint, payload, &response);
    free(payload);
    if (err == ESP_OK && response.status != 200) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    response_release(&response);
    if (err != ESP_OK) {
        set_status(hub, false, "Device command failed");
    }
    return err;
}

static void load_connection_config(struct app_hub *hub)
{
    hub->role = APP_HUB_ROLE_HUB;
    strlcpy(hub->base_url, HUB_LOCAL_URL, sizeof(hub->base_url));
    nvs_handle_t nvs;
    if (nvs_open(HUB_NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t role = APP_HUB_ROLE_HUB;
        (void)nvs_get_u8(nvs, HUB_NVS_ROLE, &role);
        if (role == APP_HUB_ROLE_PANEL) {
            hub->role = APP_HUB_ROLE_PANEL;
            size_t url_len = sizeof(hub->base_url);
            if (nvs_get_str(nvs, HUB_NVS_REMOTE_URL, hub->base_url,
                            &url_len) != ESP_OK || hub->base_url[0] == '\0') {
                hub->role = APP_HUB_ROLE_HUB;
                strlcpy(hub->base_url, HUB_LOCAL_URL, sizeof(hub->base_url));
            }
        }
        size_t token_len = sizeof(hub->api_token);
        (void)nvs_get_str(nvs, HUB_NVS_TOKEN, hub->api_token, &token_len);
        nvs_close(nvs);
    }
}

static void hub_worker(void *arg)
{
    struct app_hub *hub = arg;
    uint8_t metadata_cycle = 0;
    hub_command_t *command = heap_caps_calloc(
        1, sizeof(*command), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!command) {
        command = calloc(1, sizeof(*command));
    }
    if (!command) {
        ESP_LOGE(TAG, "hub_client command buffers");
        free(command);
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        while (xQueueReceive(hub->command_queue, command, 0) == pdTRUE) {
            if (command->kind == HUB_CMD_MATTER_COMMISSION) {
                process_matter_commission(hub, command);
                sync_points(hub);
            } else if (command->kind == HUB_CMD_MATTER_CANCEL) {
                process_matter_cancel(hub, command);
                sync_points(hub);
            } else if (command->kind == HUB_CMD_ADD_DEVICE) {
                process_add_device(hub, &command->add);
            } else if (command->kind == HUB_CMD_ADD_CANCEL) {
                process_add_cancel(hub, command);
            } else if (command->kind == HUB_CMD_DELETE_POINT) {
                process_delete_point(hub, command);
                sync_points(hub);
            } else if (command->kind == HUB_CMD_SCENE_INVOKE) {
                process_scene_invoke(hub, command);
            } else if (command->kind == HUB_CMD_SCENE_CREATE) {
                process_scene_create(hub, command);
            } else if (command->kind == HUB_CMD_SCENE_DELETE) {
                process_scene_delete(hub, command);
            } else if (command->kind == HUB_CMD_RULE_SET_ENABLED) {
                process_rule_enabled(hub, command);
            } else if (command->kind == HUB_CMD_RULE_CREATE) {
                process_rule_create(hub, command);
            } else if (command->kind == HUB_CMD_RULE_DELETE) {
                process_rule_delete(hub, command);
            } else if (command->kind == HUB_CMD_WIFI_SCAN) {
                process_wifi_scan(hub);
            } else if (command->kind == HUB_CMD_SYSTEM_WIFI) {
                process_system_wifi(hub, command);
            } else if (command->kind == HUB_CMD_SYSTEM_REBOOT) {
                process_system_reboot(hub);
            } else if (process_command(hub, command) == ESP_OK) {
                sync_points(hub);
            }
        }

        if (hub->commission_active) {
            sync_matter_status(hub);
        }
        sync_points(hub);
        update_add_progress(hub);
        if (metadata_cycle++ == 0) {
            (void)sync_scenes(hub);
            (void)sync_rules(hub);
            (void)sync_system(hub);
        }
        if (metadata_cycle >= HUB_METADATA_SYNC_CYCLES) {
            metadata_cycle = 0;
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(
                             hub->commission_active ? 1200 : HUB_SYNC_INTERVAL_MS));
    }
}

app_hub_result_t app_hub_start(app_hub_handle_t *out_handle)
{
    if (!out_handle) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    *out_handle = NULL;
    struct app_hub *hub = heap_caps_calloc(
        1, sizeof(*hub), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!hub) {
        hub = calloc(1, sizeof(*hub));
    }
    if (!hub) {
        return APP_HUB_ERR_NO_MEMORY;
    }
    hub->mutex = xSemaphoreCreateMutex();
    hub->command_queue = xQueueCreate(HUB_COMMAND_QUEUE_LENGTH,
                                      sizeof(hub_command_t));
    if (!hub->mutex || !hub->command_queue) {
        if (hub->mutex) {
            vSemaphoreDelete(hub->mutex);
        }
        if (hub->command_queue) {
            vQueueDelete(hub->command_queue);
        }
        free(hub);
        return APP_HUB_ERR_NO_MEMORY;
    }
    load_connection_config(hub);
    strlcpy(hub->status, "Connecting to MiniHub", sizeof(hub->status));
    hub->add.state = APP_HUB_ADD_IDLE;
    hub->add.generation = 1;
    strlcpy(hub->add.status, "Choose a protocol", sizeof(hub->add.status));
#if CONFIG_MINIHUB_MATTER_ADAPTER
    hub->add.capabilities |= APP_HUB_CAP_MATTER;
#endif
#if CONFIG_MINIHUB_ZIGBEE_SDK
    hub->add.capabilities |= APP_HUB_CAP_ZIGBEE;
#endif
#if CONFIG_MINIHUB_BLE_MESH_SDK
    hub->add.capabilities |= APP_HUB_CAP_BLE_MESH;
#endif
#if CONFIG_MINIHUB_MQTT_ADAPTER
    hub->add.capabilities |= APP_HUB_CAP_MQTT;
#endif
#if CONFIG_MINIHUB_MODBUS
    hub->add.capabilities |= APP_HUB_CAP_MODBUS_TCP;
#endif
    strlcpy(hub->scenes_status, "Loading scenes...",
            sizeof(hub->scenes_status));
    strlcpy(hub->rules_status, "Loading automations...",
            sizeof(hub->rules_status));
    strlcpy(hub->system.status, "Loading system status...",
            sizeof(hub->system.status));
    if (xTaskCreateWithCaps(hub_worker, "hub_client", 8192, hub, 5,
                            &hub->worker,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        free(hub);
        return APP_HUB_ERR_NO_MEMORY;
    }
    *out_handle = hub;
    return APP_HUB_OK;
}

app_hub_result_t app_hub_snapshot(app_hub_handle_t handle, size_t page,
                                  app_hub_snapshot_t *out_snapshot)
{
    if (!handle || !out_snapshot) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    *out_snapshot = (app_hub_snapshot_t) {
        .generation = handle->generation,
        .total_points = handle->point_count,
        .online_points = handle->online_count,
        .connected = handle->hub_connected,
        .role = handle->role,
    };
    strlcpy(out_snapshot->base_url, handle->base_url,
            sizeof(out_snapshot->base_url));
    strlcpy(out_snapshot->status, handle->status, sizeof(out_snapshot->status));
    out_snapshot->page_count =
        (handle->point_count + 1 + APP_HUB_PAGE_SIZE - 1) / APP_HUB_PAGE_SIZE;
    if (out_snapshot->page_count == 0) {
        out_snapshot->page_count = 1;
    }
    if (page >= out_snapshot->page_count) {
        page = out_snapshot->page_count - 1;
    }
    out_snapshot->page = page;
    const size_t start = page * APP_HUB_PAGE_SIZE;
    for (size_t i = 0; i < APP_HUB_PAGE_SIZE && start + i < handle->point_count;
            ++i) {
        out_snapshot->points[i] = handle->points[start + i];
        ++out_snapshot->visible_points;
    }
    xSemaphoreGive(handle->mutex);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_toggle(app_hub_handle_t handle, const char *point_id)
{
    if (!handle || !point_id || point_id[0] == '\0') {
        return APP_HUB_ERR_INVALID_ARG;
    }
    hub_command_t command = {0};
    strlcpy(command.point_id, point_id, sizeof(command.point_id));
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_set_parameter(app_hub_handle_t handle,
                                       const char *point_id,
                                       const char *parameter, int32_t value)
{
    if (!handle || !point_id || point_id[0] == '\0' || !parameter ||
            parameter[0] == '\0') {
        return APP_HUB_ERR_INVALID_ARG;
    }
    hub_command_t command = {
        .value = value,
        .has_value = true,
    };
    strlcpy(command.point_id, point_id, sizeof(command.point_id));
    strlcpy(command.parameter, parameter, sizeof(command.parameter));
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_refresh(app_hub_handle_t handle)
{
    if (!handle) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_wifi_scan(app_hub_handle_t handle)
{
    if (!handle) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(handle->mutex,
                       pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    if (handle->wifi.scanning) {
        xSemaphoreGive(handle->mutex);
        return APP_HUB_OK;
    }
    handle->wifi.scanning = true;
    handle->wifi.count = 0;
    ++handle->wifi.generation;
    xSemaphoreGive(handle->mutex);

    hub_command_t command = { .kind = HUB_CMD_WIFI_SCAN };
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        if (xSemaphoreTake(handle->mutex,
                           pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            handle->wifi.scanning = false;
            ++handle->wifi.generation;
            xSemaphoreGive(handle->mutex);
        }
        return APP_HUB_ERR_BUSY;
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_wifi_snapshot(
    app_hub_handle_t handle, app_hub_wifi_snapshot_t *out_snapshot)
{
    if (!handle || !out_snapshot) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(handle->mutex,
                       pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    *out_snapshot = handle->wifi;
    xSemaphoreGive(handle->mutex);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_set_role(app_hub_handle_t handle,
                                  app_hub_role_t role)
{
    if (!handle || role != APP_HUB_ROLE_HUB) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    if (save_connection_config(role, "", "") != ESP_OK) {
        return APP_HUB_ERR_INTERNAL;
    }
    if (xSemaphoreTake(handle->mutex,
                       pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    handle->role = APP_HUB_ROLE_HUB;
    strlcpy(handle->base_url, HUB_LOCAL_URL, sizeof(handle->base_url));
    handle->api_token[0] = '\0';
    handle->cookie[0] = '\0';
    handle->hub_connected = false;
    strlcpy(handle->status, "Connecting to local MiniHub",
            sizeof(handle->status));
    ++handle->generation;
    xSemaphoreGive(handle->mutex);
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_set_remote(app_hub_handle_t handle,
                                    const char *base_url,
                                    const char *bearer_token)
{
    if (!handle || !base_url || !bearer_token || !base_url[0] ||
            !bearer_token[0] ||
            strlen(base_url) >= APP_HUB_REMOTE_URL_LEN ||
            strlen(bearer_token) >= APP_HUB_TOKEN_LEN ||
            (strncmp(base_url, "http://", 7) != 0 &&
             strncmp(base_url, "https://", 8) != 0)) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    char normalized[APP_HUB_REMOTE_URL_LEN];
    strlcpy(normalized, base_url, sizeof(normalized));
    size_t length = strlen(normalized);
    while (length > 0 && normalized[length - 1] == '/') {
        normalized[--length] = '\0';
    }
    if (length == 0 ||
            save_connection_config(APP_HUB_ROLE_PANEL, normalized,
                                   bearer_token) != ESP_OK) {
        return APP_HUB_ERR_INTERNAL;
    }
    if (xSemaphoreTake(handle->mutex,
                       pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    handle->role = APP_HUB_ROLE_PANEL;
    strlcpy(handle->base_url, normalized, sizeof(handle->base_url));
    strlcpy(handle->api_token, bearer_token, sizeof(handle->api_token));
    handle->cookie[0] = '\0';
    handle->hub_connected = false;
    handle->point_count = 0;
    handle->online_count = 0;
    strlcpy(handle->status, "Connecting to remote MiniHub",
            sizeof(handle->status));
    ++handle->generation;
    xSemaphoreGive(handle->mutex);
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_matter_commission(app_hub_handle_t handle,
                                           const char *pairing_code)
{
    if (!handle || !pairing_code || pairing_code[0] == '\0') {
        return APP_HUB_ERR_INVALID_ARG;
    }
    hub_command_t command = {
        .kind = HUB_CMD_MATTER_COMMISSION,
    };
    strlcpy(command.point_id, pairing_code, sizeof(command.point_id));
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        handle->commission.state = APP_HUB_COMMISSION_RUNNING;
        handle->commission.stage = APP_HUB_COMMISSION_STAGE_DISCOVERING;
        handle->commission.node_id = 0;
        handle->commission.error[0] = '\0';
        handle->commission_active = true;
        ++handle->generation;
        xSemaphoreGive(handle->mutex);
    }
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        if (xSemaphoreTake(handle->mutex,
                           pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            handle->commission.state = APP_HUB_COMMISSION_FAILED;
            handle->commission.stage = APP_HUB_COMMISSION_STAGE_NONE;
            strlcpy(handle->commission.error, "Command queue full",
                    sizeof(handle->commission.error));
            handle->commission_active = false;
            ++handle->generation;
            xSemaphoreGive(handle->mutex);
        }
        return APP_HUB_ERR_BUSY;
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_matter_commission_snapshot(
    app_hub_handle_t handle, app_hub_commission_snapshot_t *out_snapshot)
{
    if (!handle || !out_snapshot) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    *out_snapshot = handle->commission;
    xSemaphoreGive(handle->mutex);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_matter_cancel(app_hub_handle_t handle,
                                       uint32_t node_id)
{
    if (!handle) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    hub_command_t command = {
        .kind = HUB_CMD_MATTER_CANCEL,
        .value = (int32_t)node_id,
    };
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        handle->commission_active = false;
        handle->commission.state = APP_HUB_COMMISSION_IDLE;
        handle->commission.stage = APP_HUB_COMMISSION_STAGE_NONE;
        ++handle->generation;
        xSemaphoreGive(handle->mutex);
    }
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_add_snapshot(app_hub_handle_t handle,
                                      app_hub_add_snapshot_t *out_snapshot)
{
    if (!handle || !out_snapshot) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    *out_snapshot = handle->add;
    xSemaphoreGive(handle->mutex);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_add_device(app_hub_handle_t handle,
                                    const app_hub_add_request_t *add)
{
    if (!handle || !add || add->protocol >= APP_HUB_PROTOCOL_COUNT) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    if ((handle->add.capabilities & (1U << add->protocol)) == 0) {
        return APP_HUB_ERR_NOT_SUPPORTED;
    }
    if (!add->name[0] ||
            (add->protocol == APP_HUB_PROTOCOL_MATTER &&
             !app_hub_matter_pairing_code_ok(add->pairing_code)) ||
            (add->protocol == APP_HUB_PROTOCOL_ZIGBEE &&
             add->install_code[0] && !add->ieee[0]) ||
            (add->protocol == APP_HUB_PROTOCOL_MQTT &&
             (!add->host[0] || !add->point_id[0] ||
              !add->state_topic[0])) ||
            (add->protocol == APP_HUB_PROTOCOL_MODBUS_TCP &&
             (!add->host[0] || !add->point_id[0] ||
              add->unit_id == 0 || add->unit_id > 247))) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    if (handle->add.state == APP_HUB_ADD_PREPARING ||
            handle->add.state == APP_HUB_ADD_WAITING) {
        return APP_HUB_ERR_BUSY;
    }
    hub_command_t command = {
        .kind = HUB_CMD_ADD_DEVICE,
        .add = *add,
    };
    handle->add_cancel_requested = false;
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        handle->add.protocol = add->protocol;
        handle->add.state = APP_HUB_ADD_PREPARING;
        handle->add.progress = 5;
        handle->add.point_id[0] = '\0';
        strlcpy(handle->add.status, "Queued", sizeof(handle->add.status));
        ++handle->add.generation;
        xSemaphoreGive(handle->mutex);
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_add_cancel(app_hub_handle_t handle)
{
    if (!handle) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    handle->add_cancel_requested = true;
    set_add_status(handle, APP_HUB_ADD_CANCELLED, 0, "Add cancelled", NULL);
    hub_command_t command = { .kind = HUB_CMD_ADD_CANCEL };
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_delete_point(app_hub_handle_t handle,
                                      const char *point_id, uint32_t node_id)
{
    if (!handle || !point_id || point_id[0] == '\0') {
        return APP_HUB_ERR_INVALID_ARG;
    }
    if (strcmp(point_id, "mosaico.imu") == 0) {
        return APP_HUB_ERR_NOT_SUPPORTED;
    }
    hub_command_t command = {
        .kind = HUB_CMD_DELETE_POINT,
        .value = (int32_t)node_id,
    };
    strlcpy(command.point_id, point_id, sizeof(command.point_id));
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_scenes_snapshot(
    app_hub_handle_t handle, size_t page,
    app_hub_scenes_snapshot_t *out_snapshot)
{
    if (!handle || !out_snapshot) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    *out_snapshot = (app_hub_scenes_snapshot_t) {
        .generation = handle->scenes_generation,
        .total = handle->scene_count,
        .ready = handle->scenes_ready,
        .pending = handle->scene_pending,
    };
    strlcpy(out_snapshot->status, handle->scenes_status,
            sizeof(out_snapshot->status));
    out_snapshot->page_count =
        (handle->scene_count + APP_HUB_SCENES_PAGE_SIZE - 1) /
        APP_HUB_SCENES_PAGE_SIZE;
    if (out_snapshot->page_count == 0) {
        out_snapshot->page_count = 1;
    }
    if (page >= out_snapshot->page_count) {
        page = out_snapshot->page_count - 1;
    }
    out_snapshot->page = page;
    const size_t start = page * APP_HUB_SCENES_PAGE_SIZE;
    for (size_t i = 0;
            i < APP_HUB_SCENES_PAGE_SIZE && start + i < handle->scene_count;
            ++i) {
        out_snapshot->scenes[i] = handle->scenes[start + i];
        ++out_snapshot->visible;
    }
    xSemaphoreGive(handle->mutex);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_scene_invoke(app_hub_handle_t handle,
                                      const char *scene_id)
{
    if (!handle || !scene_id || scene_id[0] == '\0') {
        return APP_HUB_ERR_INVALID_ARG;
    }
    hub_command_t command = {
        .kind = HUB_CMD_SCENE_INVOKE,
    };
    strlcpy(command.point_id, scene_id, sizeof(command.point_id));
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        handle->scene_pending = true;
        strlcpy(handle->scenes_status, "Starting scene...",
                sizeof(handle->scenes_status));
        ++handle->scenes_generation;
        xSemaphoreGive(handle->mutex);
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_scene_delete(app_hub_handle_t handle,
                                      const char *scene_id)
{
    if (!handle || !scene_id || scene_id[0] == '\0') {
        return APP_HUB_ERR_INVALID_ARG;
    }
    hub_command_t command = {
        .kind = HUB_CMD_SCENE_DELETE,
    };
    strlcpy(command.point_id, scene_id, sizeof(command.point_id));
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        handle->scene_pending = true;
        strlcpy(handle->scenes_status, "Deleting scene...",
                sizeof(handle->scenes_status));
        ++handle->scenes_generation;
        xSemaphoreGive(handle->mutex);
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_scene_create(app_hub_handle_t handle,
                                      const char *name,
                                      const char *const *point_ids,
                                      const bool *on_values, size_t count)
{
    if (!handle || !name || !name[0] || !point_ids || !on_values ||
            count == 0 || count > APP_HUB_PAGE_SIZE) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    hub_command_t command = {
        .kind = HUB_CMD_SCENE_CREATE,
        .scene_count = (uint8_t)count,
    };
    static uint32_t s_scene_seq;
    snprintf(command.point_id, sizeof(command.point_id), "panel_%lu",
             (unsigned long)++s_scene_seq);
    strlcpy(command.name, name, sizeof(command.name));
    for (size_t i = 0; i < count; ++i) {
        if (!point_ids[i] || !point_ids[i][0]) {
            return APP_HUB_ERR_INVALID_ARG;
        }
        strlcpy(command.scene_points[i], point_ids[i],
                sizeof(command.scene_points[i]));
        command.scene_values[i] = on_values[i];
    }
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        app_hub_scene_t *slot = upsert_local_scene(handle, command.point_id,
                                                   name,
                                                   count ? 1U : 0U);
        if (slot) {
            fill_scene_actions_from_command(slot, &command);
        }
        handle->scene_pending = true;
        strlcpy(handle->scenes_status, "Creating scene...",
                sizeof(handle->scenes_status));
        ++handle->scenes_generation;
        xSemaphoreGive(handle->mutex);
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_rules_snapshot(
    app_hub_handle_t handle, size_t page,
    app_hub_rules_snapshot_t *out_snapshot)
{
    if (!handle || !out_snapshot) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    *out_snapshot = (app_hub_rules_snapshot_t) {
        .generation = handle->rules_generation,
        .total = handle->rule_count,
        .ready = handle->rules_ready,
        .pending = handle->rule_pending,
    };
    strlcpy(out_snapshot->status, handle->rules_status,
            sizeof(out_snapshot->status));
    out_snapshot->page_count =
        (handle->rule_count + APP_HUB_RULES_PAGE_SIZE - 1) /
        APP_HUB_RULES_PAGE_SIZE;
    if (out_snapshot->page_count == 0) {
        out_snapshot->page_count = 1;
    }
    if (page >= out_snapshot->page_count) {
        page = out_snapshot->page_count - 1;
    }
    out_snapshot->page = page;
    const size_t start = page * APP_HUB_RULES_PAGE_SIZE;
    for (size_t i = 0;
            i < APP_HUB_RULES_PAGE_SIZE && start + i < handle->rule_count;
            ++i) {
        out_snapshot->rules[i] = handle->rules[start + i];
        ++out_snapshot->visible;
    }
    xSemaphoreGive(handle->mutex);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_rule_set_enabled(app_hub_handle_t handle,
                                          const char *rule_id, bool enabled)
{
    if (!handle || !rule_id || rule_id[0] == '\0') {
        return APP_HUB_ERR_INVALID_ARG;
    }
    hub_command_t command = {
        .kind = HUB_CMD_RULE_SET_ENABLED,
        .enabled = enabled,
    };
    strlcpy(command.point_id, rule_id, sizeof(command.point_id));
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        handle->rule_pending = true;
        strlcpy(handle->rules_status, "Saving automation...",
                sizeof(handle->rules_status));
        ++handle->rules_generation;
        xSemaphoreGive(handle->mutex);
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_rule_delete(app_hub_handle_t handle,
                                     const char *rule_id)
{
    if (!handle || !rule_id || rule_id[0] == '\0') {
        return APP_HUB_ERR_INVALID_ARG;
    }
    hub_command_t command = {
        .kind = HUB_CMD_RULE_DELETE,
    };
    strlcpy(command.point_id, rule_id, sizeof(command.point_id));
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        handle->rule_pending = true;
        strlcpy(handle->rules_status, "Deleting automation...",
                sizeof(handle->rules_status));
        ++handle->rules_generation;
        xSemaphoreGive(handle->mutex);
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_rule_create(app_hub_handle_t handle, const char *name,
                                     const char *source_point,
                                     const char *source_value,
                                     const char *target_point, bool target_on)
{
    if (!handle || !name || !name[0] || !source_point || !source_point[0] ||
            !source_value || !source_value[0] || !target_point ||
            !target_point[0]) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    hub_command_t command = {
        .kind = HUB_CMD_RULE_CREATE,
        .enabled = target_on,
    };
    strlcpy(command.name, name, sizeof(command.name));
    strlcpy(command.point_id, source_point, sizeof(command.point_id));
    strlcpy(command.source_value, source_value, sizeof(command.source_value));
    strlcpy(command.target_point, target_point, sizeof(command.target_point));
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        handle->rule_pending = true;
        strlcpy(handle->rules_status, "Creating automation...",
                sizeof(handle->rules_status));
        ++handle->rules_generation;
        xSemaphoreGive(handle->mutex);
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_system_snapshot(
    app_hub_handle_t handle, app_hub_system_snapshot_t *out_snapshot)
{
    if (!handle || !out_snapshot) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(HUB_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    *out_snapshot = handle->system;
    if (out_snapshot->ip[0] == '\0') {
        strlcpy(out_snapshot->ip, handle->local_ip, sizeof(out_snapshot->ip));
    }
    xSemaphoreGive(handle->mutex);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_system_set_wifi(app_hub_handle_t handle,
                                         const char *ssid,
                                         const char *password,
                                         bool open_network)
{
    if (!handle || !ssid || strlen(ssid) >= APP_HUB_WIFI_SSID_LEN ||
            (password && strlen(password) > APP_HUB_CREDENTIAL_LEN)) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    hub_command_t command = {
        .kind = HUB_CMD_SYSTEM_WIFI,
        .wifi_open = open_network,
    };
    strlcpy(command.wifi_ssid, ssid, sizeof(command.wifi_ssid));
    strlcpy(command.wifi_password, password ? password : "",
            sizeof(command.wifi_password));
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}

app_hub_result_t app_hub_system_reboot(app_hub_handle_t handle)
{
    if (!handle) {
        return APP_HUB_ERR_INVALID_ARG;
    }
    hub_command_t command = {
        .kind = HUB_CMD_SYSTEM_REBOOT,
    };
    if (xQueueSend(handle->command_queue, &command, 0) != pdTRUE) {
        return APP_HUB_ERR_BUSY;
    }
    xTaskNotifyGive(handle->worker);
    return APP_HUB_OK;
}
