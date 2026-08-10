#pragma once
// ============================================================
// ESP32-C6 co-processor bridge — ESP-Hosted SDIO transport
// WiFi via esp_wifi_remote → esp_hosted → C6 SDIO slave
// ============================================================
#include <stdbool.h>
#include "esp_err.h"

/** Initialise UART to the C6 co-processor. */
void coprocessor_init(void);

/**
 * If a WiFi SSID is stored in NVS, spawn a one-shot background task that
 * calls cop_wifi_connect() automatically.  Call after coprocessor_init()
 * and game_store_init().
 */
void coprocessor_autoconnect(void);

// ── WiFi (AT command wrappers) ───────────────────────────────

/** Connect to SSID/password. Blocks up to C6_AT_TIMEOUT_MS. */
esp_err_t cop_wifi_connect(const char *ssid, const char *pass);

/** Disconnect from current AP. */
esp_err_t cop_wifi_disconnect(void);

/** Get current IP address (null-terminates buf). */
esp_err_t cop_wifi_get_ip(char *buf, size_t len);

/** Scan for nearby networks; fills names[count] (NULL-terminated strings). */
esp_err_t cop_wifi_scan(char names[][33], int max, int *count);

/** Returns true if C6 reports WiFi connected. */
bool cop_wifi_is_connected(void);

// ── BLE HID keyboard ────────────────────────────────────────

/** Start BLE advertising as HID keyboard. */
esp_err_t cop_ble_start(void);

/** Stop BLE advertising / disconnect. */
esp_err_t cop_ble_stop(void);

/**
 * Returns true if a BLE HID host (keyboard) is currently connected.
 * Must be called periodically; the store's last_key is set inside the
 * UART RX handler.
 */
bool cop_ble_is_connected(void);

/**
 * Last character received from the BLE HID keyboard.
 * Returns 0 if no new character.
 */
char cop_ble_pop_key(void);
