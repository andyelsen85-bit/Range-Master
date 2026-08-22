// ============================================================
// Terminal-to-gateway fire transport.
//
// The terminal deliberately has no LoRa hardware. It queues a short
// HTTP request to the WiFi-connected gateway, which owns the radio.
// ============================================================
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "lora_stub.h"
#include "game_store.h"
#include "../../../lora-common/trapmaster_auth.h"

static const char *TAG = "lora_stub";
static QueueHandle_t s_fire_queue;
static char s_status[96] = "Gateway not configured";

typedef struct {
    Maschine machine;
    uint32_t sequence;
} FireRequest;

static void set_status(const char *text)
{
    snprintf(s_status, sizeof(s_status), "%s", text);
    ESP_LOGI(TAG, "%s", s_status);
}

static bool build_fire_url(char *url, size_t url_len, const FireRequest *request)
{
    const char *base = g_store.gatewayUrl;
    if (!base[0]) {
        set_status("Gateway not configured");
        return false;
    }
    if (strncmp(base, "http://", 7) != 0) {
        set_status("Gateway URL must use http://");
        return false;
    }
    if (!g_store.gatewayToken[0]) {
        set_status("Gateway auth key not configured");
        return false;
    }
    if (strlen(g_store.gatewayToken) < 16) {
        set_status("Gateway auth key too short");
        return false;
    }
    size_t len = strlen(base);
    const char *suffix = (len > 0 && base[len - 1] == '/')
                       ? "fire?machine=" : "/fire?machine=";
    int written = snprintf(url, url_len, "%s%s%c&seq=%08lx", base, suffix,
                           (char)('A' + (int)request->machine),
                           (unsigned long)request->sequence);
    if (written < 0 || (size_t)written >= url_len) {
        set_status("Gateway URL is too long");
        return false;
    }
    return true;
}

static void fire_worker(void *arg)
{
    FireRequest request;
    char url[MAX_URL_LEN + 32];
    for (;;) {
        if (xQueueReceive(s_fire_queue, &request, portMAX_DELAY) != pdTRUE) continue;
        if (!build_fire_url(url, sizeof(url), &request)) continue;

        bool success = false;
        int last_http = 0;
        uint8_t mac[tm_auth::MAC_LEN];
        char mac_hex[tm_auth::MAC_HEX_LEN + 1];
        if (!tm_auth::make_request_mac((const uint8_t *)g_store.gatewayToken,
                                       strlen(g_store.gatewayToken),
                                       (uint8_t)('A' + (int)request.machine),
                                       request.sequence, mac)) {
            set_status("Gateway auth key invalid");
            continue;
        }
        tm_auth::mac_to_hex(mac, mac_hex);
        for (int attempt = 0; attempt < 3 && !success; attempt++) {
            esp_http_client_config_t cfg = {};
            cfg.url = url;
            cfg.timeout_ms = 1500;
            cfg.disable_auto_redirect = true;
            esp_http_client_handle_t client = esp_http_client_init(&cfg);
            if (!client) {
                set_status("Gateway client unavailable");
                break;
            }
            esp_http_client_set_header(client, "X-TrapMaster-Auth", mac_hex);
            esp_err_t err = esp_http_client_perform(client);
            last_http = esp_http_client_get_status_code(client);
            if (err == ESP_OK && last_http >= 200 && last_http < 300) {
                success = true;
            }
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            if (!success && attempt < 2)
                vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (success) {
            char msg[96];
            if (last_http == 202)
                snprintf(msg, sizeof(msg), "Machine %c sent (no ACK)",
                         (char)('A' + (int)request.machine));
            else
                snprintf(msg, sizeof(msg), "Machine %c fired",
                         (char)('A' + (int)request.machine));
            set_status(msg);
        } else if (last_http >= 400) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Gateway rejected (HTTP %d)", last_http);
            set_status(msg);
        } else {
            set_status("Gateway unreachable");
        }
    }
}

void lora_stub_init(void)
{
    if (s_fire_queue) return;
    s_fire_queue = xQueueCreate(1, sizeof(FireRequest));
    if (!s_fire_queue) {
        set_status("Fire queue unavailable");
        return;
    }
    if (xTaskCreateWithCaps(fire_worker, "fire_gateway", 8192, NULL, 5, NULL,
                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
        vQueueDelete(s_fire_queue);
        s_fire_queue = NULL;
        set_status("Gateway worker unavailable");
        return;
    }
    ESP_LOGI(TAG, "Gateway fire worker ready");
}

void lora_fire_machine(Maschine m)
{
    if (!s_fire_queue || m < MASCHINE_A || m >= MASCHINE_COUNT) {
        set_status("Fire request unavailable");
        return;
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "Sending machine %c...", (char)('A' + (int)m));
    set_status(msg);
    // Replace a stale request rather than allowing a burst to build up.
    if (g_store.gatewaySequence == UINT32_MAX) {
        set_status("Gateway sequence exhausted");
        return;
    }
    // Save before the request enters the queue: a reboot can skip a sequence,
    // but it can never reuse a MAC-protected command sequence.
    g_store.gatewaySequence++;
    game_store_save();
    FireRequest request = {
        .machine = m,
        .sequence = g_store.gatewaySequence,
    };
    xQueueOverwrite(s_fire_queue, &request);
}

const char *lora_status_text(void)
{
    return s_status;
}
