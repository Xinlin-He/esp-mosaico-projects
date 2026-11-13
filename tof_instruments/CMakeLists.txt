cmake_minimum_required(VERSION 3.16)

get_filename_component(MOSAICO_WORKSPACE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(MOSAICO_UTILS_ROOT
    "${MOSAICO_WORKSPACE_ROOT}/submodule/esp-mosaico-utils")

if(NOT SDKCONFIG)
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()

if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS
        "sdkconfig.defaults;sdkconfig.application.defaults")
endif()

include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/gsp_compiler.cmake")
if(WIN32)
    if(NOT MOSAICO_REAL_GSPC_EXECUTABLE)
        set(MOSAICO_REAL_GSPC_EXECUTABLE "${GSPC_EXECUTABLE}" CACHE INTERNAL
            "Resolved native gspc executable")
    endif()
    find_program(MOSAICO_GSP_DEPFILE_PYTHON NAMES python3 python REQUIRED)
    file(TO_NATIVE_PATH "${MOSAICO_REAL_GSPC_EXECUTABLE}" MOSAICO_REAL_GSPC_NATIVE)
    file(TO_NATIVE_PATH "${MOSAICO_GSP_DEPFILE_PYTHON}" MOSAICO_GSP_PYTHON_NATIVE)
    file(TO_NATIVE_PATH
        "${CMAKE_CURRENT_LIST_DIR}/tools/normalize_gsp_depfile.py"
        MOSAICO_GSP_NORMALIZER_NATIVE)
    set(MOSAICO_GSPC_WRAPPER "${CMAKE_BINARY_DIR}/gspc-wrapper.cmd")
    file(WRITE "${MOSAICO_GSPC_WRAPPER}"
        "@echo off\r\n"
        "\"${MOSAICO_REAL_GSPC_NATIVE}\" %*\r\n"
        "if errorlevel 1 exit /b %errorlevel%\r\n"
        "\"${MOSAICO_GSP_PYTHON_NATIVE}\" \"${MOSAICO_GSP_NORMALIZER_NATIVE}\" %*\r\n")
    set(GSPC_EXECUTABLE "${MOSAICO_GSPC_WRAPPER}" CACHE FILEPATH
        "Windows depfile-normalizing gspc wrapper" FORCE)
    set(ENV{GSPC_EXECUTABLE} "${GSPC_EXECUTABLE}")
endif()

set(MOSAICO_BSP_ROOT
    "${MOSAICO_WORKSPACE_ROOT}/submodule/esp-mosaico-bsp")
set(EXTRA_COMPONENT_DIRS
    "${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/components/esp_mosaico_app_recovery")

include("${MOSAICO_UTILS_ROOT}/esp-mosaico-vibe-mode/cmake/mosaico_idf_project.cmake")
project(tof_instruments VERSION 0.1.1)

include("${MOSAICO_UTILS_ROOT}/mosaico-tools/cmake/system_update.cmake")
