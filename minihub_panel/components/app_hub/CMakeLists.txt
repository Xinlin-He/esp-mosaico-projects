idf_component_register(
    SRCS "app_hub.c" "hub_matter_status.c"
    INCLUDE_DIRS "include"
    REQUIRES cjson esp_http_client esp_wifi nvs_flash
)
