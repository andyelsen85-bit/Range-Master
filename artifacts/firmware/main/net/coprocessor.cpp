// ============================================================
// ESP32-C6 co-processor UART AT bridge (WiFi + BLE HID)
// ============================================================
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "coprocessor.h"
#include "app_config.h"

static const char *TAG = "coprocessor";

static QueueHandle_t s_uart_queue;
static TaskHandle_t  s_event_task    = NULL;   // handle for suspend/resume
static char          s_rx_buf[C6_UART_RX_BUF];
static volatile bool s_wifi_connected = false;
static volatile bool s_ble_connected  = false;
static volatile char s_last_key       = 0;

// ── Low-level AT send / wait for response ─────────────────────
static esp_err_t at_send(const char *cmd, const char *expect,
                         char *resp, size_t resp_len, uint32_t timeout_ms)
{
    // Suspend the event task so it cannot consume RX bytes that belong to
    // this synchronous request/response exchange.
    if (s_event_task) vTaskSuspend(s_event_task);

    uart_flush_input(C6_UART_PORT);   // discard any stale RX bytes
    uart_write_bytes(C6_UART_PORT, cmd, strlen(cmd));
    uart_write_bytes(C6_UART_PORT, "\r\n", 2);

    uint32_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    int total = 0;
    memset(s_rx_buf, 0, sizeof(s_rx_buf));

    while (xTaskGetTickCount() < deadline) {
        int n = uart_read_bytes(C6_UART_PORT, (uint8_t *)s_rx_buf + total,
                                sizeof(s_rx_buf) - total - 1,
                                pdMS_TO_TICKS(50));
        if (n > 0) {
            total += n;
            s_rx_buf[total] = '\0';
            if (strstr(s_rx_buf, expect)) {
                if (resp && resp_len) {
                    strncpy(resp, s_rx_buf, resp_len - 1);
                    resp[resp_len - 1] = '\0';
                }
                if (s_event_task) vTaskResume(s_event_task);
                return ESP_OK;
            }
            if (strstr(s_rx_buf, "ERROR")) {
                if (s_event_task) vTaskResume(s_event_task);
                return ESP_FAIL;
            }
        }
    }
    ESP_LOGW(TAG, "AT timeout waiting for '%s'. Got: %.80s", expect, s_rx_buf);
    if (s_event_task) vTaskResume(s_event_task);
    return ESP_ERR_TIMEOUT;
}

// ── UART event task (handles unsolicited C6 events) ──────────
static void uart_event_task(void *arg)
{
    uart_event_t event;
    uint8_t buf[256];
    while (true) {
        if (xQueueReceive(s_uart_queue, &event, portMAX_DELAY)) {
            if (event.type == UART_DATA) {
                int n = uart_read_bytes(C6_UART_PORT, buf,
                                        sizeof(buf) - 1,
                                        pdMS_TO_TICKS(20));
                if (n > 0) {
                    buf[n] = '\0';
                    // Handle unsolicited events from Espressif ESP-AT firmware.
                    // Standard strings (no '+' prefix, no colon):
                    //   "WIFI CONNECTED"  "WIFI GOT IP"  "WIFI DISCONNECT"
                    //   "+BLE_CONN:"  "+BLE_DISCONN:"  (BLE varies by version)
                    if (strstr((char *)buf, "WIFI GOT IP"))
                        s_wifi_connected = true;
                    if (strstr((char *)buf, "WIFI DISCONNECT"))
                        s_wifi_connected = false;
                    if (strstr((char *)buf, "+BLE_CONN:") ||
                        strstr((char *)buf, "+BLE:CONNECTED"))
                        s_ble_connected = true;
                    if (strstr((char *)buf, "+BLE_DISCONN:") ||
                        strstr((char *)buf, "+BLE:DISCONNECTED"))
                        s_ble_connected = false;
                    // BLE HID key event: "+KEY:A"
                    const char *kp = strstr((char *)buf, "+KEY:");
                    if (kp && strlen(kp) >= 6) {
                        s_last_key = kp[5];
                    }
                }
            }
        }
    }
}

// ── Public init ───────────────────────────────────────────────
void coprocessor_init(void)
{
    ESP_LOGI(TAG, "Init UART%d to ESP32-C6 TX=%d RX=%d @%d baud",
             C6_UART_PORT, C6_UART_TX, C6_UART_RX, C6_UART_BAUD);

    uart_config_t cfg = {};
    cfg.baud_rate  = C6_UART_BAUD;
    cfg.data_bits  = UART_DATA_8_BITS;
    cfg.parity     = UART_PARITY_DISABLE;
    cfg.stop_bits  = UART_STOP_BITS_1;
    cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_driver_install(C6_UART_PORT, C6_UART_RX_BUF * 2,
                                        0, 20, &s_uart_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(C6_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(C6_UART_PORT, C6_UART_TX, C6_UART_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(uart_event_task, "cop_uart", 4096, NULL, 10, &s_event_task);

    // Probe the C6 — retry up to 5× with increasing delay.
    // The factory Guition board has Espressif ESP-AT pre-loaded; no flash needed.
    // The C6 AT firmware can take 1-2 s to boot, so we retry rather than give up.
    bool c6_ok = false;
    for (int attempt = 0; attempt < 5; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(attempt == 0 ? 500 : 1000));
        if (at_send("AT", "OK", NULL, 0, 2000) == ESP_OK) {
            ESP_LOGI(TAG, "C6 responsive (attempt %d/5)", attempt + 1);
            c6_ok = true;
            break;
        }
        ESP_LOGW(TAG, "C6 AT probe %d/5 — no response yet", attempt + 1);
    }
    if (c6_ok) {
        at_send("ATE0",       "OK", NULL, 0, 1000);  // disable echo
        at_send("AT+CWMODE=1","OK", NULL, 0, 2000);  // STA mode — required before CWJAP
    } else {
        ESP_LOGE(TAG, "C6 not responding after 5 attempts — check GPIO4/5 wiring");
    }
}

// ── WiFi ─────────────────────────────────────────────────────
esp_err_t cop_wifi_connect(const char *ssid, const char *pass)
{
    // Always ensure STA mode before connecting — handles the case where
    // the init-time probe timed out and CWMODE was never set.
    at_send("AT+CWMODE=1", "OK", NULL, 0, 3000);
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pass);
    // 15 s: AT+CWJAP can take up to ~10 s for association + DHCP; 8 s was too tight.
    return at_send(cmd, "WIFI GOT IP", NULL, 0, 15000);
}

esp_err_t cop_wifi_disconnect(void)
{
    return at_send("AT+CWQAP", "OK", NULL, 0, 3000);
}

esp_err_t cop_wifi_get_ip(char *buf, size_t len)
{
    char resp[256] = {0};
    esp_err_t err = at_send("AT+CIPSTA?", "OK", resp, sizeof(resp), 3000);
    if (err != ESP_OK) return err;
    // Parse +CIPSTA:ip:"x.x.x.x"
    const char *p = strstr(resp, "ip:\"");
    if (!p) return ESP_ERR_NOT_FOUND;
    p += 4;
    const char *end = strchr(p, '"');
    if (!end) return ESP_ERR_NOT_FOUND;
    size_t iplen = (size_t)(end - p);
    if (iplen >= len) iplen = len - 1;
    strncpy(buf, p, iplen);
    buf[iplen] = '\0';
    return ESP_OK;
}

esp_err_t cop_wifi_scan(char names[][33], int max, int *count)
{
    char resp[1024] = {0};
    esp_err_t err = at_send("AT+CWLAP", "OK", resp, sizeof(resp), 10000);
    *count = 0;
    if (err != ESP_OK) return err;
    // Each line: +CWLAP:(ecn,"ssid",rssi,...)
    char *p = resp;
    while ((p = strstr(p, "+CWLAP:")) && *count < max) {
        p += 8; // skip +CWLAP:(
        const char *sq = strchr(p, '"');
        if (!sq) break;
        sq++;
        const char *eq = strchr(sq, '"');
        if (!eq) break;
        size_t nlen = (size_t)(eq - sq);
        if (nlen >= 33) nlen = 32;
        strncpy(names[*count], sq, nlen);
        names[*count][nlen] = '\0';
        (*count)++;
        p = (char *)eq + 1;
    }
    return ESP_OK;
}

bool cop_wifi_is_connected(void) { return s_wifi_connected; }

// ── BLE HID ───────────────────────────────────────────────────
esp_err_t cop_ble_start(void)
{
    // Set device name and start BLE HID keyboard mode
    at_send("AT+BLEINIT=2", "OK", NULL, 0, 3000);
    at_send("AT+BLENAME=\"TrapMaster KB\"", "OK", NULL, 0, 3000);
    return at_send("AT+BLEADVSTART", "OK", NULL, 0, 3000);
}

esp_err_t cop_ble_stop(void)
{
    at_send("AT+BLEADVSTOP", "OK", NULL, 0, 3000);
    return at_send("AT+BLEINIT=0", "OK", NULL, 0, 3000);
}

bool cop_ble_is_connected(void) { return s_ble_connected; }

char cop_ble_pop_key(void)
{
    char k = s_last_key;
    s_last_key = 0;
    return k;
}
