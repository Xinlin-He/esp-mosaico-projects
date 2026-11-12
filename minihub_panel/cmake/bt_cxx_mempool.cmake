# IDF 7b9cc1ac, with CONFIG_BT_DUAL_MODE_ARCH=y, implements NimBLE
# os_mempool_* as static inline wrappers that return btdm_osal_error_t
# (typedef int) from functions declared to return os_error_t (an enum).
# C accepts the implicit conversion. GCC 16 C++ rejects it while parsing
# the wrapper, which Matter reaches from NimBLE os.h.
#
# The connectedhomeip component is named chip. The existing GCC 16
# workaround in the project CMakeLists does not list chip, so those
# sources still see the unmodified header.
#
# This writes the same header with an explicit C cast and places that
# copy ahead of the controller include for chip only. It does not change
# IDF, does not add -fpermissive, and does not turn Matter or Zigbee off.
# If the upstream lines move, configuration stops instead of using a
# stale copy.

function(minihub_prepare_bt_cxx_mempool)
    if(NOT CONFIG_MINIHUB_MATTER_ADAPTER)
        return()
    endif()
    idf_build_get_property(_components BUILD_COMPONENTS)
    if(NOT "chip" IN_LIST _components)
        return()
    endif()
    set(_src "$ENV{IDF_PATH}/components/bt/porting_btdm/controller/ble/include/os/os_mempool.h")
    if(NOT EXISTS "${_src}")
        message(FATAL_ERROR
            "MiniHub C++ mempool shim could not find ${_src}")
    endif()
    file(READ "${_src}" _text)
    foreach(_fn
            btdm_memblock_put_from_cb
            btdm_memblock_put
            btdm_mempool_ext_init
            btdm_mempool_ext_clear
            btdm_mempool_init
            btdm_mempool_unregister
            btdm_mempool_clear)
        string(FIND "${_text}" "return ${_fn}(" _at)
        if(_at EQUAL -1)
            message(FATAL_ERROR
                "IDF os_mempool.h has no 'return ${_fn}('. "
                "The dual-mode wrapper layout changed; do not guess a cast.")
        endif()
        string(REPLACE
            "return ${_fn}("
            "return (os_error_t)${_fn}("
            _text "${_text}")
    endforeach()
    set(_root "${CMAKE_BINARY_DIR}/minihub_bt_cxx")
    set(_dir "${_root}/os")
    file(MAKE_DIRECTORY "${_dir}")
    file(WRITE "${_dir}/os_mempool.h"
        "/* Generated from IDF os_mempool.h. Explicit casts only. */\n${_text}")

    # btdm_le.h forward-typedefs esp_power_level before esp_bt.h defines it.
    # C completes that incomplete enum. GCC 16 C++ treats the forward typedef
    # as typedef int, then rejects the real enum. Give the enum a fixed
    # underlying type in both headers. Relative includes are rewritten because
    # this copy does not live next to the IDF tree.
    set(_bt_inc "$ENV{IDF_PATH}/components/bt/include")
    foreach(_name esp32s31/include/esp_bt.h common/btdm_le.h common/btdm_bredr.h)
        if(NOT EXISTS "${_bt_inc}/${_name}")
            message(FATAL_ERROR "MiniHub C++ BLE shim could not find ${_bt_inc}/${_name}")
        endif()
    endforeach()
    file(READ "${_bt_inc}/esp32s31/include/esp_bt.h" _esp_bt)
    file(READ "${_bt_inc}/common/btdm_le.h" _btdm_le)
    file(READ "${_bt_inc}/common/btdm_bredr.h" _btdm_bredr)
    foreach(_needle
            "#include \"../../common/btdm_le.h\""
            "#include \"../../common/btdm_bredr.h\""
            "typedef enum esp_power_level {"
            "typedef enum esp_ble_power_type {")
        string(FIND "${_esp_bt}" "${_needle}" _at)
        if(_at EQUAL -1)
            message(FATAL_ERROR "esp_bt.h no longer contains: ${_needle}")
        endif()
    endforeach()
    string(REPLACE "#include \"../../common/btdm_le.h\"" "#include \"btdm_le.h\""
        _esp_bt "${_esp_bt}")
    string(REPLACE "#include \"../../common/btdm_bredr.h\"" "#include \"btdm_bredr.h\""
        _esp_bt "${_esp_bt}")
    string(REPLACE "typedef enum esp_power_level {" "typedef enum esp_power_level : int {"
        _esp_bt "${_esp_bt}")
    string(REPLACE "typedef enum esp_ble_power_type {" "typedef enum esp_ble_power_type : int {"
        _esp_bt "${_esp_bt}")
    # Unset bools are omitted from sdkconfig.h. `#if CONFIG_...` is 0 in
    # C, and -Werror=undef in the chip C++ build. Keep the disabled meaning.
    set(_log_if "#if CONFIG_BT_LE_CONTROLLER_LOG_ENABLED")
    string(FIND "${_esp_bt}" "${_log_if}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "esp_bt.h no longer contains ${_log_if}")
    endif()
    string(REPLACE "${_log_if}"
        "#if defined(CONFIG_BT_LE_CONTROLLER_LOG_ENABLED) && CONFIG_BT_LE_CONTROLLER_LOG_ENABLED"
        _esp_bt "${_esp_bt}")
    set(_fwd "typedef enum esp_power_level esp_power_level_t;")
    string(FIND "${_btdm_le}" "${_fwd}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "btdm_le.h no longer contains the esp_power_level forward typedef")
    endif()
    set(_cxx_fwd [=[
#ifdef __cplusplus
enum esp_power_level : int;
enum esp_ble_power_type : int;
#endif
]=])
    string(REPLACE "${_fwd}" "${_cxx_fwd}${_fwd}" _btdm_le "${_btdm_le}")
    file(WRITE "${_root}/esp_bt.h" "/* Generated from IDF esp_bt.h. Underlying type only. */\n${_esp_bt}")
    file(WRITE "${_root}/btdm_le.h" "/* Generated from IDF btdm_le.h. C++ forward declaration only. */\n${_btdm_le}")
    file(WRITE "${_root}/btdm_bredr.h" "${_btdm_bredr}")

    idf_component_get_property(_chip chip COMPONENT_LIB)
    target_include_directories(${_chip} BEFORE PRIVATE "${_root}")
    # GCC 16 -Wdangling-pointer flags CHIP's intrusive callback list when a
    # function-local sentinel is linked into the list and unlinked before
    # return (CHIPCallback.h). The pointer does not outlive the sentinel.
    # This demotes that diagnostic for chip only. It is not -fpermissive
    # and it does not hide the os_mempool type conversion above.
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 16)
        target_compile_options(${_chip} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wno-error=dangling-pointer>)
    endif()
endfunction()
