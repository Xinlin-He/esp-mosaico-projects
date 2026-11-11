cmake_minimum_required(VERSION 3.16)
set(MOSAICO_UTILS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-utils")
set(MOSAICO_BSP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../submodule/esp-mosaico-bsp")
if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()
if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.application.defaults")
endif()

set(MINIHUB_PANEL_VARIANT "zigbee" CACHE STRING
    "MiniHub radio variant: zigbee or thread")
set_property(CACHE MINIHUB_PANEL_VARIANT PROPERTY STRINGS zigbee thread)
if(NOT MINIHUB_PANEL_VARIANT STREQUAL "zigbee" AND
   NOT MINIHUB_PANEL_VARIANT STREQUAL "thread")
    message(FATAL_ERROR
        "MINIHUB_PANEL_VARIANT must be 'zigbee' or 'thread', got '${MINIHUB_PANEL_VARIANT}'")
endif()
# ESP-IDF expands component requirements in a child CMake process that does
# not inherit arbitrary cache variables. Export the selected variant so
# component CMake files resolve the same radio graph during regeneration.
set(ENV{MINIHUB_PANEL_VARIANT} "${MINIHUB_PANEL_VARIANT}")
list(APPEND SDKCONFIG_DEFAULTS
    "sdkconfig.variant.${MINIHUB_PANEL_VARIANT}.defaults")

get_filename_component(MINIHUB_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../esp-minihub" ABSOLUTE)
if(NOT DEFINED ENV{ESP_MATTER_PATH})
    get_filename_component(_LOCAL_ESP_MATTER
        "${CMAKE_CURRENT_LIST_DIR}/../../../esp-matter" ABSOLUTE)
    if(EXISTS "${_LOCAL_ESP_MATTER}/components")
        set(ENV{ESP_MATTER_PATH} "${_LOCAL_ESP_MATTER}")
    endif()
endif()
if(NOT DEFINED ENV{ESP_MATTER_PATH})
    message(FATAL_ERROR "ESP_MATTER_PATH is required for MiniHub Matter support")
endif()

# MiniHub is a read-only upstream. Import its official Host graph and startup
# implementation; this product only adds the Mosaico display/Recovery shell.
set(MINIHUB_IMPORT_STORE_MODULES_DISABLED ON)
include("${MINIHUB_REPO_ROOT}/cmake/minihub_bundle/minihub_import.cmake")

include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_compiler.cmake")
list(APPEND EXTRA_COMPONENT_DIRS
    "${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/components/esp_mosaico_app_recovery"
    "${MOSAICO_UTILS_ROOT}/mosaico-tools/components/esp_mosaico_gsp_bundle"
    "${MOSAICO_UTILS_ROOT}/mosaico-tools/components/esp_mosaico_gsp_iris")
include("${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/cmake/mosaico_idf_project.cmake")
minihub_register_kconfig()
idf_build_set_property(COMPILE_OPTIONS
    "-Wno-error=stringop-truncation;-Wno-error=format-truncation;-Wno-error=format;-Wno-error=unused-result" APPEND)
project(minihub_panel_${MINIHUB_PANEL_VARIANT} VERSION 1.0.0)
if(CONFIG_MINIHUB_MATTER_ADAPTER AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
    foreach(_comp esp_matter_controller esp_matter esp_matter_ota_provider esp_matter_console minihub_matter)
        idf_build_get_property(_build_components BUILD_COMPONENTS)
        if(${_comp} IN_LIST _build_components)
            idf_component_get_property(_lib ${_comp} COMPONENT_LIB)
            get_target_property(_lib_type ${_lib} TYPE)
            if(NOT _lib_type STREQUAL "INTERFACE_LIBRARY")
                target_compile_options(${_lib} PRIVATE
                    $<$<COMPILE_LANGUAGE:CXX>:-fpermissive;-Wno-error>)
            endif()
        endif()
    endforeach()
endif()
include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/system_update.cmake")
