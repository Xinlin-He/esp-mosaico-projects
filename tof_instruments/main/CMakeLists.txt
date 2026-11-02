idf_component_register(
    SRCS
        "main.c"
        "game_ui.c"
        "lane_art.c"
        "instrument_tof.c"
        "instrument_audio.c"
        "guitar_chords.c"
        "guitar_tune.c"
        "board_display.c"
        "iris_screen_mirror.c"
        "ui_bundle.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_iris esp-mosaico-bsp esp_mosaico_app_recovery
        mosaico_module_mgr mosaico_module_tof pkolt__vl53l0x
        freertos nvs_flash
    PRIV_REQUIRES
        esp_timer esp-gsp esp_display_present esp_lcd esp_partition mbedtls
    EMBED_FILES
        "n040.pcm" "n041.pcm" "n043.pcm" "n045.pcm" "n047.pcm" "n048.pcm"
        "n050.pcm" "n052.pcm" "n053.pcm" "n055.pcm" "n057.pcm" "n059.pcm"
        "n060.pcm" "n062.pcm" "n064.pcm" "n065.pcm" "n066.pcm" "n067.pcm")

target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=esp_display_presenter_submit_buffer")

set(gsp_bundle_symbol bundle)
gsp_add_bundle(${COMPONENT_LIB}
    SCENES "../ui/main.json"
    PIXEL_FORMAT rgb565
    DEPLOYABLE
    SYMBOL ${gsp_bundle_symbol})
