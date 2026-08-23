// ============================================================
// ESP32-C6 co-processor WiFi bridge — ESP-Hosted SDIO transport
//
// The P4↔C6 link is SDIO (GPIO14-19 + reset GPIO54 + wakeup GPIO6).
// espressif/esp_wifi_remote intercepts all esp_wifi_* calls and
// routes them over esp_hosted (SDIO) to the C6 slave firmware.
// Application code is identical to a native WiFi chip — no AT commands.
//
// C6 must be flashed with ESP-Hosted-MCU slave firmware (SDIO mode).
// ============================================================
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_heap_caps.h"
#include "coprocessor.h"
#include "game_store.h"
#include "esp_sntp.h"
#include "web_config.h"
#include "app_config.h"

static const char *TAG = "coprocessor";

// ── WiFi state ───────────────────────────────────────────────
static EventGroupHandle_t  s_wifi_event_group;
static EventGroupHandle_t  s_scan_done_group  = NULL;  // non-NULL only during cop_wifi_scan()
static volatile bool       s_wifi_connected = false;
static char                s_ip_addr[16]    = {0};
static int                 s_retry_count    = 0;

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define SCAN_DONE_BIT       BIT2
#define WIFI_CONNECT_TIMEOUT_MS  20000   // 20 s — DHCP can be slow
#define WIFI_SCAN_TIMEOUT_MS     30000   // 30 s — allow for slow RPC; reduce once C6 slave is at 2.12.12

// ── WiFi event handler ───────────────────────────────────────
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    if (base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started");
                break;

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi associated");
                break;

            case WIFI_EVENT_SCAN_DONE:
                // Signal cop_wifi_scan() that results are ready.
                // s_scan_done_group is pre-allocated in coprocessor_init().
                // NOTE: this handler runs in the event-loop task (not an ISR),
                // so use xEventGroupSetBits — the ISR variant routes through
                // the timer daemon and can silently drop the signal.
                // Spurious firings are harmless: cop_wifi_scan() clears the
                // bit with xEventGroupClearBits() before each new scan.
                xEventGroupSetBits(s_scan_done_group, SCAN_DONE_BIT);
                break;

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t *disc =
                    (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "WiFi disconnected (reason %d)", disc->reason);
                s_wifi_connected = false;
                s_ip_addr[0]     = '\0';
                web_config_stop();
                // Signal failure so cop_wifi_connect() unblocks
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                break;
            }

            default:
                break;
        }
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_addr, sizeof(s_ip_addr), IPSTR, IP2STR(&ev->ip_info.ip));
        s_wifi_connected = true;
        s_retry_count    = 0;
        ESP_LOGI(TAG, "WiFi got IP: %s", s_ip_addr);
        strncpy(g_store.wifiIp, s_ip_addr, sizeof(g_store.wifiIp) - 1);
        g_store.wifiIp[sizeof(g_store.wifiIp) - 1] = '\0';
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        // Start the config web server so operators can set API URL / key
        // from any browser on the same network without using the on-screen
        // keyboard.  Open http://<ip>/ in any browser.
        web_config_start();
        // Start NTP after DHCP so game timestamps use real wall time.
        // Luxembourg timezone: CET (UTC+1) in winter, CEST (UTC+2) in summer.
        if (!esp_sntp_enabled()) {
            setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
            tzset();
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_setservername(1, "0.europe.pool.ntp.org");
            esp_sntp_init();
            ESP_LOGI(TAG, "SNTP started — pool.ntp.org  TZ=CET-1CEST (Luxembourg)");
        }
    }
}

// ── Public init ───────────────────────────────────────────────
void coprocessor_init(void)
{
    // Diagnostic: show remaining internal RAM after esp_hosted early-init.
    // esp_hosted runs before app_main and consumes DMA-capable internal SRAM
    // for SDIO TX/RX queue buffers. This number must stay above ~16 KB or
    // the PSRAM DMA reserve pool reservation will fail and abort.
    ESP_LOGI(TAG, "Internal RAM free (post esp_hosted init): %u bytes",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "ESP-Hosted SDIO init: CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d RST=%d",
             C6_SDIO_CLK, C6_SDIO_CMD,
             C6_SDIO_D0, C6_SDIO_D1, C6_SDIO_D2, C6_SDIO_D3,
             C6_RESET_PIN);

    s_wifi_event_group = xEventGroupCreate();
    // Pre-allocate the scan-done event group here, while internal RAM is
    // still healthy (~93 KB free).  cop_wifi_scan() previously called
    // xEventGroupCreate() on every scan tap — by then only ~52 bytes of
    // internal RAM remain, so the allocation fails and the scan silently
    // returns ESP_ERR_NO_MEM with zero results.
    s_scan_done_group = xEventGroupCreate();

    // Standard lwip + event loop initialisation.
    // esp_wifi_remote intercepts esp_wifi_init() and brings up the
    // SDIO transport to the C6 slave automatically (Kconfig-configured).
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi stack started (ESP-Hosted SDIO)");
}

// ── WiFi ─────────────────────────────────────────────────────
esp_err_t cop_wifi_connect(const char *ssid, const char *pass)
{
    if (!ssid || strlen(ssid) == 0) return ESP_ERR_INVALID_ARG;

    wifi_config_t wifi_cfg = {};
    strncpy((char *)wifi_cfg.sta.ssid,     ssid, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, pass, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    // Disconnect first if already connected
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));

    // Clear both bits so WaitBits below starts fresh
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s_retry_count = 0;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "Connecting to SSID: %s ...", ssid);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,   // don't clear on exit
        pdFALSE,   // wait for any bit
        pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected. IP: %s", s_ip_addr);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "Connection failed or timed out");
    return ESP_FAIL;
}

esp_err_t cop_wifi_disconnect(void)
{
    s_wifi_connected = false;
    return esp_wifi_disconnect();
}

esp_err_t cop_wifi_get_ip(char *buf, size_t len)
{
    if (!s_wifi_connected || s_ip_addr[0] == '\0') return ESP_ERR_NOT_FOUND;
    strncpy(buf, s_ip_addr, len - 1);
    buf[len - 1] = '\0';
    return ESP_OK;
}

esp_err_t cop_wifi_scan(char names[][33], int max, int *count)
{
    *count = 0;
    ESP_LOGI(TAG, "cop_wifi_scan: entered, max=%d", max);

    // s_scan_done_group was created once in coprocessor_init() while
    // internal RAM was healthy.  Clear any stale bit from a prior scan
    // before starting, so WaitBits below doesn't fire immediately.
    xEventGroupClearBits(s_scan_done_group, SCAN_DONE_BIT);

    wifi_scan_config_t scan_cfg = {};
    scan_cfg.show_hidden = false;
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);   // non-blocking
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan start failed: %s", esp_err_to_name(err));
        return err;
    }

    // Wait for WIFI_EVENT_SCAN_DONE, signalled from wifi_event_handler.
    ESP_LOGI(TAG, "Scan started — waiting up to %d s for results",
             WIFI_SCAN_TIMEOUT_MS / 1000);
    EventBits_t bits = xEventGroupWaitBits(
        s_scan_done_group, SCAN_DONE_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(WIFI_SCAN_TIMEOUT_MS));

    if (!(bits & SCAN_DONE_BIT)) {
        ESP_LOGW(TAG, "Scan timed out after %d s — "
                      "check: (1) C6 slave flashed with esp_hosted 2.12.12? "
                      "Run tools/flash_c6_slave.sh. "
                      "(2) SDIO link stable? Check GPIO14-19/54 connections.",
                 WIFI_SCAN_TIMEOUT_MS / 1000);
        esp_wifi_scan_stop();
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "SCAN_DONE received");

    // Ask the driver how many APs it actually captured — this is the
    // ground truth before we allocate the records buffer.
    uint16_t ap_count_raw = 0;
    esp_err_t num_err = esp_wifi_scan_get_ap_num(&ap_count_raw);
    ESP_LOGI(TAG, "esp_wifi_scan_get_ap_num: err=%s count=%u",
             esp_err_to_name(num_err), (unsigned)ap_count_raw);

    uint16_t ap_num = (ap_count_raw < (uint16_t)max)
                    ? ap_count_raw : (uint16_t)max;
    if (ap_num == 0) {
        // Driver returned zero records — scan ran but found nothing
        // (or the esp_hosted RPC hasn't delivered results yet).
        // Small delay + retry once to handle C6 result latency.
        ESP_LOGW(TAG, "ap_num=0 after SCAN_DONE — retrying get_ap_num in 500 ms");
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_wifi_scan_get_ap_num(&ap_count_raw);
        ap_num = (ap_count_raw < (uint16_t)max) ? ap_count_raw : (uint16_t)max;
        ESP_LOGI(TAG, "retry ap_num=%u", (unsigned)ap_num);
    }

    if (ap_num == 0) {
        ESP_LOGW(TAG, "Still 0 APs — returning empty list");
        *count = 0;
        return ESP_OK;
    }

    wifi_ap_record_t *records =
        (wifi_ap_record_t *)malloc(ap_num * sizeof(wifi_ap_record_t));
    if (!records) return ESP_ERR_NO_MEM;

    uint16_t fetched = ap_num;
    err = esp_wifi_scan_get_ap_records(&fetched, records);
    ESP_LOGI(TAG, "esp_wifi_scan_get_ap_records: err=%s fetched=%u",
             esp_err_to_name(err), (unsigned)fetched);
    if (err == ESP_OK) {
        int idx = 0;
        for (int i = 0; i < fetched; i++) {
            // Skip hidden networks (empty SSID)
            if (records[i].ssid[0] == '\0') continue;
            strncpy(names[idx], (char *)records[i].ssid, 32);
            names[idx][32] = '\0';
            ESP_LOGI(TAG, "  AP[%d]: ssid=\"%s\" rssi=%d ch=%d",
                     idx, names[idx], records[i].rssi, records[i].primary);
            idx++;
        }
        *count = idx;
        ESP_LOGI(TAG, "Scan complete: %d networks found (%u raw, hidden filtered)",
                 *count, (unsigned)fetched);
    }
    free(records);
    return err;
}

bool cop_wifi_is_connected(void) { return s_wifi_connected; }

// ── Auto-connect on boot ──────────────────────────────────────
void coprocessor_autoconnect(void)
{
    if (g_store.wifiSsid[0] == '\0') {
        ESP_LOGI(TAG, "autoconnect: no stored SSID — skipping");
        return;
    }
    ESP_LOGI(TAG, "autoconnect: connecting to stored SSID '%s'", g_store.wifiSsid);

    // One-shot task — tries up to 3 times, then deletes itself.
    // Stack in SPIRAM; TCB needs internal RAM (always the case with xTaskCreate).
    xTaskCreateWithCaps([](void *arg) {
        for (int attempt = 1; attempt <= 3; attempt++) {
            ESP_LOGI("autoconnect", "Attempt %d/3: SSID='%s'", attempt, g_store.wifiSsid);
            esp_err_t err = cop_wifi_connect(g_store.wifiSsid, g_store.wifiPass);
            if (err == ESP_OK) {
                // Mark connected in the store so the dashboard tick picks it up
                g_store.wifiConnected = true;
                ESP_LOGI("autoconnect", "Connected. IP: %s", g_store.wifiIp);
                // Sync once per boot after the stored WiFi connection is
                // confirmed. The store safely waits if its worker is still
                // being created while the display/UI is booting.
                store_sync_after_boot_wifi_connected();
                vTaskDelete(NULL);
                return;
            }
            ESP_LOGW("autoconnect", "Attempt %d/3 failed — %s",
                     attempt, attempt < 3 ? "retrying in 5 s" : "giving up");
            if (attempt < 3) vTaskDelay(pdMS_TO_TICKS(5000));
        }
        vTaskDelete(NULL);
    }, "autoconn", 4096, NULL, 4, NULL,
       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// ── BLE HID — future work via ESP-Hosted BLE transport ───────
// BLE through esp_hosted requires additional hosted BLE configuration
// on both the C6 slave and P4 host. Stubbed for phase 1/2.
esp_err_t cop_ble_start(void)
{
    ESP_LOGW(TAG, "BLE HID not yet implemented (esp_hosted BLE — phase 3)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t cop_ble_stop(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

bool cop_ble_is_connected(void) { return false; }

char cop_ble_pop_key(void)       { return 0; }
