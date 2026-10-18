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
#include "network_trace.h"
#include "esp_http_client.h"
static esp_err_t gateway_timing_event(esp_http_client_event_t *event) {
    if(event->event_id==HTTP_EVENT_ON_CONNECTED)
        esp_http_client_set_timeout_ms(event->client,event->user_data?150:2000);
    return ESP_OK;
}
#include "button_events.h"
#include "gateway_health.h"
#include "game_store.h"
#include "terminal_logs.h"
#include "esp_timer.h"
#include "coprocessor.h"
#include "../../../lora-common/trapmaster_auth.h"
#include "../../../lora-common/fire_link.h"
#include "esp_wifi.h"

static const char *TAG = "lora_stub";
static QueueHandle_t s_gateway_queue;
static SemaphoreHandle_t s_state_mutex;
static char s_status[256] = "Gateway not configured";
static char s_machine_test_status[256];
static bool s_machine_test_running;
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
    uint32_t pressed_ms;
    bool prefetched;
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
// Single-worker cache: one short-lived nonce, scoped to the exact URL/key.
// Gateway NONCE_TTL_MS is 10000. Consume well before expiry and never reuse.
// Also compiled by host tests that isolate only a subset of worker helpers.
[[maybe_unused]] static GatewayRequest s_prefetched;
[[maybe_unused]] static uint32_t s_prefetched_ms;
[[maybe_unused]] static bool s_has_prefetched;
[[maybe_unused]] static uint32_t s_last_fire_ms;
[[maybe_unused]] static unsigned s_transport_retries;
[[maybe_unused]] static bool s_transport_ambiguous;
[[maybe_unused]] static tm_fire_link::Samples s_latency;
[[maybe_unused]] static uint32_t s_send_ms,s_first_byte_ms;
struct FireTrace {uint32_t t0,t1,t2,t3,t4;int status,rssi;unsigned retries;bool prefetched,ack;char machine;};
[[maybe_unused]] static FireTrace s_traces[50];
[[maybe_unused]] static unsigned s_trace_next;
[[maybe_unused]] static uint32_t s_trace_cursor;
[[maybe_unused]] static char s_trace_gateway[32]{};

static void set_status(const char *text)
{
    char log_text[sizeof(s_status)];
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    snprintf(s_status, sizeof(s_status), "%s", text);
    snprintf(log_text, sizeof(log_text), "%s", s_status);
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
    (void)log_text; // Structured shot summary below is the one INFO line.
}

static void publish_machine_test_status(const char *text, bool running)
{
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    snprintf(s_machine_test_status, sizeof(s_machine_test_status), "%s", text);
    s_machine_test_running = running;
    if (s_state_mutex) xSemaphoreGive(s_state_mutex);
    set_status(text);
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

struct FireHttpBody {char body[384]{};size_t length=0;};
static esp_err_t fire_http_event(esp_http_client_event_t *event) {
    if(event->event_id==HTTP_EVENT_HEADERS_SENT&&!s_send_ms)
        s_send_ms=(uint32_t)(esp_timer_get_time()/1000);
    if(event->event_id==HTTP_EVENT_ON_HEADER&&!s_first_byte_ms)
        s_first_byte_ms=(uint32_t)(esp_timer_get_time()/1000);
    if(event->event_id==HTTP_EVENT_ON_DATA&&event->data_len>0) {
        if(!s_first_byte_ms)s_first_byte_ms=(uint32_t)(esp_timer_get_time()/1000);
        auto *b=(FireHttpBody*)event->user_data;
        const size_t n=(size_t)event->data_len;
        if(b&&b->length+n<sizeof(b->body)) {
            memcpy(b->body+b->length,event->data,n);b->length+=n;b->body[b->length]=0;
        }
    }
    return ESP_OK;
}
static bool perform_authenticated_get(const char *url, const char *mac_hex,
                                       int *last_http, int timeout_ms, int attempts)
{
    bool success = false;
    *last_http = 0;
    s_transport_retries=0;
    s_transport_ambiguous=false;
    for (int attempt = 0; attempt < attempts && !success; attempt++) {
        s_transport_retries=attempt;
        esp_http_client_config_t cfg = {};
        cfg.url = url;
        cfg.timeout_ms = timeout_ms;
        cfg.disable_auto_redirect = true;
        FireHttpBody body;
        cfg.event_handler=fire_http_event;cfg.user_data=&body;
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) {
            set_status("Gateway client unavailable");
            break;
        }
        esp_http_client_set_header(client, "X-TrapMaster-Auth", mac_hex);
        network_trace_begin(url,"gateway-command",attempt);
        esp_err_t err = esp_http_client_perform(client);
        network_trace_end(err,err==ESP_OK?esp_http_client_get_status_code(client):0);
        if(err!=ESP_OK) s_transport_ambiguous=true;
        *last_http = esp_http_client_get_status_code(client);
        cJSON *json=cJSON_Parse(body.body);
        bool progress=json&&cJSON_IsTrue(cJSON_GetObjectItem(json,"progress"));
        if(json)cJSON_Delete(json);
        if (err == ESP_OK && *last_http >= 200 && *last_http < 300&&!progress) {
            success = true;
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        // Retry only transport failures, with this exact URL/MAC/nonce. Never
        // acquire another nonce or retry a final busy/auth/storage rejection.
        if (err == ESP_OK&&!progress) break;
        if(progress) {s_transport_ambiguous=true;vTaskDelay(pdMS_TO_TICKS(timeout_ms));}
        if (!success && attempt + 1 < attempts)
            vTaskDelay(pdMS_TO_TICKS(100));
    }
    return success;
}

static bool perform_fire_transport(const GatewayRequest *request,const char *url,
                                  const char *mac,int *status,int timeout,unsigned budget=3) {
    FireLinkResult ws;
    bool completed=button_events_fire(request->gateway_url,request->gateway_token,
        request->nonce_hex,mac,(char)('A'+request->machine),
        request->kind==GATEWAY_REQUEST_FIRE_PAIR?(char)('A'+request->second_machine):0,
        request->delay_ms,timeout,&ws,budget);
    s_send_ms=ws.sentMs;s_first_byte_ms=ws.firstByteMs;
    if(completed) {
        *status=ws.status;s_transport_retries=ws.attempts?ws.attempts-1:0;
        s_transport_ambiguous=false;return ws.status>=200&&ws.status<300;
    }
    if(ws.attempts>=budget) {
        *status=0;s_transport_retries=2;s_transport_ambiguous=true;return false;
    }
    // Connection absent or lost: same nonce and MAC, sharing the TOTAL retry
    // budget with WS. Never replace the action merely because transport changed.
    bool result=perform_authenticated_get(url,mac,status,timeout,(int)(budget-ws.attempts));
    s_transport_retries+=ws.attempts;
    s_transport_ambiguous|=ws.ambiguous;
    return result;
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

static bool perform_health_get(const char *url, const char *mac_hex, HealthFailure *failure,
                               bool background=false)
{
    for (int attempt = 0; attempt < (background?1:2); ++attempt) {
        *failure = {};
        esp_http_client_config_t cfg = {};
        cfg.url = url;
        cfg.timeout_ms = background?150:1500;
        cfg.event_handler=gateway_timing_event;
        cfg.user_data=background?(void*)1:nullptr;
        cfg.disable_auto_redirect = true;
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) { failure->error = ESP_ERR_NO_MEM; return false; }
        esp_http_client_set_header(client, "X-TrapMaster-Auth", mac_hex);
        network_trace_begin(url,"gateway-health",attempt);
        failure->error = esp_http_client_perform(client);
        failure->http_status = esp_http_client_get_status_code(client);
        network_trace_end(failure->error,failure->http_status);
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
    // Per-request network traces aggregate failures; keep the streak detail
    // available in DEBUG instead of repeating the same warning every check.
    ESP_LOGD(TAG, "Gateway check failed reason=%s http=%d esp=%s(%d) errno=%d tls=%d streak=%lu/3 failedSinceBoot=%lu",
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

static bool fetch_gateway_nonce(GatewayRequest *request, int *last_http, bool background=false)
{
    char url[MAX_URL_LEN + 96];
    if (!build_health_url(url, sizeof(url), request)) return false;
    const size_t length = strlen(request->gateway_url);
    const char *suffix = request->gateway_url[length - 1] == '/' ? "nonce" : "/nonce";
    snprintf(url, sizeof(url), "%s%s", request->gateway_url, suffix);
    for (int attempt = 0; attempt < (background?1:3); ++attempt) {
        NonceResponse response = {};
        esp_http_client_config_t cfg = {};
        cfg.url = url;
        cfg.timeout_ms = background?150:2000;
        cfg.disable_auto_redirect = true;
        cfg.event_handler = nonce_http_event;
        cfg.user_data = &response;
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) { set_status("Gateway-Nonce-Client nicht verfügbar."); return false; }
        network_trace_begin(url,"gateway-info",attempt);
        esp_err_t err = esp_http_client_perform(client);
        network_trace_end(err,err==ESP_OK?esp_http_client_get_status_code(client):0);
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

struct GatewayTraceBody {char *bytes;size_t length=0;bool overflow=false;};
static esp_err_t gateway_trace_event(esp_http_client_event_t *e) {
    if(e->event_id==HTTP_EVENT_ON_DATA&&e->data_len>0) {
        auto *b=(GatewayTraceBody*)e->user_data;
        if(b->length+(size_t)e->data_len>=24576)b->overflow=true;
        else {memcpy(b->bytes+b->length,e->data,e->data_len);b->length+=e->data_len;b->bytes[b->length]=0;}
    }
    return ESP_OK;
}
static void fetch_gateway_traces(const GatewayRequest &request) {
    uint8_t digest[32];char auth[65],url[MAX_URL_LEN+80];
    if(!tm_auth::make_health_mac((const uint8_t*)request.gateway_token,strlen(request.gateway_token),digest))return;
    tm_auth::mac_to_hex(digest,auth);
    size_t n=strlen(request.gateway_url);while(n&&request.gateway_url[n-1]=='/')--n;
    snprintf(url,sizeof(url),"%.*s/trace?after=%lu",(int)n,request.gateway_url,(unsigned long)s_trace_cursor);
    char *bytes=(char*)heap_caps_calloc(1,24576,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!bytes)return;
    GatewayTraceBody body{bytes};
    esp_http_client_config_t cfg{};cfg.url=url;cfg.timeout_ms=300;
    cfg.disable_auto_redirect=true;cfg.event_handler=gateway_trace_event;cfg.user_data=&body;
    auto client=esp_http_client_init(&cfg);
    if(!client){heap_caps_free(bytes);return;}
    esp_http_client_set_header(client,"X-TrapMaster-Auth",auth);
    network_trace_begin(url,"gateway-events",0);
    esp_err_t result=esp_http_client_perform(client);
    network_trace_end(result,result==ESP_OK?esp_http_client_get_status_code(client):0);
    const int status=esp_http_client_get_status_code(client);esp_http_client_cleanup(client);
    cJSON *root=result==ESP_OK&&status==200&&!body.overflow?cJSON_Parse(bytes):nullptr;
    heap_caps_free(bytes);if(!root)return;
    cJSON *id=cJSON_GetObjectItem(root,"gatewayId"),*rows=cJSON_GetObjectItem(root,"entries");
    if(cJSON_IsString(id)&&strlen(id->valuestring)<sizeof(s_trace_gateway)&&
       cJSON_IsArray(rows)&&cJSON_GetArraySize(rows)<=50) {
        if(strcmp(s_trace_gateway,id->valuestring)) {
            snprintf(s_trace_gateway,sizeof(s_trace_gateway),"%s",id->valuestring);s_trace_cursor=0;
        }
        cJSON *row;
        cJSON_ArrayForEach(row,rows) {
            cJSON *seq=cJSON_GetObjectItem(row,"seq");
            if(!cJSON_IsNumber(seq)||seq->valuedouble<=s_trace_cursor||seq->valuedouble>UINT32_MAX||
                seq->valuedouble!=(uint32_t)seq->valuedouble)continue;
            // Never forward an arbitrary gateway object (including credentials
            // added by a different/older gateway firmware).
            cJSON *clean=cJSON_CreateObject();
            if(!clean)break;
            cJSON_AddStringToObject(clean,"gatewayId",id->valuestring);
            const char *fields[]={"seq","durationMs","rssi","status","authMs","nvsMs",
                "nvsDurationMs","txStartMs","txDoneMs","ackMs","answerMs"};
            for(const char *field:fields) {
                cJSON *value=cJSON_GetObjectItem(row,field);
                if(cJSON_IsNumber(value))cJSON_AddNumberToObject(clean,field,value->valuedouble);
            }
            cJSON *machine=cJSON_GetObjectItem(row,"machine");
            if(cJSON_IsString(machine)&&strlen(machine->valuestring)==1&&
                machine->valuestring[0]>='A'&&machine->valuestring[0]<='H')
                cJSON_AddStringToObject(clean,"machine",machine->valuestring);
            terminal_log_add("gateway_trace","INFO","Gateway-Funkweg",clean);
            cJSON_Delete(clean);
            s_trace_cursor=(uint32_t)seq->valuedouble;
        }
    }
    cJSON_Delete(root);
}

static bool prepare_fire_request(GatewayRequest *request, char *url, size_t url_len,
                                 char *mac_hex, int *last_http)
{
    // One fresh nonce per operator action (and per selected batch machine).
    // Once signed, retries use the same request and cannot produce a new shot.
    uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
    bool cached=s_has_prefetched && now-s_prefetched_ms<8000 &&
        !strcmp(request->gateway_url,s_prefetched.gateway_url) &&
        !strcmp(request->gateway_token,s_prefetched.gateway_token);
    if(cached) {
        memcpy(request->nonce,s_prefetched.nonce,sizeof(request->nonce));
        memcpy(request->nonce_hex,s_prefetched.nonce_hex,sizeof(request->nonce_hex));
        request->prefetched=true;s_has_prefetched=false;
    } else {
        s_has_prefetched=false;
        if (!fetch_gateway_nonce(request, last_http)) return false;
    }
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

static void record_fire_trace(const GatewayRequest &request,uint32_t start,uint32_t ready,
                              uint32_t end,int status,bool success,int rssi) {
    const FireTrace sample{start,ready,s_send_ms,s_first_byte_ms,end,status,rssi,
        s_transport_retries,request.prefetched,success&&status!=202,(char)('A'+request.machine)};
    if(s_state_mutex)xSemaphoreTake(s_state_mutex,portMAX_DELAY);
    s_traces[s_trace_next++%50]=sample;s_latency.add(end-start);
    if(s_state_mutex)xSemaphoreGive(s_state_mutex);
    cJSON *trace=cJSON_CreateObject();
    cJSON_AddNumberToObject(trace,"durationMs",end-start);
    cJSON_AddNumberToObject(trace,"prepareMs",ready-start);
    cJSON_AddNumberToObject(trace,"retries",sample.retries);
    cJSON_AddBoolToObject(trace,"prefetched",sample.prefetched);
    cJSON_AddNumberToObject(trace,"status",status);
    cJSON_AddNumberToObject(trace,"machine",request.machine);
    cJSON_AddBoolToObject(trace,"ack",sample.ack);
    cJSON_AddNumberToObject(trace,"t0",start);cJSON_AddNumberToObject(trace,"t1",ready);
    cJSON_AddNumberToObject(trace,"t2",sample.t2);cJSON_AddNumberToObject(trace,"t3",sample.t3);
    cJSON_AddNumberToObject(trace,"t4",end);cJSON_AddNumberToObject(trace,"rssi",rssi);
    terminal_log_add("fire_trace",success?"INFO":"WARN","Gateway-Schussanfrage beendet",trace);
    cJSON_Delete(trace);
    ESP_LOGI(TAG,"FIRE %c duration=%lums status=%d retries=%u",sample.machine,
        (unsigned long)(end-start),status,sample.retries);
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
            const uint32_t start=(uint32_t)(esp_timer_get_time()/1000);
            uint32_t ready=start;
            s_send_ms=s_first_byte_ms=0;s_transport_retries=0;
            wifi_ap_record_t ap{};const int rssi=esp_wifi_sta_get_ap_info(&ap)==ESP_OK?ap.rssi:0;
            if (prepare_fire_request(&request, url, sizeof(url), mac_hex, &http_status)) {
                ready=(uint32_t)(esp_timer_get_time()/1000);
                success = perform_fire_transport(&request,url,mac_hex,&http_status,7000);
            }
            s_last_fire_ms=(uint32_t)(esp_timer_get_time()/1000);
            record_fire_trace(request,start,ready,s_last_fire_ms,http_status,success,rssi);
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
        publish_machine_test_status(results, true);
    }
    char final_status[256];
    snprintf(final_status, sizeof(final_status), "%s: %.210s",
             warning ? "TEST MIT WARNUNG BEENDET" : "TEST BEENDET", results);
    publish_machine_test_status(final_status, false);
}

static void gateway_worker(void *arg)
{
    GatewayRequest request;
    char url[MAX_URL_LEN + 96];
    for (;;) {
        if (xQueueReceive(s_gateway_queue, &request, pdMS_TO_TICKS(1000)) != pdTRUE) {
            TickType_t now = xTaskGetTickCount();
            if(cop_wifi_is_connected() && !lora_request_busy() && g_store.gatewayUrl[0] &&
                g_store.gatewayToken[0] && (!s_has_prefetched ||
                (uint32_t)(esp_timer_get_time()/1000)-s_prefetched_ms>=10000) &&
                (uint32_t)(esp_timer_get_time()/1000)-s_last_fire_ms>=2000) {
                GatewayRequest next={};copy_gateway_config(&next);
                int status=0;
                if(fetch_gateway_nonce(&next,&status,true)) {
                    s_prefetched=next;s_prefetched_ms=(uint32_t)(esp_timer_get_time()/1000);
                    s_has_prefetched=true;
                }
                // Never follow nonce preparation with a competing health call.
                continue;
            }
            if((uint32_t)(esp_timer_get_time()/1000)-s_last_fire_ms<2000) continue;
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
            // Includes manually queued health; defer rather than competing with
            // a just-pressed FIRE or its two-second quiet interval.
            if((uint32_t)(esp_timer_get_time()/1000)-s_last_fire_ms<2000) {
                if(request.manual)set_request_busy(false);
                continue;
            }
            HealthFailure failure;
            if (build_health_url(url, sizeof(url), &request) &&
                tm_auth::make_health_mac((const uint8_t *)request.gateway_token,
                                         strlen(request.gateway_token), mac)) {
                tm_auth::mac_to_hex(mac, mac_hex);
                if (cop_wifi_is_connected()) {
                    success = perform_health_get(url, mac_hex, &failure,!request.manual);
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
        s_transport_retries=0;
        s_transport_ambiguous=false;
        s_send_ms=s_first_byte_ms=0;
        uint32_t fire_started=request.pressed_ms?request.pressed_ms:(uint32_t)(esp_timer_get_time()/1000);
        wifi_ap_record_t ap{};int rssi=0;
        if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK)rssi=ap.rssi;
        uint32_t nonce_ready=fire_started;
        if (prepare_fire_request(&request, url, sizeof(url), mac_hex, &last_http)) {
            request_ready = true;
            nonce_ready=(uint32_t)(esp_timer_get_time()/1000);
            success = perform_fire_transport(&request,url,mac_hex,&last_http,
                request.kind == GATEWAY_REQUEST_FIRE_PAIR ? 20000 : 7000);
            // Only an unambiguous pre-admission 401 may acquire a replacement.
            // After any transport ambiguity, retries keep the signed request.
            if(!success && last_http==401 && request.prefetched && !s_transport_ambiguous) {
                const unsigned used=s_transport_retries+1;
                request.prefetched=false;
                if(used<3&&prepare_fire_request(&request,url,sizeof(url),mac_hex,&last_http)) {
                    nonce_ready=(uint32_t)(esp_timer_get_time()/1000);
                    success=perform_fire_transport(&request,url,mac_hex,&last_http,
                        request.kind==GATEWAY_REQUEST_FIRE_PAIR?20000:7000,3-used);
                    s_transport_retries+=used;
                }
            }
        }
        s_last_fire_ms=(uint32_t)(esp_timer_get_time()/1000);
        record_fire_trace(request,fire_started,nonce_ready,s_last_fire_ms,last_http,success,rssi);

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
        if(cop_wifi_is_connected()&&!s_has_prefetched) {
            GatewayRequest next{};copy_gateway_config(&next);int code=0;
            if(fetch_gateway_nonce(&next,&code,true)) {
                s_prefetched=next;s_prefetched_ms=(uint32_t)(esp_timer_get_time()/1000);
                s_has_prefetched=true;
            }
        }
    }
}

static void gateway_trace_worker(void *) {
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if(!cop_wifi_is_connected()||lora_request_busy()||
           (uint32_t)(esp_timer_get_time()/1000)-s_last_fire_ms<2000)continue;
        GatewayRequest scope{};copy_gateway_config(&scope);
        if(scope.gateway_url[0]&&strlen(scope.gateway_token)>=16)fetch_gateway_traces(scope);
    }
}

void lora_stub_init(void)
{
    button_events_init();
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
    if(xTaskCreateWithCaps(gateway_trace_worker,"gateway_trace",6144,nullptr,2,nullptr,
                          MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)!=pdPASS)
        terminal_log_add("sync","WARN","Gateway-Spuren-Worker nicht verfügbar");
    set_gateway_state((g_store.gatewayUrl[0] && g_store.gatewayToken[0])
                          ? GATEWAY_CHECKING : GATEWAY_NOT_CONFIGURED);
}

void lora_copy_latency_text(char *out,size_t cap) {
    uint32_t min,median,p95,max;unsigned count;
    if(s_state_mutex)xSemaphoreTake(s_state_mutex,portMAX_DELAY);
    count=s_latency.count;s_latency.summary(min,median,p95,max);
    if(s_state_mutex)xSemaphoreGive(s_state_mutex);
    snprintf(out,cap,"Letzte %u Würfe (Taste → Antwort)\nMin: %lu ms · Median: %lu ms\nP95: %lu ms · Max: %lu ms",
        count,(unsigned long)min,(unsigned long)median,(unsigned long)p95,(unsigned long)max);
}
void lora_clear_latency() {
    if(s_state_mutex)xSemaphoreTake(s_state_mutex,portMAX_DELAY);
    s_latency.clear();s_trace_next=0;memset(s_traces,0,sizeof(s_traces));
    if(s_state_mutex)xSemaphoreGive(s_state_mutex);
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
    publish_machine_test_status("TEST STARTET: aktive Maschinen werden nacheinander ausgelöst.", true);
    if (xQueueSend(s_gateway_queue, &request, 0) != pdTRUE) {
        set_request_busy(false);
        publish_machine_test_status("Gateway-Warteschlange nicht verfügbar.", false);
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
    request.pressed_ms=(uint32_t)(esp_timer_get_time()/1000);
    s_last_fire_ms=request.pressed_ms;
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
    request.pressed_ms=(uint32_t)(esp_timer_get_time()/1000);
    s_last_fire_ms=request.pressed_ms;
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
    request.pressed_ms=(uint32_t)(esp_timer_get_time()/1000);
    s_last_fire_ms=request.pressed_ms;
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
    request.pressed_ms=(uint32_t)(esp_timer_get_time()/1000);
    s_last_fire_ms=request.pressed_ms;
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

void lora_copy_machine_test_status(char *out, size_t out_len, bool *running)
{
    if (!out || out_len == 0) return;
    if (s_state_mutex) xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    snprintf(out, out_len, "%s", s_machine_test_status);
    if (running) *running = s_machine_test_running;
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
