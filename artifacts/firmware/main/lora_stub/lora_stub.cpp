// ============================================================
// Terminal-to-gateway fire transport.
//
// The terminal deliberately has no LoRa hardware. It queues a short
// HTTP request to the WiFi-connected gateway, which owns the radio.
// ============================================================
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_log.h"
#include "cJSON.h"
#include "lora_stub.h"
#include "gateway_health.h"
#include "game_store.h"
#include "coprocessor.h"
#include "../../../lora-common/trapmaster_auth.h"

static const char *TAG = "lora_stub";
static QueueHandle_t s_gateway_queue;
static SemaphoreHandle_t s_state_mutex;
static char s_status[256] = "Gateway not configured";
static bool s_request_busy = false;
static GatewayReachability s_gateway_state = GATEWAY_NOT_CONFIGURED;
static uint32_t s_gateway_state_ms = 0;
static TickType_t s_last_manual_health_tick = 0;
static TickType_t s_last_auto_health_tick = 0;
static GatewayHealthHistory s_health_history;
static char s_health_url[MAX_URL_LEN];
static char s_health_token[MAX_KEY_LEN];

typedef enum : uint8_t {
    GATEWAY_REQUEST_FIRE,
    GATEWAY_REQUEST_FIRE_PAIR,
    GATEWAY_REQUEST_TEST_ALL,
    GATEWAY_REQUEST_HEALTH,
} GatewayRequestKind;

typedef struct {
    GatewayRequestKind kind;
    Maschine machine;
    Maschine second_machine;
    uint16_t delay_ms;
    uint8_t nonce[tm_auth::NONCE_LEN];
    char nonce_hex[tm_auth::NONCE_HEX_LEN + 1];
    bool manual; // FIRE and operator-initiated health; autonomous health is false
    bool gameLaunch; // only live-game ACKs contribute to clay accounting
    uint8_t machine_mask; // immutable operator-confirmed test selection
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

static bool health_check_throttled(bool manual, TickType_t now)
{
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    TickType_t last = manual ? s_last_manual_health_tick
                            : s_last_auto_health_tick;
    bool throttled = last != 0 &&
                     (now - last) < pdMS_TO_TICKS(15000);
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
    return throttled;
}

static void record_health_check(bool manual, TickType_t now)
{
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    if (manual)
        s_last_manual_health_tick = now;
    else
        s_last_auto_health_tick = now;
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

static void adopt_health_scope(const GatewayRequest &request)
{
    if (!strcmp(s_health_url, request.gateway_url) &&
        !strcmp(s_health_token, request.gateway_token)) return;
    snprintf(s_health_url, sizeof(s_health_url), "%s", request.gateway_url);
    snprintf(s_health_token, sizeof(s_health_token), "%s", request.gateway_token);
    s_health_history.success();
    set_gateway_state(GATEWAY_CHECKING); // another gateway's green is not evidence
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
    int written = snprintf(url, url_len, "%s%s%c&nonce=%s", base, suffix,
                           (char)('A' + (int)request->machine),
                            request->nonce_hex);
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
    int written = snprintf(url, url_len, "%s%s%c&second=%c&delayMs=%u&nonce=%s",
                           base, suffix, (char)('A' + (int)request->machine),
                           (char)('A' + (int)request->second_machine),
                           (unsigned)request->delay_ms,
                            request->nonce_hex);
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
        // Retry only transport failures, with this exact URL/MAC/nonce. Never
        // acquire another nonce or retry a final busy/auth/storage rejection.
        if (err == ESP_OK) break;
        if (!success && attempt + 1 < attempts)
            vTaskDelay(pdMS_TO_TICKS(100));
    }
    return success;
}

struct NonceResponse {
    char body[128];
    size_t length;
    bool overflow;
};

static esp_err_t nonce_http_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
    NonceResponse *response = (NonceResponse *)event->user_data;
    const size_t size = (size_t)event->data_len;
    if (size >= sizeof(response->body) - response->length) {
        response->overflow = true;
    } else if (!response->overflow) {
        memcpy(response->body + response->length, event->data, size);
        response->length += size;
        response->body[response->length] = '\0';
    }
    return ESP_OK;
}

struct HealthFailure {
    esp_err_t error = ESP_OK;
    int socket_errno = 0;
    int tls_error = 0;
    int http_status = 0;
};

static const char *health_failure_reason(const HealthFailure &failure)
{
    if (failure.error == ESP_ERR_INVALID_ARG) return "configuration";
    if (failure.error == ESP_ERR_NO_MEM) return "memory";
    if (failure.http_status == 401 || failure.http_status == 403) return "unauthorized";
    if (failure.http_status >= 300) return "http";
    if (failure.tls_error == ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME) return "DNS";
    if (failure.error == ESP_ERR_TIMEOUT || failure.socket_errno == ETIMEDOUT ||
        failure.tls_error == ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT) return "timeout";
    if (failure.socket_errno == ECONNREFUSED) return "refused";
    if (failure.socket_errno == ENETUNREACH || failure.socket_errno == EHOSTUNREACH)
        return "network-unreachable";
    return "connection";
}

static bool perform_health_get(const char *url, const char *mac_hex, HealthFailure *failure)
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        *failure = {};
        esp_http_client_config_t cfg = {};
        cfg.url = url;
        cfg.timeout_ms = 2000; // bounds connect AND response waits; at most two attempts
        cfg.disable_auto_redirect = true;
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) { failure->error = ESP_ERR_NO_MEM; return false; }
        esp_http_client_set_header(client, "X-TrapMaster-Auth", mac_hex);
        failure->error = esp_http_client_perform(client);
        failure->http_status = esp_http_client_get_status_code(client);
        failure->socket_errno = esp_http_client_get_errno(client);
        int tls_flags = 0;
        esp_http_client_get_and_clear_last_tls_error(client, &failure->tls_error, &tls_flags);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (failure->error == ESP_OK) {
            // A final HTTP response is not a dropped SYN. Never retry a 401.
            return failure->http_status >= 200 && failure->http_status < 300;
        }
        // Immediate fresh connection retry, within this same logical check.
    }
    return false;
}

static void apply_health_result(bool success, const HealthFailure &failure)
{
    if (success) {
        s_health_history.success();
        set_status("Gateway erreichbar - Schlüssel akzeptiert.");
        set_gateway_state(GATEWAY_REACHABLE);
        return;
    }
    bool declare_failed = s_health_history.failure();
    ESP_LOGW(TAG, "Gateway check failed reason=%s http=%d esp=%s(%d) errno=%d tls=%d streak=%lu/3 failedSinceBoot=%lu",
             health_failure_reason(failure), failure.http_status,
             esp_err_to_name(failure.error), (int)failure.error,
             failure.socket_errno, failure.tls_error,
             (unsigned long)s_health_history.consecutive,
             (unsigned long)s_health_history.failed_since_boot);
    // Leave both the visible status text and badge untouched for failures 1/2.
    if (!declare_failed) return;
    if (failure.http_status == 401 || failure.http_status == 403) {
        set_status("Gateway-Schlüssel abgelehnt (3 fehlgeschlagene Prüfungen).");
        set_gateway_state(GATEWAY_AUTH_FAILED);
    } else if (failure.http_status >= 300) {
        set_status("Gateway-Prüfung abgelehnt (3 fehlgeschlagene Prüfungen).");
        set_gateway_state(GATEWAY_FAILED);
    } else {
        set_status("Gateway nicht erreichbar (3 fehlgeschlagene Prüfungen).");
        set_gateway_state(GATEWAY_UNREACHABLE);
    }
}

static bool fetch_gateway_nonce(GatewayRequest *request, int *last_http)
{
    char url[MAX_URL_LEN + 96];
    if (!build_health_url(url, sizeof(url), request)) return false;
    const size_t length = strlen(request->gateway_url);
    const char *suffix = request->gateway_url[length - 1] == '/' ? "nonce" : "/nonce";
    snprintf(url, sizeof(url), "%s%s", request->gateway_url, suffix);
    for (int attempt = 0; attempt < 3; ++attempt) {
        NonceResponse response = {};
        esp_http_client_config_t cfg = {};
        cfg.url = url;
        cfg.timeout_ms = 2000;
        cfg.disable_auto_redirect = true;
        cfg.event_handler = nonce_http_event;
        cfg.user_data = &response;
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) { set_status("Gateway-Nonce-Client nicht verfügbar."); return false; }
        esp_err_t err = esp_http_client_perform(client);
        *last_http = esp_http_client_get_status_code(client);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (err == ESP_OK && *last_http == 200 && !response.overflow) {
            cJSON *root = cJSON_Parse(response.body);
            cJSON *nonce = root ? cJSON_GetObjectItemCaseSensitive(root, "nonce") : nullptr;
            bool valid = cJSON_IsString(nonce) &&
                         tm_auth::nonce_from_hex(nonce->valuestring, request->nonce);
            if (valid) tm_auth::nonce_to_hex(request->nonce, request->nonce_hex);
            if (root) cJSON_Delete(root);
            if (!valid) set_status("Gateway hat eine ungültige Nonce geliefert.");
            return valid;
        }
        if (err == ESP_OK && *last_http != 429) break;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    set_status("Gateway-Nonce nicht verfügbar. Keine Maschine ausgelöst.");
    return false;
}

static bool prepare_fire_request(GatewayRequest *request, char *url, size_t url_len,
                                 char *mac_hex, int *last_http)
{
    // One fresh nonce per operator action (and per selected batch machine).
    // Once signed, retries use the same request and cannot produce a new shot.
    if (!fetch_gateway_nonce(request, last_http)) return false;
    uint8_t mac[tm_auth::MAC_LEN];
    bool valid;
    if (request->kind == GATEWAY_REQUEST_FIRE_PAIR) {
        valid = build_fire_pair_url(url, url_len, request) &&
                tm_auth::make_pair_request_mac((const uint8_t *)request->gateway_token,
                    strlen(request->gateway_token), (uint8_t)('A' + request->machine),
                    (uint8_t)('A' + request->second_machine), request->delay_ms, request->nonce, mac);
    } else {
        valid = build_fire_url(url, url_len, request) &&
                tm_auth::make_request_mac((const uint8_t *)request->gateway_token,
                    strlen(request->gateway_token), (uint8_t)('A' + request->machine), request->nonce, mac);
    }
    if (valid) tm_auth::mac_to_hex(mac, mac_hex);
    return valid;
}

static void perform_machine_test_batch(const GatewayRequest *batch)
{
    char results[256] = "TEST: ";
    bool stopped = false;
    bool warning = false;
    bool previous_completed = false;
    for (int m = MASCHINE_A; m < MASCHINE_COUNT; ++m) {
        if (!(batch->machine_mask & (1u << m))) continue;
        const char *result = "NICHT GESENDET";
        if (!stopped) {
            // Let the previous HTTP/radio/ACK cycle finish, then allow the
            // machinery to settle. Sleep only this worker, never the UI.
            // Fetch the next nonce AFTER the pause so its TTL is not wasted.
            if (previous_completed) vTaskDelay(pdMS_TO_TICKS(2000));
            GatewayRequest request = *batch;
            request.kind = GATEWAY_REQUEST_FIRE;
            request.machine = (Maschine)m;
            request.gameLaunch = false;
            char url[MAX_URL_LEN + 96];
            char mac_hex[tm_auth::MAC_HEX_LEN + 1];
            int http_status = 0;
            bool success = false;
            if (prepare_fire_request(&request, url, sizeof(url), mac_hex, &http_status)) {
                success = perform_authenticated_get(url, mac_hex, &http_status, 7000, 3);
            }
            if (success) {
                previous_completed = true;
                s_health_history.success();
                result = http_status == 202 ? "OHNE ACK" : "OK";
                warning |= http_status == 202;
                set_gateway_state(GATEWAY_REACHABLE);
            } else {
                result = http_status == 409 ? "BELEGT/KONFLIKT" : "FEHLER";
                warning = true;
                stopped = true; // no later, unexpected shots after an HTTP failure
                set_gateway_state(http_status == 409 ? GATEWAY_BUSY :
                    (http_status == 401 || http_status == 403) ? GATEWAY_AUTH_FAILED : GATEWAY_FAILED);
            }
        }
        size_t used = strlen(results);
        snprintf(results + used, sizeof(results) - used, "%c:%s ",
                 (char)('A' + m), result);
        set_status(results);
    }
    char final_status[256];
    snprintf(final_status, sizeof(final_status), "%s: %.210s",
             warning ? "TEST MIT WARNUNG BEENDET" : "TEST BEENDET", results);
    set_status(final_status);
}

static void gateway_worker(void *arg)
{
    GatewayRequest request;
    char url[MAX_URL_LEN + 96];
    for (;;) {
        if (xQueueReceive(s_gateway_queue, &request, pdMS_TO_TICKS(1000)) != pdTRUE) {
            TickType_t now = xTaskGetTickCount();
            if (!g_store.gatewayUrl[0] ||
                !g_store.gatewayToken[0] ||
                health_check_throttled(false, now)) {
                if (!g_store.gatewayUrl[0] || !g_store.gatewayToken[0])
                    set_gateway_state(GATEWAY_NOT_CONFIGURED);
                continue;
            }
            record_health_check(false, now);
            request = {};
            request.kind = GATEWAY_REQUEST_HEALTH;
            copy_gateway_config(&request);
            // FIRE/manual work queued during the idle timeout takes priority.
            // Autonomous health never owns s_request_busy and uses bounded
            // request, so it cannot suppress subsequent FIRE requests.
            GatewayRequest queued;
            if (xQueueReceive(s_gateway_queue, &queued, 0) == pdTRUE)
                request = queued;
        }

        // Bind every request, including FIRE, to its immutable gateway scope.
        // A successful fire before the first poll remains valid evidence.
        adopt_health_scope(request);
        if (request.kind == GATEWAY_REQUEST_TEST_ALL) {
            perform_machine_test_batch(&request);
            set_request_busy(false);
            continue;
        }

        uint8_t mac[tm_auth::MAC_LEN];
        char mac_hex[tm_auth::MAC_HEX_LEN + 1];
        int last_http = 0;
        bool success = false;

        if (request.kind == GATEWAY_REQUEST_HEALTH) {
            HealthFailure failure;
            if (build_health_url(url, sizeof(url), &request) &&
                tm_auth::make_health_mac((const uint8_t *)request.gateway_token,
                                         strlen(request.gateway_token), mac)) {
                tm_auth::mac_to_hex(mac, mac_hex);
                if (cop_wifi_is_connected()) {
                    success = perform_health_get(url, mac_hex, &failure);
                } else {
                    failure.error = ESP_ERR_INVALID_STATE;
                    failure.socket_errno = ENETUNREACH;
                }
            } else {
                failure.error = ESP_ERR_INVALID_ARG;
            }
            apply_health_result(success, failure);
            if (request.manual) set_request_busy(false);
            continue;
        }

        bool request_ready = false;
        if (prepare_fire_request(&request, url, sizeof(url), mac_hex, &last_http)) {
            request_ready = true;
            success = perform_authenticated_get(url, mac_hex, &last_http,
                request.kind == GATEWAY_REQUEST_FIRE_PAIR ? 20000 : 7000, 3);
        }

        if (success) {
            s_health_history.success(); // a successful FIRE is also fresh reachability evidence
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
        } else if (last_http == 409) {
            s_health_history.success();
            set_status("Gateway belegt oder Anfragekonflikt (HTTP 409).");
            set_gateway_state(GATEWAY_BUSY);
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
                          ? GATEWAY_CHECKING : GATEWAY_NOT_CONFIGURED);
}

bool lora_test_enabled_machines(uint8_t confirmed_mask)
{
    static_assert(MASCHINE_COUNT <= 8, "Test mask must fit every machine");
    uint8_t enabled_mask = 0;
    for (int m = MASCHINE_A; m < MASCHINE_COUNT; ++m) {
        if (g_store.maschinenAktiv[m]) {
            enabled_mask |= (1u << m);
        }
    }
    if (!confirmed_mask || confirmed_mask != enabled_mask) {
        set_status("Keine aktiven Maschinen oder Auswahl geändert. Bitte neu bestätigen.");
        return false;
    }
    if (!s_gateway_queue || !cop_wifi_is_connected()) {
        set_status("Test nicht möglich: Gateway-Worker oder WLAN nicht verfügbar.");
        return false;
    }
    GatewayRequest request = {};
    request.kind = GATEWAY_REQUEST_TEST_ALL;
    request.manual = true;
    request.machine_mask = confirmed_mask;
    copy_gateway_config(&request);
    char url[MAX_URL_LEN + 96];
    if (!build_fire_url(url, sizeof(url), &request)) return false;
    if (!begin_request()) {
        set_status("Gateway-Anfrage läuft bereits.");
        return false;
    }
    set_status("TEST STARTET: aktive Maschinen werden nacheinander ausgelöst.");
    if (xQueueSend(s_gateway_queue, &request, 0) != pdTRUE) {
        set_request_busy(false);
        set_status("Gateway-Warteschlange nicht verfügbar.");
        return false;
    }
    return true;
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
    GatewayRequest request = {};
    request.kind = GATEWAY_REQUEST_FIRE;
    request.manual = true;
    request.machine = m;
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
    // share the normal validation flow in a compact local copy.
    if (!s_gateway_queue || m < MASCHINE_A || m >= MASCHINE_COUNT || !cop_wifi_is_connected() ||
        !begin_request()) return false;
    GatewayRequest request = {};
    request.kind = GATEWAY_REQUEST_FIRE; request.manual = true; request.gameLaunch = true;
    request.machine = m; copy_gateway_config(&request);
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
    GatewayRequest request = {};
    request.kind = GATEWAY_REQUEST_FIRE_PAIR;
    request.manual = true;
    request.machine = first;
    request.second_machine = second;
    request.delay_ms = delay_ms;
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
        !cop_wifi_is_connected() || !begin_request()) return false;
    GatewayRequest request = {};
    request.kind = GATEWAY_REQUEST_FIRE_PAIR; request.manual = true; request.gameLaunch = true;
    request.machine = first; request.second_machine = second; request.delay_ms = delay_ms;
    copy_gateway_config(&request);
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
    if (!g_store.gatewayUrl[0] || !g_store.gatewayToken[0]) {
        set_status("Gateway not configured");
        set_gateway_state(GATEWAY_NOT_CONFIGURED);
        return false;
    }
    TickType_t now = xTaskGetTickCount();
    if (health_check_throttled(true, now)) {
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
    // Do not replace last good feedback just because a check was requested.
    record_health_check(true, now);
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
        case GATEWAY_BUSY: return "BELEGT / KONFLIKT";
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
