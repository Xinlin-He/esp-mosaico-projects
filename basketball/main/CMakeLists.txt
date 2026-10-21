idf_component_register(
    SRCS "main.c" "bball_app.c" "bball_game.c" "bball_view.c" "game_audio.c" "raylib_screen_mirror.c"
    INCLUDE_DIRS "."
    REQUIRES mosaico_game_app mosaico_game_assets mosaico_game_2d
             mosaico_raylib_fast mosaico_raylib_port raylib esp-gsp
             esp-mosaico-bsp nvs_flash)

# --- Game-asset atlas pipeline --------------------------------------------
# Compile the real ball / rim+net / arcade-interior PNGs into runtime .atlas
# files plus assets_ids.h, then pack them into the game_assets partition so the
# device renders the same textured look as the Host simulator.
set(GAME_ASSET_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../assets_src")
set(GAME_ASSET_OUTPUT "${CMAKE_CURRENT_LIST_DIR}/../assets/generated")
add_custom_target(basketball_assets ALL
    COMMAND "${PYTHON}" "${RAYLIB_LITE_ENGINE_ROOT}/tools/pack_game_assets.py"
            --source "${GAME_ASSET_SOURCE}" --output "${GAME_ASSET_OUTPUT}"
    BYPRODUCTS "${GAME_ASSET_OUTPUT}/ball.atlas"
               "${GAME_ASSET_OUTPUT}/hoop.atlas"
               "${GAME_ASSET_OUTPUT}/court.atlas"
               "${GAME_ASSET_OUTPUT}/assets_ids.h"
    DEPENDS "${GAME_ASSET_SOURCE}/game_assets.json"
            "${GAME_ASSET_SOURCE}/atlas.json"
            "${GAME_ASSET_SOURCE}/hoop_atlas.json"
            "${GAME_ASSET_SOURCE}/court_atlas.json"
            "${GAME_ASSET_SOURCE}/basketball.png"
            "${GAME_ASSET_SOURCE}/hoop.png"
            "${GAME_ASSET_SOURCE}/court.png")
add_dependencies(${COMPONENT_LIB} basketball_assets)
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
    "${GAME_ASSET_OUTPUT}")
spiffs_create_partition_assets(
    game_assets "${GAME_ASSET_OUTPUT}"
    FLASH_IN_PROJECT
    MMAP_FILE_SUPPORT_FORMAT ".atlas"
    MMAP_FILE_ALIGNMENT 64
    IMPORT_INC_PATH "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS basketball_assets)

set(canvas_png "${CMAKE_CURRENT_BINARY_DIR}/canvas_480.png")
execute_process(
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/make_canvas_png.py" "${canvas_png}"
    RESULT_VARIABLE canvas_result)
if(NOT canvas_result EQUAL 0)
    message(FATAL_ERROR "Could not generate Raylib Canvas placeholder")
endif()
set(CANVAS_IMAGE "${canvas_png}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/scene.json.in"
               "${CMAKE_CURRENT_BINARY_DIR}/scene.json" @ONLY)
gsp_add_bundle(${COMPONENT_LIB}
    SCENES "${CMAKE_CURRENT_BINARY_DIR}/scene.json"
    PIXEL_FORMAT rgb565)

# Ship the generated atlas with the application's System Update bundle so the
# game_assets partition is written alongside the app image.
set_property(GLOBAL APPEND PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_LABELS game_assets)
set_property(GLOBAL PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_game_assets_IMAGE
    "${CMAKE_BINARY_DIR}/mmap_build/generated/game_assets/game_assets.bin")
set_property(GLOBAL PROPERTY MOSAICO_SYSTEM_UPDATE_DATA_game_assets_TARGET
    assets_game_assets_bin)
