idf_component_register(
    SRCS
        "main.cpp"
        "audio/click_sound.cpp"
        "display/jd9365_panel.cpp"
        "display/gsl3680_touch.cpp"
        "store/game_store.cpp"
        "store/offline_cache.cpp"
        "net/coprocessor.cpp"
        "net/http_sync.cpp"
        "ui/ui_manager.cpp"
        "ui/ui_fonts.cpp"
        "ui/ui_font_fallback_12.c"
        "ui/ui_font_fallback_14.c"
        "ui/ui_font_fallback_16.c"
        "ui/ui_font_fallback_18.c"
        "ui/ui_font_fallback_20.c"
        "ui/ui_font_fallback_22.c"
        "ui/ui_font_fallback_24.c"
        "ui/ui_font_fallback_28.c"
        "ui/ui_font_fallback_36.c"
        "ui/ui_font_fallback_48.c"
        "ui/screen_dashboard.cpp"
        "ui/screen_start.cpp"
        "ui/screen_spiel.cpp"
        "ui/screen_resultate.cpp"
        "ui/screen_geschichte.cpp"
        "ui/screen_kredite.cpp"
        "ui/screen_einstellungen.cpp"
        "ui/screen_spiller.cpp"
        "ui/screen_wifi.cpp"
        "ui/screen_catering.cpp"
        "lora_stub/lora_stub.cpp"
        "net/web_config.cpp"
    INCLUDE_DIRS
        "."
        "audio"
        "display"
        "store"
        "net"
        "ui"
        "lora_stub"
    REQUIRES
        esp_lcd
        esp_driver_ppa
        esp_driver_i2s
        esp_lvgl_port
        lvgl
        driver
        esp_http_client
        esp-tls
        esp_http_server
        mbedtls
        nvs_flash
        json
        esp_timer
        lwip
        freertos
        log
        spi_flash
        fatfs
        wear_levelling
        esp_partition
        esp_system
        esp_psram
        esp_mm
    PRIV_REQUIRES
        # ESP-Hosted WiFi stack (coprocessor.cpp uses esp_wifi_*, esp_netif_*, esp_event_*)
        esp_wifi
        esp_netif
        esp_event
)

# Keep the existing 0x220000 app slots and storage offsets. These control/UI
# and network modules are not display/DMA hot paths; use size optimization rather than
# moving partitions or changing the project's performance optimization globally.
# Per-source options are emitted after the sdkconfig-selected -O2, so this also
# applies to existing builds without deleting sdkconfig or terminal data.
set_source_files_properties(
    "ui/screen_einstellungen.cpp"
    "ui/screen_geschichte.cpp"
    "net/http_sync.cpp"
    "lora_stub/lora_stub.cpp"
    PROPERTIES COMPILE_OPTIONS "-Os"
)
