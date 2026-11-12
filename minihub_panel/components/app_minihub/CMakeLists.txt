set(_official_main
    "${MINIHUB_REPO_ROOT}/application/minihub/main/main.c")
set(_official_adapters
    "${MINIHUB_REPO_ROOT}/application/minihub/main/adapters_setup.generated.c")

idf_component_register(
    SRCS "app_minihub.c" "${_official_main}" "${_official_adapters}"
    INCLUDE_DIRS "." "include" "${MINIHUB_REPO_ROOT}/application/minihub/main"
    REQUIRES
        minihub_bundle
        minihub_net
        minihub_mqtt_broker
        minihub_usb_msc
        minihub_rtsp_cam
        adapter_wifi
        nvs_flash
        esp_flash_dispatcher
        esp_system
)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    app_main=minihub_official_main)
