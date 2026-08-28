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
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "lora_stub.h"
#include "game_store.h"
#include "coprocessor.h"
#include "../../../lora-common/trapmaster_auth.h"

static const char *TAG = "lora_stub";
static QueueHandle_t s_gateway_queue;
static SemaphoreHandle_t s_state_mutex;
static char s_status[96] = "Gateway not configured";
static bool s_request_busy = false;
static GatewayReachability s_gateway_state = GATEWAY_NOT_CONFIGURED;
static uint32_t s_gateway_state_ms = 0;
static TickType_t s_last_health_tick = 0;

typedef enum : uint8_t {
    GATEWAY_REQUEST_FIRE,
    GATEWAY_REQUEST_FIRE_PAIR,
    GATEWAY_REQUEST_HEALTH,
} GatewayRequestKind;

typedef struct {
    GatewayRequestKind kind;
    Maschine machine;
    Maschine second_machine;
    uint16_t delay_ms;
    uint32_t sequence;
    bool manual; // FIRE and operator-initiated health; autonomous health is false
    bool gameLaunch; // only live-game ACKs contribute to clay accounting
    char gateway_url[MAX_URL_LEN];
    char gateway_token[MAX_KEY_LEN];
} GatewayRequest;

static void set_status(const char *text)
{
    char log_text[sizeof(s_status)];
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    snprintf(s_status, sizeof(s_status), "%s", text);
    snprintf(log_text, sizeof(log_text), "%s", s_status);
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
    ESP_LOGI(TAG, "%s", log_text);
}

static void set_request_busy(bool busy)
{
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_request_busy = busy;
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
}

static void set_gateway_state(GatewayReachability state)
{
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_gateway_state = state;
    s_gateway_state_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
}

static bool begin_request(void)
{
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    bool ready = !s_request_busy;
    if (ready) s_request_busy = true;
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
    return ready;
}

static bool copy_gateway_config(GatewayRequest *request)
{
    if (!request) return false;
    snprintf(request->gateway_url, sizeof(request->gateway_url), "%s", g_store.gatewayUrl);
    snprintf(request->gateway_token, sizeof(request->gateway_token), "%s", g_store.gatewayToken);
    return true;
}

static bool build_fire_url(char *url, size_t url_len, const GatewayRequest *request)
{
    const char *base = request->gateway_url;
    if (!base[0]) {
        set_status("Gateway not configured");
        return false;
    }
    if (strncmp(base, "http://", 7) != 0) {
        set_status("Gateway URL must use http://");
        return false;
    }
    if (!request->gateway_token[0]) {
        set_status("Gateway auth key not configured");
        return false;
    }
    if (strlen(request->gateway_token) < 16) {
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

static bool build_fire_pair_url(char *url, size_t url_len, const GatewayRequest *request)
{
    const char *base = request->gateway_url;
    if (!base[0]) {
        set_status("Gateway not configured");
        return false;
    }
    if (strncmp(base, "http://", 7) != 0) {
        set_status("Gateway URL must use http://");
        return false;
    }
    if (!request->gateway_token[0] || strlen(request->gateway_token) < 16) {
        set_status("Gateway auth key not configured");
        return false;
    }
    size_t len = strlen(base);
    const char *suffix = (len > 0 && base[len - 1] == '/')
                       ? "fire-pair?first=" : "/fire-pair?first=";
    int written = snprintf(url, url_len, "%s%s%c&second=%c&delayMs=%u&seq=%08lx",
                           base, suffix, (char)('A' + (int)request->machine),
                           (char)('A' + (int)request->second_machine),
                           (unsigned)request->delay_ms,
                           (unsigned long)request->sequence);
    if (written < 0 || (size_t)written >= url_len) {
        set_status("Gateway URL is too long");
        return false;
    }
    return true;
}

static bool build_health_url(char *url, size_t url_len, const GatewayRequest *request)
{
    const char *base = request->gateway_url;
    if (!base[0]) {
        set_status("Gateway not configured");
        return false;
    }
    if (strncmp(base, "http://", 7) != 0) {
        set_status("Gateway URL must use http://");
        return false;
    }
    if (!request->gateway_token[0]) {
        set_status("Gateway auth key not configured");
        return false;
    }
    if (strlen(request->gateway_token) < 16) {
        set_status("Gateway auth key too short");
        return false;
    }
    size_t len = strlen(base);
    const char *suffix = (len > 0 && base[len - 1] == '/') ? "health" : "/health";
    int written = snprintf(url, url_len, "%s%s", base, suffix);
    if (written < 0 || (size_t)written >= url_len) {
        set_status("Gateway URL is too long");
        return false;
    }
    return true;
}

static bool perform_authenticated_get(const char *url, const char *mac_hex,
                                       int *last_http, int timeout_ms, int attempts)
{
    bool success = false;
    *last_http = 0;
    for (int attempt = 0; attempt < attempts && !success; attempt++) {
        esp_http_client_config_t cfg = {};
        cfg.url = url;
        cfg.timeout_ms = timeout_ms;
        cfg.disable_auto_redirect = true;
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) {
            set_status("Gateway client unavailable");
            break;
        }
        esp_http_client_set_header(client, "X-TrapMaster-Auth", mac_hex);
        esp_err_t err = esp_http_client_perform(client);
        *last_http = esp_http_client_get_status_code(client);
        if (err == ESP_OK && *last_http >= 200 && *last_http < 300) {
            success = true;
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (!success && attempt + 1 < attempts)
            vTaskDelay(pdMS_TO_TICKS(100));
    }
    return success;
}

static void gateway_worker(void *arg)
{
    GatewayRequest request;
    char url[MAX_URL_LEN + 96];
    for (;;) {
        if (xQueueReceive(s_gateway_queue, &request, pdMS_TO_TICKS(1000)) != pdTRUE) {
            TickType_t now = xTaskGetTickCount();
            if (!cop_wifi_is_connected() || !g_store.gatewayUrl[0] ||
                !g_store.gatewayToken[0] ||
                (now - s_last_health_tick) < pdMS_TO_TICKS(15000)) {
                if (!cop_wifi_is_connected() &&
                    g_store.gatewayUrl[0] && g_store.gatewayToken[0])
                    set_gateway_state(GATEWAY_UNREACHABLE);
                if (!g_store.gatewayUrl[0] || !g_store.gatewayToken[0])
                    set_gateway_state(GATEWAY_NOT_CONFIGURED);
                continue;
            }
            s_last_health_tick = now;
            request = {};
            request.kind = GATEWAY_REQUEST_HEALTH;
            copy_gateway_config(&request);
            set_gateway_state(GATEWAY_CHECKING);
            // FIRE/manual work queued during the idle timeout takes priority.
            // Autonomous health never owns s_request_busy and uses one short
            // request, so it cannot suppress subsequent FIRE requests.
            GatewayRequest queued;
            if (xQueueReceive(s_gateway_queue, &queued, 0) == pdTRUE)
                request = queued;
        }

        uint8_t mac[tm_auth::MAC_LEN];
        char mac_hex[tm_auth::MAC_HEX_LEN + 1];
        int last_http = 0;
        bool success = false;

        if (request.kind == GATEWAY_REQUEST_HEALTH) {
            bool request_ready = false;
            if (build_health_url(url, sizeof(url), &request) &&
                tm_auth::make_health_mac((const uint8_t *)request.gateway_token,
                                         strlen(request.gateway_token), mac)) {
                request_ready = true;
                tm_auth::mac_to_hex(mac, mac_hex);
                success = perform_authenticated_get(url, mac_hex, &last_http,
                    request.manual ? 2000 : 250, request.manual ? 3 : 1);
            } else if (request.gateway_token[0] && strlen(request.gateway_token) >= 16) {
                set_status("Gateway auth key invalid");
            }

            if (success) {
                set_status("Gateway reachable - key accepted");
                set_gateway_state(GATEWAY_REACHABLE);
            } else if (last_http == 401 || last_http == 403) {
                set_status("Gateway key rejected");
                set_gateway_state(GATEWAY_AUTH_FAILED);
            } else if (last_http >= 400) {
                char msg[96];
                snprintf(msg, sizeof(msg), "Gateway check rejected (HTTP %d)", last_http);
                set_status(msg);
                set_gateway_state(GATEWAY_FAILED);
            } else if (request_ready) {
                set_status("Gateway unreachable");
                set_gateway_state(GATEWAY_UNREACHABLE);
            }
            if (request.manual) set_request_busy(false);
            continue;
        }

        bool request_ready = false;
        if (request.kind == GATEWAY_REQUEST_FIRE_PAIR) {
            if (build_fire_pair_url(url, sizeof(url), &request) &&
                tm_auth::make_pair_request_mac(
                    (const uint8_t *)request.gateway_token,
                    strlen(request.gateway_token),
                    (uint8_t)('A' + (int)request.machine),
                    (uint8_t)('A' + (int)request.second_machine),
                    request.delay_ms, request.sequence, mac)) {
                request_ready = true;
                tm_auth::mac_to_hex(mac, mac_hex);
                success = perform_authenticated_get(url, mac_hex, &last_http, 20000, 3);
            }
        } else if (build_fire_url(url, sizeof(url), &request) &&
                   tm_auth::make_request_mac((const uint8_t *)request.gateway_token,
                                              strlen(request.gateway_token),
                                              (uint8_t)('A' + (int)request.machine),
                                              request.sequence, mac)) {
            request_ready = true;
            tm_auth::mac_to_hex(mac, mac_hex);
            success = perform_authenticated_get(url, mac_hex, &last_http, 7000, 3);
        } else if (request.gateway_token[0] && strlen(request.gateway_token) >= 16) {
            set_status("Gateway auth key invalid");
        }

        if (success) {
            // 202 explicitly means the gateway did not observe a FIRE ACK.
            // Tests use the untagged API, so they cannot affect a game total.
            if (request.gameLaunch && last_http != 202)
                store_account_acknowledged_clays(
                    request.kind == GATEWAY_REQUEST_FIRE_PAIR ||
                    request.machine == MASCHINE_H ? 2 : 1);
            set_gateway_state(GATEWAY_REACHABLE);
            char msg[96];
            if (request.kind == GATEWAY_REQUEST_FIRE_PAIR) {
                snprintf(msg, sizeof(msg),
                         last_http == 202
                             ? (request.delay_ms == 0
                                 ? "Pair %c+%c sent (0s: first ACK skipped)"
                                 : "Pair %c+%c sent (ACK missing)")
                             : "Pair %c+%c fired",
                         (char)('A' + (int)request.machine),
                         (char)('A' + (int)request.second_machine));
            } else if (last_http == 202) {
                snprintf(msg, sizeof(msg), "Machine %c sent (no ACK)",
                         (char)('A' + (int)request.machine));
            } else {
                snprintf(msg, sizeof(msg), "Machine %c fired",
                         (char)('A' + (int)request.machine));
            }
            set_status(msg);
        } else if (last_http >= 400) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Gateway rejected (HTTP %d)", last_http);
            set_status(msg);
            set_gateway_state((last_http == 401 || last_http == 403)
                                  ? GATEWAY_AUTH_FAILED : GATEWAY_FAILED);
        } else if (request_ready) {
            set_status("Gateway unreachable");
            set_gateway_state(GATEWAY_UNREACHABLE);
        }
        set_request_busy(false);
    }
}

void lora_stub_init(void)
{
    if (s_gateway_queue) return;
    if (!s_state_mutex) {
        s_state_mutex = xSemaphoreCreateMutex();
        if (!s_state_mutex) {
            set_status("Gateway state unavailable");
            return;
        }
    }
    s_gateway_queue = xQueueCreate(1, sizeof(GatewayRequest));
    if (!s_gateway_queue) {
        set_status("Gateway queue unavailable");
        return;
    }
    if (xTaskCreateWithCaps(gateway_worker, "gateway_http", 8192, NULL, 5, NULL,
                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS) {
        vQueueDelete(s_gateway_queue);
        s_gateway_queue = NULL;
        set_status("Gateway worker unavailable");
        return;
    }
    ESP_LOGI(TAG, "Gateway fire worker ready");
    set_gateway_state((g_store.gatewayUrl[0] && g_store.gatewayToken[0])
                          ? GATEWAY_UNREACHABLE : GATEWAY_NOT_CONFIGURED);
}

bool lora_fire_machine(Maschine m)
{
    if (!s_gateway_queue || m < MASCHINE_A || m >= MASCHINE_COUNT) {
        set_status("Fire request unavailable");
        return false;
    }
    if (!cop_wifi_is_connected()) {
        set_status("WiFi not connected");
        set_gateway_state(GATEWAY_UNREACHABLE);
        return false;
    }
    if (!begin_request()) {
        set_status("Gateway request already in progress");
        return false;
    }
    char msg[96];
    snprintf(msg, sizeof(msg), "Sending machine %c...", (char)('A' + (int)m));
    set_status(msg);
    // Persist before enqueueing: a reboot can skip a sequence, but it can
    // never reuse a MAC-protected FIRE sequence.
    if (g_store.gatewaySequence == UINT32_MAX) {
        set_request_busy(false);
        set_status("Gateway sequence exhausted");
        return false;
    }
    // Save before the request enters the queue: a reboot can skip a sequence,
    // but it can never reuse a MAC-protected command sequence.
    g_store.gatewaySequence++;
    game_store_save();
    GatewayRequest request = {};
    request.kind = GATEWAY_REQUEST_FIRE;
    request.manual = true;
    request.machine = m;
    request.sequence = g_store.gatewaySequence;
    copy_gateway_config(&request);
    if (xQueueSend(s_gateway_queue, &request, 0) != pdTRUE) {
        set_request_busy(false);
        set_status("Gateway queue unavailable");
        return false;
    }
    return true;
}

bool lora_fire_machine_game(Maschine m)
{
    // The queue request must be marked before the worker can consume it, so
    // share the normal validation/sequence flow in a compact local copy.
    if (!s_gateway_queue || m < MASCHINE_A || m >= MASCHINE_COUNT || !cop_wifi_is_connected() ||
        !begin_request() || g_store.gatewaySequence == UINT32_MAX) return false;
    g_store.gatewaySequence++; game_store_save();
    GatewayRequest request = {};
    request.kind = GATEWAY_REQUEST_FIRE; request.manual = true; request.gameLaunch = true;
    request.machine = m; request.sequence = g_store.gatewaySequence; copy_gateway_config(&request);
    if (xQueueSend(s_gateway_queue, &request, 0) != pdTRUE) {
        set_request_busy(false); set_status("Gateway queue unavailable"); return false;
    }
    return true;
}

bool lora_fire_doublette(Maschine first, Maschine second, uint16_t delay_ms)
{
    if (!s_gateway_queue || first < MASCHINE_A || first > MASCHINE_G ||
        second < MASCHINE_A || second > MASCHINE_G || first == second ||
        delay_ms > 10000) {
        set_status("Invalid custom doublette");
        return false;
    }
    if (!cop_wifi_is_connected()) {
        set_status("WiFi not connected");
        set_gateway_state(GATEWAY_UNREACHABLE);
        return false;
    }
    if (!begin_request()) {
        set_status("Gateway request already in progress");
        return false;
    }
    if (g_store.gatewaySequence == UINT32_MAX) {
        set_request_busy(false);
        set_status("Gateway sequence exhausted");
        return false;
    }
    g_store.gatewaySequence++;
    game_store_save();
    GatewayRequest request = {};
    request.kind = GATEWAY_REQUEST_FIRE_PAIR;
    request.manual = true;
    request.machine = first;
    request.second_machine = second;
    request.delay_ms = delay_ms;
    request.sequence = g_store.gatewaySequence;
    copy_gateway_config(&request);
    char msg[96];
    snprintf(msg, sizeof(msg), "Sending pair %c+%c...",
             (char)('A' + (int)first), (char)('A' + (int)second));
    set_status(msg);
    if (xQueueSend(s_gateway_queue, &request, 0) != pdTRUE) {
        set_request_busy(false);
        set_status("Gateway queue unavailable");
        return false;
    }
    return true;
}

bool lora_fire_doublette_game(Maschine first, Maschine second, uint16_t delay_ms)
{
    if (!s_gateway_queue || first < MASCHINE_A || first > MASCHINE_G ||
        second < MASCHINE_A || second > MASCHINE_G || first == second || delay_ms > 10000 ||
        !cop_wifi_is_connected() || !begin_request() || g_store.gatewaySequence == UINT32_MAX) return false;
    g_store.gatewaySequence++; game_store_save();
    GatewayRequest request = {};
    request.kind = GATEWAY_REQUEST_FIRE_PAIR; request.manual = true; request.gameLaunch = true;
    request.machine = first; request.second_machine = second; request.delay_ms = delay_ms;
    request.sequence = g_store.gatewaySequence; copy_gateway_config(&request);
    if (xQueueSend(s_gateway_queue, &request, 0) != pdTRUE) {
        set_request_busy(false); set_status("Gateway queue unavailable"); return false;
    }
    return true;
}

bool lora_gateway_check(void)
{
    if (!s_gateway_queue) {
        set_status("Gateway request unavailable");
        return false;
    }
    if (!cop_wifi_is_connected()) {
        set_status("WiFi not connected");
        set_gateway_state(GATEWAY_UNREACHABLE);
        return false;
    }
    if (!g_store.gatewayUrl[0] || !g_store.gatewayToken[0]) {
        set_status("Gateway not configured");
        set_gateway_state(GATEWAY_NOT_CONFIGURED);
        return false;
    }
    TickType_t now = xTaskGetTickCount();
    if (s_last_health_tick != 0 &&
        (now - s_last_health_tick) < pdMS_TO_TICKS(15000)) {
        set_status("Gateway check throttled (15s)");
        return false;
    }
    if (!begin_request()) {
        set_status("Gateway request already in progress");
        return false;
    }
    GatewayRequest request = {};
    request.kind = GATEWAY_REQUEST_HEALTH;
    request.manual = true;
    request.machine = MASCHINE_A;
    copy_gateway_config(&request);
    set_status("Checking gateway...");
    set_gateway_state(GATEWAY_CHECKING);
    s_last_health_tick = now;
    if (xQueueSend(s_gateway_queue, &request, 0) != pdTRUE) {
        set_request_busy(false);
        set_status("Gateway queue unavailable");
        return false;
    }
    return true;
}

bool lora_request_busy(void)
{
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    bool busy = s_request_busy;
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
    return busy;
}

void lora_copy_status_text(char *out, size_t out_len)
{
    if (!out || out_len == 0) return;
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    snprintf(out, out_len, "%s", s_status);
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
}

GatewayReachability lora_gateway_state(void)
{
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    GatewayReachability state = s_gateway_state;
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
    return state;
}

uint32_t lora_gateway_state_timestamp_ms(void)
{
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    uint32_t timestamp = s_gateway_state_ms;
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
    return timestamp;
}

const char *lora_gateway_state_label(GatewayReachability state)
{
    switch (state) {
        case GATEWAY_NOT_CONFIGURED: return "NOT CONFIGURED";
        case GATEWAY_CHECKING: return "CHECKING";
        case GATEWAY_REACHABLE: return "REACHABLE";
        case GATEWAY_UNREACHABLE: return "UNREACHABLE";
        case GATEWAY_AUTH_FAILED: return "AUTH FAILED";
        default: return "FAILED";
    }
}

void lora_copy_gateway_state_label(char *out, size_t out_len)
{
    if (!out || !out_len) return;
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    snprintf(out, out_len, "%s", lora_gateway_state_label(s_gateway_state));
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
}
