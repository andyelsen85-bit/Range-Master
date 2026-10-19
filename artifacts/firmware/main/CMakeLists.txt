idf_component_register(
    SRCS
        "main.cpp"
        "audio/click_sound.cpp"
        "display/jd9365_panel.cpp"
        "display/gsl3680_touch.cpp"
        "store/game_store.cpp"
        "store/day_inventory.cpp"
        "store/day_player_events.cpp"
        "store/terminal_logs.cpp"
        "store/game_journal.cpp"
        "store/storage_admin.cpp"
        "store/active_game.cpp"
        "store/fat_selftest.cpp"
        "ui/screen_inventory.cpp"
        "store/offline_cache.cpp"
        "net/coprocessor.cpp"
        "net/http_sync.cpp"
        "net/network_trace.cpp"
        "net/network_test.cpp"
        "net/socket_census.cpp"
        "net/runtime_safety.cpp"
        "net/gateway_http.cpp"
        "net/bounded_dns.cpp"
        "net/portal_runtime.cpp"
        "net/portal_queue_json.cpp"
        "ui/screen_queue.cpp"
        "ui/ui_manager.cpp"
        "ui/ui_fonts.cpp"
        "ui/display_text.cpp"
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
        "lora_stub/button_events.cpp"
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
        esp_websocket_client
        esp-tls
        esp_http_server
        mbedtls
        nvs_flash
        json
        esp_timer
        esp_app_format
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

# This static pass needs only Python, including on Windows (no host compiler).
add_custom_target(ui_font_coverage ALL
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../tests/test_font_coverage.py" --check-only
    VERBATIM)
add_dependencies(${COMPONENT_LIB} ui_font_coverage)
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=lwip_getaddrinfo,--wrap=lwip_connect,--wrap=mbedtls_ssl_handshake,--wrap=lwip_send,--wrap=lwip_recv,--wrap=lwip_select")

# Keep size headroom in the 0x330000 OTA slots without changing storage offsets.
# These control/UI and network modules are not display/DMA hot paths; use scoped
# size optimization without changing performance optimization globally.
# Per-source options are emitted after the sdkconfig-selected -O2, so this also
# applies to existing builds without deleting sdkconfig or terminal data.
set_source_files_properties(
    "store/game_store.cpp"
    "store/game_journal.cpp"
    "store/day_stats.cpp"
    "store/day_inventory.cpp"
    "store/offline_cache.cpp"
    "store/terminal_logs.cpp"
    "ui/screen_dashboard.cpp"
    "ui/screen_start.cpp"
    "ui/screen_spiel.cpp"
    "ui/screen_resultate.cpp"
    "ui/screen_spiller.cpp"
    "ui/screen_kredite.cpp"
    "ui/screen_wifi.cpp"
    "ui/screen_catering.cpp"
    "ui/screen_inventory.cpp"
    "ui/screen_einstellungen.cpp"
    "ui/screen_geschichte.cpp"
    "net/http_sync.cpp"
    "net/portal_runtime.cpp"
    "net/portal_queue_json.cpp"
    "ui/screen_queue.cpp"
    "lora_stub/lora_stub.cpp"
    "lora_stub/button_events.cpp"
    PROPERTIES COMPILE_OPTIONS "-Os"
)
