cmake_minimum_required(VERSION 3.22)
if(NOT MOSAICO_UTILS_ROOT)
    set(MOSAICO_UTILS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-utils")
endif()
if(NOT MOSAICO_BSP_ROOT)
    set(MOSAICO_BSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-bsp")
endif()
if(NOT RAYLIB_LITE_ENGINE_ROOT)
    set(RAYLIB_LITE_ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/raylib-lite-engine")
endif()

if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()

if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS
        "sdkconfig.defaults;sdkconfig.application.defaults")
endif()

include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/raylib_lite_engine.cmake")
mosaico_game_sdk_configure_gsp_compiler()
mosaico_raylib_lite_add_engine()
include("${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/cmake/mosaico_idf_project.cmake")
project(basketball VERSION 1.0.0)

include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/system_update.cmake")
