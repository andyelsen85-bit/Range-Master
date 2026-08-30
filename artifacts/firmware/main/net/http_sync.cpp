// ============================================================
// Portal API HTTP sync client
// Mirrors emulator gameStore.ts syncSpiele / syncSpieler logic
// ============================================================
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_mac.h"
#include "cJSON.h"

#include "http_sync.h"
#include "game_store.h"
#include "app_config.h"
#include "coprocessor.h"
#include "offline_cache.h"

static const char *TAG = "http_sync";

#define HTTP_BUF_SIZE  (32 * 1024)
static portMUX_TYPE s_error_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_last_error[384];
static void set_http_error(const char *operation, const char *path,
                           const char *reason);
static esp_err_t http_post_json(const char *path, const char *body,
                                char *resp_buf, size_t resp_cap);
static esp_err_t http_get_json(const char *path, char *resp_buf,
                               size_t resp_cap);

// HTTP parsing and all persistence deliberately happen outside this window.
// It only serializes the small in-RAM publication with the LVGL timer.
static bool sync_commit_begin(const char *dataset, TickType_t *started)
{
    if (!store_sync_commit_begin()) {
        set_http_error("SYNC", dataset, "UI commit acknowledgement timed out");
        return false;
    }
    *started = xTaskGetTickCount();
    return true;
}

static void sync_commit_end(const char *dataset, TickType_t started)
{
    TickType_t elapsed = xTaskGetTickCount() - started;
    store_sync_commit_end();
    ESP_LOGI(TAG, "Sync commit dataset=%s duration=%ums", dataset,
             (unsigned)(elapsed * portTICK_PERIOD_MS));
}

// Outbox acknowledgements affect both the queue and the bill projection.
// Keep those RAM mutations together under the UI window; persistence follows
// after release so NVS/FAT can never extend a pause.
static esp_err_t finish_kredit_sync_publication(const KreditEvent *snapshot,
                                                int count, bool delivered)
{
    TickType_t started;
    if (!sync_commit_begin("credit-outbox", &started)) return ESP_ERR_TIMEOUT;
    store_finish_kredit_event_sync_commit(snapshot, count, delivered);
    store_rebuild_bill_projection();
    sync_commit_end("credit-outbox", started);
    game_store_save();
    return ESP_OK;
}

static esp_err_t finish_verkauf_sync_publication(const VerkaufEvent *snapshot,
                                                 int count, bool delivered)
{
    TickType_t started;
    if (!sync_commit_begin("sales-outbox", &started)) return ESP_ERR_TIMEOUT;
    store_finish_verkauf_sync_commit(snapshot, count, delivered);
    store_rebuild_bill_projection();
    sync_commit_end("sales-outbox", started);
    game_store_save();
    return ESP_OK;
}

static bool finish_payment_sync_publication(const PaymentEvent *snapshot, int count,
                                            const char *const *accepted_ids,
                                            int accepted_count, const char *error)
{
    TickType_t started;
    if (!sync_commit_begin("payment-outbox", &started)) return false;
    bool changed = store_finish_payment_sync_commit(snapshot, count, accepted_ids,
                                                    accepted_count, error);
    store_rebuild_bill_projection();
    sync_commit_end("payment-outbox", started);
    if (changed) game_store_save();
    return changed;
}

typedef struct {
    bool available;
    char token[OFFLINE_CACHE_SECTION_COUNT][CACHE_MANIFEST_TOKEN_LEN];
} SyncManifest;

static bool fetch_manifest(SyncManifest *manifest)
{
    if (!manifest) return false;
    memset(manifest, 0, sizeof(*manifest));
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char date[11], path[80]; strftime(date, sizeof(date), "%Y-%m-%d", &tm);
    snprintf(path, sizeof(path), "/api/sync/manifest?datum=%s", date);
    char response[2048];
    if (http_get_json(path, response, sizeof(response)) != ESP_OK) return false;
    cJSON *root = cJSON_Parse(response);
    cJSON *schema = root ? cJSON_GetObjectItemCaseSensitive(root, "schemaVersion") : NULL;
    cJSON *revs = root ? cJSON_GetObjectItemCaseSensitive(root, "revisions") : NULL;
    static const char *keys[OFFLINE_CACHE_SECTION_COUNT] = {
        "roster", "productCatalog", "gameHistory", "dailyCredits", "dailySales", "dailyBillSummary"
    };
    bool ok = cJSON_IsNumber(schema) && schema->valueint == 1 && cJSON_IsObject(revs);
    for (int i = 0; ok && i < OFFLINE_CACHE_SECTION_COUNT; ++i) {
        cJSON *value = cJSON_GetObjectItemCaseSensitive(revs, keys[i]);
        if (!cJSON_IsString(value) || strlen(value->valuestring) >= CACHE_MANIFEST_TOKEN_LEN)
            ok = false;
        else
            snprintf(manifest->token[i], sizeof(manifest->token[i]), "%s", value->valuestring);
    }
    if (root) cJSON_Delete(root);
    manifest->available = ok;
    if (!ok) ESP_LOGW(TAG, "Sync manifest unavailable/incompatible; using full pulls");
    return ok;
}

static bool manifest_changed(const SyncManifest *manifest, OfflineCacheSection section)
{
    if (section >= OFFLINE_CACHE_CREDITS) {
        time_t now = time(NULL); struct tm tm; char date[11];
        localtime_r(&now, &tm); strftime(date, sizeof(date), "%Y-%m-%d", &tm);
        if (strcmp(g_store.cacheManifestDailyDate, date) != 0) return true;
    }
    return !manifest || !manifest->available ||
           strcmp(g_store.cacheManifestTokens[section], manifest->token[section]) != 0;
}

static void commit_manifest_token(const SyncManifest *manifest, OfflineCacheSection section)
{
    if (manifest && manifest->available &&
        !offline_cache_set_manifest_token(section, manifest->token[section]))
        ESP_LOGW(TAG, "Snapshot cached but manifest token could not be committed");
}

static void redact_query(const char *path, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!path) {
        snprintf(out, out_size, "(null)");
        return;
    }
    size_t len = strcspn(path, "?");
    snprintf(out, out_size, "%.*s%s", (int)len, path,
             path[len] == '?' ? "?[redacted]" : "");
}

#define TERMINAL_CONFIG_SCHEMA_VERSION 1

typedef struct {
    Modus modus;
    bool maschinenAktiv[MASCHINE_COUNT];
    char apiUrl[MAX_URL_LEN];
    char gatewayUrl[MAX_URL_LEN];
    char gatewayToken[MAX_KEY_LEN];
    char wifiSsid[TM_MAX_SSID_LEN];
    char wifiPass[MAX_PASS_LEN];
    bool autoSyncEnabled;
    uint32_t autoSyncSeconds;
    uint32_t billingSyncSeconds;
    bool clickSoundEnabled;
    CustomSequenzEintrag customSequenzen[4][CUSTOM_SEQ_MAX];
    int customSequenzLen[4];
    int customLaeufe[4];
} TerminalConfigSnapshot;

static void set_http_error(const char *operation, const char *path,
                           const char *reason)
{
    portENTER_CRITICAL(&s_error_lock);
    snprintf(s_last_error, sizeof(s_last_error), "%s %s: %s",
             operation, path, reason);
    portEXIT_CRITICAL(&s_error_lock);
}

void http_sync_copy_last_error(char *out, size_t out_len)
{
    if (!out || !out_len) return;
    portENTER_CRITICAL(&s_error_lock);
    snprintf(out, out_len, "%s", s_last_error);
    portEXIT_CRITICAL(&s_error_lock);
}

static void terminal_id(char *out, size_t out_len)
{
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        snprintf(out, out_len, "ESP32-P4-UNKNOWN");
        return;
    }
    snprintf(out, out_len, "ESP32-P4-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static cJSON *config_snapshot_to_json(void)
{
    cJSON *cfg = cJSON_CreateObject();
    if (!cfg) return NULL;
    cJSON_AddNumberToObject(cfg, "modus", (int)g_store.modus);
    cJSON *machines = cJSON_AddArrayToObject(cfg, "maschinenAktiv");
    for (int i = 0; i < MASCHINE_COUNT; ++i)
        cJSON_AddItemToArray(machines, cJSON_CreateBool(g_store.maschinenAktiv[i]));
    cJSON_AddStringToObject(cfg, "apiUrl", g_store.apiUrl);
    cJSON_AddStringToObject(cfg, "gatewayUrl", g_store.gatewayUrl);
    cJSON_AddStringToObject(cfg, "gatewayToken", g_store.gatewayToken);
    cJSON_AddStringToObject(cfg, "wifiSsid", g_store.wifiSsid);
    cJSON_AddStringToObject(cfg, "wifiPass", g_store.wifiPass);
    cJSON_AddBoolToObject(cfg, "autoSyncEnabled", g_store.autoSyncEnabled);
    cJSON_AddNumberToObject(cfg, "autoSyncSeconds", g_store.autoSyncSeconds);
    cJSON_AddNumberToObject(cfg, "billingSyncSeconds", g_store.billingSyncSeconds);
    cJSON_AddBoolToObject(cfg, "clickSoundEnabled", g_store.clickSoundEnabled);
    cJSON *all_custom = cJSON_AddArrayToObject(cfg, "customSequenzen");
    for (int c = 0; c < 4; ++c) {
        cJSON *sequence = cJSON_CreateArray();
        cJSON_AddItemToArray(all_custom, sequence);
        for (int i = 0; i < g_store.customSequenzLen[c]; ++i) {
            const CustomSequenzEintrag *entry = &g_store.customSequenzen[c][i];
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "maschine", (int)entry->maschine);
            cJSON_AddBoolToObject(item, "isDoublette", entry->isDoublette);
            cJSON_AddNumberToObject(item, "partner", (int)entry->partner);
            cJSON_AddNumberToObject(item, "delayMs", entry->delayMs);
            cJSON_AddItemToArray(sequence, item);
        }
    }
    cJSON *runs = cJSON_AddArrayToObject(cfg, "customLaeufe");
    for (int c = 0; c < 4; ++c)
        cJSON_AddItemToArray(runs, cJSON_CreateNumber(g_store.customLaeufe[c]));
    return cfg;
}

static bool publish_backup_state(bool sync_publication, const char *status,
                                 const char *updated_at)
{
    TickType_t started = 0;
    if (sync_publication && !sync_commit_begin("config-backup", &started))
        return false;
    if (updated_at)
        snprintf(g_store.lastConfigBackupAt, sizeof(g_store.lastConfigBackupAt),
                 "%s", updated_at);
    snprintf(g_store.configBackupStatus, sizeof(g_store.configBackupStatus), "%s", status);
    if (sync_publication) sync_commit_end("config-backup", started);
    // Persistence is purposefully after the publication window.
    game_store_save();
    return true;
}

static esp_err_t http_backup_config_impl(bool sync_publication)
{
    char id[40];
    terminal_id(id, sizeof(id));
    cJSON *root = cJSON_CreateObject();
    cJSON *cfg = config_snapshot_to_json();
    if (!root || !cfg) {
        if (root) cJSON_Delete(root);
        if (cfg) cJSON_Delete(cfg);
        (void)publish_backup_state(sync_publication, "BACKUP FEELER: NET GENUG SPEICHER", NULL);
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "terminalId", id);
    cJSON_AddNumberToObject(root, "schemaVersion", TERMINAL_CONFIG_SCHEMA_VERSION);
    cJSON_AddStringToObject(root, "firmwareVersion", APP_VERSION);
    cJSON_AddItemToObject(root, "configuration", cfg);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return ESP_ERR_NO_MEM;

    char response[512];
    esp_err_t err = http_post_json("/api/sync/config-backup", body,
                                   response, sizeof(response));
    cJSON_free(body);
    if (err != ESP_OK) {
        return publish_backup_state(sync_publication,
                                    "BACKUP FEELER: PORTAL NET ERREECHBAR", NULL)
            ? err : ESP_ERR_TIMEOUT;
    }
    cJSON *response_root = cJSON_Parse(response);
    cJSON *backup = response_root ? cJSON_GetObjectItem(response_root, "backup") : NULL;
    cJSON *created = backup ? cJSON_GetObjectItem(backup, "updatedAt") : NULL;
    const char *updated_at = cJSON_IsString(created) ? created->valuestring : NULL;
    bool published = publish_backup_state(sync_publication, "BACKUP ERFOLLEGRÄICH", updated_at);
    if (response_root) cJSON_Delete(response_root);
    return published ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t http_backup_config(void)
{
    return http_backup_config_impl(false);
}

static bool json_string_into(cJSON *obj, const char *key, char *out, size_t cap)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(value) || strlen(value->valuestring) >= cap) return false;
    snprintf(out, cap, "%s", value->valuestring);
    return true;
}

static bool parse_config_snapshot(cJSON *cfg, TerminalConfigSnapshot *out)
{
    if (!cJSON_IsObject(cfg) || !out) return false;
    memset(out, 0, sizeof(*out));
    cJSON *modus = cJSON_GetObjectItemCaseSensitive(cfg, "modus");
    cJSON *machines = cJSON_GetObjectItemCaseSensitive(cfg, "maschinenAktiv");
    cJSON *auto_enabled = cJSON_GetObjectItemCaseSensitive(cfg, "autoSyncEnabled");
    cJSON *auto_seconds = cJSON_GetObjectItemCaseSensitive(cfg, "autoSyncSeconds");
    cJSON *billing_seconds = cJSON_GetObjectItemCaseSensitive(cfg, "billingSyncSeconds");
    cJSON *sound = cJSON_GetObjectItemCaseSensitive(cfg, "clickSoundEnabled");
    cJSON *sequences = cJSON_GetObjectItemCaseSensitive(cfg, "customSequenzen");
    cJSON *runs = cJSON_GetObjectItemCaseSensitive(cfg, "customLaeufe");
    if (!cJSON_IsNumber(modus) || modus->valueint < 0 || modus->valueint >= MODUS_COUNT ||
        !cJSON_IsArray(machines) || cJSON_GetArraySize(machines) != MASCHINE_COUNT ||
        !cJSON_IsBool(auto_enabled) || !cJSON_IsNumber(auto_seconds) ||
        auto_seconds->valuedouble < AUTO_SYNC_MIN_SECONDS ||
        auto_seconds->valuedouble > AUTO_SYNC_MAX_SECONDS ||
        (billing_seconds && (!cJSON_IsNumber(billing_seconds) ||
         billing_seconds->valuedouble < BILLING_SYNC_MIN_SECONDS ||
         billing_seconds->valuedouble > BILLING_SYNC_MAX_SECONDS)) ||
        !cJSON_IsBool(sound) || !cJSON_IsArray(sequences) ||
        cJSON_GetArraySize(sequences) != 4 || !cJSON_IsArray(runs) ||
        cJSON_GetArraySize(runs) != 4) return false;
    if (!json_string_into(cfg, "apiUrl", out->apiUrl, sizeof(out->apiUrl)) ||
        !json_string_into(cfg, "gatewayUrl", out->gatewayUrl, sizeof(out->gatewayUrl)) ||
        !json_string_into(cfg, "gatewayToken", out->gatewayToken, sizeof(out->gatewayToken)) ||
        !json_string_into(cfg, "wifiSsid", out->wifiSsid, sizeof(out->wifiSsid)) ||
        !json_string_into(cfg, "wifiPass", out->wifiPass, sizeof(out->wifiPass))) return false;
    out->modus = (Modus)modus->valueint;
    out->autoSyncEnabled = cJSON_IsTrue(auto_enabled);
    out->autoSyncSeconds = (uint32_t)auto_seconds->valuedouble;
    out->billingSyncSeconds = billing_seconds
        ? (uint32_t)billing_seconds->valuedouble
        : BILLING_SYNC_DEFAULT_SECONDS;
    out->clickSoundEnabled = cJSON_IsTrue(sound);
    for (int i = 0; i < MASCHINE_COUNT; ++i) {
        cJSON *active = cJSON_GetArrayItem(machines, i);
        if (!cJSON_IsBool(active)) return false;
        out->maschinenAktiv[i] = cJSON_IsTrue(active);
    }
    for (int c = 0; c < 4; ++c) {
        cJSON *sequence = cJSON_GetArrayItem(sequences, c);
        cJSON *run = cJSON_GetArrayItem(runs, c);
        int len = cJSON_IsArray(sequence) ? cJSON_GetArraySize(sequence) : -1;
        if (len < 0 || len > CUSTOM_SEQ_MAX || !cJSON_IsNumber(run) ||
            run->valueint < 1 || run->valueint > 2) return false;
        out->customSequenzLen[c] = len;
        out->customLaeufe[c] = run->valueint;
        for (int i = 0; i < len; ++i) {
            cJSON *item = cJSON_GetArrayItem(sequence, i);
            cJSON *machine = cJSON_GetObjectItemCaseSensitive(item, "maschine");
            cJSON *is_double = cJSON_GetObjectItemCaseSensitive(item, "isDoublette");
            cJSON *partner = cJSON_GetObjectItemCaseSensitive(item, "partner");
            cJSON *delay = cJSON_GetObjectItemCaseSensitive(item, "delayMs");
            if (!cJSON_IsObject(item) || !cJSON_IsNumber(machine) ||
                machine->valueint < 0 || machine->valueint >= MASCHINE_COUNT ||
                !cJSON_IsBool(is_double) || !cJSON_IsNumber(partner) ||
                partner->valueint < 0 || partner->valueint >= MASCHINE_COUNT ||
                !cJSON_IsNumber(delay) || delay->valueint < 0 ||
                delay->valueint > 10000) return false;
            out->customSequenzen[c][i] = (CustomSequenzEintrag){
                .maschine = (Maschine)machine->valueint,
                .partner = (Maschine)partner->valueint,
                .isDoublette = cJSON_IsTrue(is_double) != 0,
                .delayMs = (uint16_t)delay->valueint,
            };
        }
    }
    return true;
}

static void apply_config_snapshot(const TerminalConfigSnapshot *cfg)
{
    g_store.modus = cfg->modus;
    memcpy(g_store.maschinenAktiv, cfg->maschinenAktiv, sizeof(g_store.maschinenAktiv));
    snprintf(g_store.apiUrl, sizeof(g_store.apiUrl), "%s", cfg->apiUrl);
    snprintf(g_store.gatewayUrl, sizeof(g_store.gatewayUrl), "%s", cfg->gatewayUrl);
    snprintf(g_store.gatewayToken, sizeof(g_store.gatewayToken), "%s", cfg->gatewayToken);
    snprintf(g_store.wifiSsid, sizeof(g_store.wifiSsid), "%s", cfg->wifiSsid);
    snprintf(g_store.wifiPass, sizeof(g_store.wifiPass), "%s", cfg->wifiPass);
    g_store.autoSyncEnabled = cfg->autoSyncEnabled;
    g_store.autoSyncSeconds = cfg->autoSyncSeconds;
    g_store.billingSyncSeconds = cfg->billingSyncSeconds;
    g_store.clickSoundEnabled = cfg->clickSoundEnabled;
    memcpy(g_store.customSequenzen, cfg->customSequenzen, sizeof(g_store.customSequenzen));
    memcpy(g_store.customSequenzLen, cfg->customSequenzLen, sizeof(g_store.customSequenzLen));
    memcpy(g_store.customLaeufe, cfg->customLaeufe, sizeof(g_store.customLaeufe));
}

esp_err_t http_restore_config(const char *restore_code)
{
    if (!restore_code || strlen(restore_code) != 12) return ESP_ERR_INVALID_ARG;
    for (const char *p = restore_code; *p; ++p)
        if (!( (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
               (*p >= 'A' && *p <= 'F') )) return ESP_ERR_INVALID_ARG;
    char id[40], path[192];
    terminal_id(id, sizeof(id));
    snprintf(path, sizeof(path), "/api/sync/config-restore?code=%s&terminalId=%s",
             restore_code, id);
    char *response = (char *)heap_caps_malloc(HTTP_BUF_SIZE, MALLOC_CAP_SPIRAM);
    if (!response) return ESP_ERR_NO_MEM;
    esp_err_t err = http_get_json(path, response, HTTP_BUF_SIZE);
    if (err != ESP_OK) {
        snprintf(g_store.configBackupStatus, sizeof(g_store.configBackupStatus),
                 "RESTORE FEELER: CODE ODER PORTAL");
        free(response);
        game_store_save();
        return err;
    }
    cJSON *root = cJSON_Parse(response);
    free(response);
    cJSON *version = root ? cJSON_GetObjectItemCaseSensitive(root, "schemaVersion") : NULL;
    cJSON *configuration = root ? cJSON_GetObjectItemCaseSensitive(root, "configuration") : NULL;
    TerminalConfigSnapshot snapshot;
    bool valid = cJSON_IsNumber(version) &&
                 version->valueint == TERMINAL_CONFIG_SCHEMA_VERSION &&
                 parse_config_snapshot(configuration, &snapshot);
    if (!valid) {
        if (root) cJSON_Delete(root);
        snprintf(g_store.configBackupStatus, sizeof(g_store.configBackupStatus),
                 "RESTORE FEELER: BACKUP NET KOMPATIBEL");
        game_store_save();
        return ESP_ERR_INVALID_RESPONSE;
    }
    apply_config_snapshot(&snapshot);
    snprintf(g_store.configBackupStatus, sizeof(g_store.configBackupStatus),
             "RESTORE ERFOLLEGRÄICH - NEISTART EMPFOHL");
    game_store_save();
    cJSON_Delete(root);
    return ESP_OK;
}

// ── UTF-8 → display-safe ASCII ────────────────────────────────
// Replaces accented / umlaut characters (ä ö ü é à è …) with
// uppercase ASCII equivalents the Montserrat font can render.
// German umlauts follow the standard ae/oe/ue expansion.
static void utf8_to_display(char *dst, const char *src, size_t dst_max)
{
    size_t di = 0;
    const unsigned char *s = (const unsigned char *)src;
    while (*s && di + 1 < dst_max) {
        unsigned char c = *s;
        if (c < 0x80) {
            // Plain ASCII — pass through as-is
            dst[di++] = (char)c;
            s++;
        } else if (c == 0xC3 && s[1]) {
            // Latin-1 Supplement block (U+00C0–U+00FF)
            unsigned char n = s[1];
            const char *rep = NULL;
            if      (n == 0x84 || n == 0xA4) rep = "AE"; // Ä / ä
            else if (n == 0x96 || n == 0xB6) rep = "OE"; // Ö / ö
            else if (n == 0x9C || n == 0xBC) rep = "UE"; // Ü / ü
            else if (n == 0x9F)              rep = "SS"; // ß
            else if (n==0x80||n==0xA0||n==0x81||n==0xA1||
                     n==0x82||n==0xA2||n==0x83||n==0xA3) rep = "A"; // À Á Â Ã
            else if (n == 0x87 || n == 0xA7)             rep = "C"; // Ç / ç
            else if (n==0x88||n==0xA8||n==0x89||n==0xA9||
                     n==0x8A||n==0xAA||n==0x8B||n==0xAB) rep = "E"; // È É Ê Ë
            else if (n==0x8C||n==0xAC||n==0x8D||n==0xAD||
                     n==0x8E||n==0xAE||n==0x8F||n==0xAF) rep = "I"; // Ì Í Î Ï
            else if (n == 0x91 || n == 0xB1)             rep = "N"; // Ñ / ñ
            else if (n==0x92||n==0xB2||n==0x93||n==0xB3||
                     n==0x94||n==0xB4||n==0x95||n==0xB5) rep = "O"; // Ò Ó Ô Õ
            else if (n==0x99||n==0xB9||n==0x9A||n==0xBA||
                     n==0x9B||n==0xBB)                   rep = "U"; // Ù Ú Û
            s += 2;
            if (rep) {
                for (int i = 0; rep[i] && di + 1 < dst_max; i++)
                    dst[di++] = rep[i];
            }
        } else {
            // Skip any other multibyte sequence
            if      (c < 0xE0) s += 2;
            else if (c < 0xF0) s += 3;
            else               s += 4;
        }
    }
    dst[di] = '\0';
}

// ── HTTP response accumulator ─────────────────────────────────
typedef struct {
    char  *buf;
    int    len;
    int    cap;
    bool   truncated;
} http_acc_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_acc_t *acc = (http_acc_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && acc) {
        int available = acc->cap - acc->len - 1;
        if (available > 0) {
            int copy = evt->data_len < available ? evt->data_len : available;
            memcpy(acc->buf + acc->len, evt->data, copy);
            acc->len += copy;
            acc->buf[acc->len] = '\0';
            if (copy != evt->data_len) acc->truncated = true;
        } else {
            acc->truncated = true;
        }
    }
    return ESP_OK;
}

// ── Generic POST helper (with retry) ─────────────────────────
static esp_err_t http_post_json(const char *path, const char *body,
                                char *resp_buf, size_t resp_cap)
{
    char diagnostic_path[160];
    redact_query(path, diagnostic_path, sizeof(diagnostic_path));
    if (!path || !body || (resp_buf && resp_cap < 2)) {
        set_http_error("POST", diagnostic_path, "invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }
    char url[256];
    int url_len = snprintf(url, sizeof(url), "%s%s", g_store.apiUrl, path);
    if (url_len < 0 || url_len >= (int)sizeof(url)) {
        set_http_error("POST", diagnostic_path, "URL too long");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (resp_buf) resp_buf[0] = '\0';
        http_acc_t acc = { .buf = resp_buf, .len = 0, .cap = (int)resp_cap,
                           .truncated = false };

        esp_http_client_config_t cfg = {};
        cfg.url               = url;
        cfg.event_handler     = http_event_handler;
        cfg.user_data         = resp_buf ? &acc : NULL;
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.timeout_ms        = 12000;
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) {
            set_http_error("POST", diagnostic_path, "client init failed");
            return ESP_ERR_NO_MEM;
        }
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_header(client, "x-api-key", g_store.apiKey);
        esp_http_client_set_post_field(client, body, strlen(body));

        err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(client);
            if (status < 200 || status >= 300) {
                ESP_LOGW(TAG, "POST %s → HTTP %d", diagnostic_path, status);
                char reason[32];
                snprintf(reason, sizeof(reason), "HTTP %d", status);
                set_http_error("POST", diagnostic_path, reason);
                err = ESP_FAIL;
            }
        } else {
            ESP_LOGW(TAG, "POST %s attempt %d/3 failed: %s", diagnostic_path, attempt, esp_err_to_name(err));
            set_http_error("POST", diagnostic_path, esp_err_to_name(err));
        }
        if (acc.truncated) {
            set_http_error("POST", diagnostic_path, "response truncated");
            err = ESP_ERR_INVALID_SIZE;
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (err == ESP_OK) break;
        if (attempt < 3) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return err;
}

// ── Generic GET helper (with retry) ──────────────────────────
static esp_err_t http_get_json(const char *path,
                               char *resp_buf, size_t resp_cap)
{
    char diagnostic_path[160];
    redact_query(path, diagnostic_path, sizeof(diagnostic_path));
    if (!path || !resp_buf || resp_cap < 2) {
        set_http_error("GET", diagnostic_path, "invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }
    char url[256];
    int url_len = snprintf(url, sizeof(url), "%s%s", g_store.apiUrl, path);
    if (url_len < 0 || url_len >= (int)sizeof(url)) {
        set_http_error("GET", diagnostic_path, "URL too long");
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= 3; attempt++) {
        resp_buf[0] = '\0';
        http_acc_t acc = { .buf = resp_buf, .len = 0, .cap = (int)resp_cap,
                           .truncated = false };

        esp_http_client_config_t cfg = {};
        cfg.url               = url;
        cfg.event_handler     = http_event_handler;
        cfg.user_data         = &acc;
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.timeout_ms        = 12000;
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) {
            set_http_error("GET", diagnostic_path, "client init failed");
            return ESP_ERR_NO_MEM;
        }
        esp_http_client_set_header(client, "x-api-key", g_store.apiKey);

        err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(client);
            if (status < 200 || status >= 300) {
                ESP_LOGW(TAG, "GET %s → HTTP %d", diagnostic_path, status);
                char reason[32];
                snprintf(reason, sizeof(reason), "HTTP %d", status);
                set_http_error("GET", diagnostic_path, reason);
                err = ESP_FAIL;
            }
        } else {
            ESP_LOGW(TAG, "GET %s attempt %d/3 failed: %s", diagnostic_path, attempt, esp_err_to_name(err));
            set_http_error("GET", diagnostic_path, esp_err_to_name(err));
        }
        if (acc.truncated) {
            set_http_error("GET", diagnostic_path, "response truncated");
            err = ESP_ERR_INVALID_SIZE;
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (err == ESP_OK) break;
        if (attempt < 3) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return err;
}

// ── Map Modus enum to the API-expected string ─────────────────
static const char *modus_api_str(Modus m)
{
    switch (m) {
        case MODUS_NORMAL:   return "NORMAL";
        case MODUS_CUSTOM_1: return "CUSTOM_1";
        case MODUS_CUSTOM_2: return "CUSTOM_2";
        case MODUS_CUSTOM_3: return "CUSTOM_3";
        case MODUS_CUSTOM_4: return "CUSTOM_4";
        default:             return "NORMAL";
    }
}

// ── Build JSON for a PendingGame ──────────────────────────────
static cJSON *pending_game_to_json(const PendingGame *g)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "externalId", g->externalId);
    // API requires z.string().datetime() — use finishedAt (real UTC) so the
    // portal shows the actual game completion time, not midnight.
    // Fall back to datum+T00:00:00.000Z if finishedAt is somehow empty.
    if (g->finishedAt[0]) {
        cJSON_AddStringToObject(obj, "datum", g->finishedAt);
    } else {
        char datum_iso[25] = "1970-01-01T00:00:00.000Z";
        if (g->datum[0]) snprintf(datum_iso, sizeof(datum_iso), "%sT00:00:00.000Z", g->datum);
        cJSON_AddStringToObject(obj, "datum", datum_iso);
    }
    cJSON_AddStringToObject(obj, "modus", modus_api_str(g->modus));
    cJSON_AddNumberToObject(obj, "lauf",           g->lauf);
    cJSON_AddNumberToObject(obj, "taubenProLauf",  g->taubenProLauf);
    cJSON_AddNumberToObject(obj, "confirmedLaunches", g->confirmedLaunches);
    cJSON_AddBoolToObject  (obj, "abgeschlossen",  g->abgeschlossen);

    cJSON *teil = cJSON_CreateArray();
    for (int i = 0; i < g->teilnahmen_count; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddNumberToObject(t, "spielerId",   g->teilnahmen[i].spielerId);
        cJSON_AddNumberToObject(t, "startPosten", g->teilnahmen[i].startPosten);
        cJSON_AddNumberToObject(t, "punkte",      g->teilnahmen[i].punkte);
        cJSON_AddNumberToObject(t, "lauf",        g->teilnahmen[i].lauf);
        cJSON_AddItemToArray(teil, t);
    }
    cJSON_AddItemToObject(obj, "teilnahmen", teil);

    cJSON *erg = cJSON_CreateArray();
    for (int i = 0; i < g->ergebnisse_count; i++) {
        const Ergebnis *e = &g->ergebnisse[i];
        cJSON *ej = cJSON_CreateObject();
        cJSON_AddNumberToObject(ej, "spielerId", e->spielerId);
        cJSON_AddNumberToObject(ej, "lauf",      e->lauf);
        cJSON_AddNumberToObject(ej, "taube",     e->taube);
        char ml[2] = {maschine_label(e->maschine)[0], '\0'};
        cJSON_AddStringToObject(ej, "maschine",  ml);
        cJSON_AddNumberToObject(ej, "posten",    e->posten);
        cJSON_AddBoolToObject  (ej, "schuss1",   e->schuss1);
        cJSON_AddBoolToObject  (ej, "schuss2",   e->schuss2);
        cJSON_AddNumberToObject(ej, "punkte",    e->punkte);
        cJSON_AddBoolToObject  (ej, "wiederholt",e->wiederholt);
        cJSON_AddItemToArray(erg, ej);
    }
    cJSON_AddItemToObject(obj, "ergebnisse", erg);
    return obj;
}

// ── http_push_pending_games ────────────────────────────────────
esp_err_t http_push_pending_games(void)
{
    if (g_store.pendingGamesCount == 0) return ESP_OK;

    char *resp = (char *)malloc(HTTP_BUF_SIZE);
    if (!resp) return ESP_ERR_NO_MEM;

    esp_err_t overall = ESP_OK;
    int synced = 0;

    for (int i = 0; i < g_store.pendingGamesCount; i++) {
        // Skip games that contain local (negative-ID) players — the portal
        // cannot resolve them and the sync would fail or produce corrupt data.
        bool has_local = false;
        for (int j = 0; j < g_store.pendingGames[i].teilnahmen_count; j++) {
            if (g_store.pendingGames[i].teilnahmen[j].spielerId < 0) {
                has_local = true;
                break;
            }
        }
        if (has_local) {
            ESP_LOGI(TAG, "Discarding game %d: contains local player(s)", i);
            synced++; // remove from queue on next flush
            continue;
        }

        // Server expects { "spiele": [ <game> ] }
        cJSON *obj  = pending_game_to_json(&g_store.pendingGames[i]);
        cJSON *arr  = cJSON_CreateArray();
        cJSON *wrap = cJSON_CreateObject();
        cJSON_AddItemToArray(arr, obj);
        cJSON_AddItemToObject(wrap, "spiele", arr);
        char *body = cJSON_PrintUnformatted(wrap);
        cJSON_Delete(wrap);          // frees obj and arr too
        if (!body) {
            overall = ESP_ERR_NO_MEM;
            set_http_error("POST", "/api/sync/spiele", "JSON allocation failed");
            continue;
        }

        esp_err_t err = http_post_json("/api/sync/spiele", body, resp, HTTP_BUF_SIZE);
        free(body);
        if (err == ESP_OK) {
            synced++;
        } else {
            overall = err;
        }
    }

    if (synced > 0) {
        // Remove synced games (simple: remove all on full success)
        if (synced == g_store.pendingGamesCount) {
            TickType_t commit_started;
            if (!sync_commit_begin("pending-games", &commit_started)) {
                free(resp);
                return ESP_ERR_TIMEOUT;
            }
            g_store.pendingGamesCount = 0;
            sync_commit_end("pending-games", commit_started);
            game_store_save();
        }
        ESP_LOGI(TAG, "Pushed %d/%d games", synced, g_store.pendingGamesCount + synced);
    }

    free(resp);
    return overall;
}

// ── http_fetch_spieler ─────────────────────────────────────────
esp_err_t http_fetch_spieler(PortalSpieler *out, int max, int *count)
{
    *count = 0;
    char *resp = (char *)malloc(HTTP_BUF_SIZE);
    if (!resp) return ESP_ERR_NO_MEM;

    esp_err_t err = http_get_json("/api/sync/spieler", resp, HTTP_BUF_SIZE);
    if (err != ESP_OK) { free(resp); return err; }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) return ESP_ERR_INVALID_RESPONSE;

    // Expect { spieler: [...] } or an array
    cJSON *arr = cJSON_GetObjectItem(root, "spieler");
    if (!arr || !cJSON_IsArray(arr)) arr = root;

    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (*count >= max) break;
        PortalSpieler *ps = &out[(*count)++];
        memset(ps, 0, sizeof(*ps));

        cJSON *jid    = cJSON_GetObjectItem(item, "id");
        cJSON *jname  = cJSON_GetObjectItem(item, "name");
        cJSON *jnr    = cJSON_GetObjectItem(item, "mitgliedNr");
        cJSON *jaktiv = cJSON_GetObjectItem(item, "portalAktiv");
        cJSON *jemail = cJSON_GetObjectItem(item, "email");

        if (jid   && cJSON_IsNumber(jid))   ps->id = (int)jid->valuedouble;
        if (jname && cJSON_IsString(jname)) {
            utf8_to_display(ps->name, jname->valuestring, MAX_NAME_LEN);
        }
        if (jnr && cJSON_IsString(jnr)) {
            strncpy(ps->mitgliedNr, jnr->valuestring, sizeof(ps->mitgliedNr) - 1);
            ps->mitgliedNr[sizeof(ps->mitgliedNr) - 1] = '\0';
        }
        if (jaktiv && cJSON_IsBool(jaktiv)) ps->portalAktiv = cJSON_IsTrue(jaktiv);
        if (jemail && cJSON_IsString(jemail)) {
            strncpy(ps->email, jemail->valuestring, MAX_EMAIL_LEN - 1);
            ps->email[MAX_EMAIL_LEN - 1] = '\0';
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "Fetched %d portal players", *count);
    return ESP_OK;
}

// ── http_fetch_spielhistorie ───────────────────────────────────
esp_err_t http_fetch_spielhistorie(void)
{
    char *resp = (char *)malloc(HTTP_BUF_SIZE);
    if (!resp) return ESP_ERR_NO_MEM;

    esp_err_t err = http_get_json("/api/sync/spiele?limit=20", resp, HTTP_BUF_SIZE);
    if (err != ESP_OK) { free(resp); return err; }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) return ESP_ERR_INVALID_RESPONSE;

    cJSON *arr = cJSON_GetObjectItem(root, "spiele");
    if (!arr || !cJSON_IsArray(arr)) arr = root;

    FinishedGame *staged = (FinishedGame *)heap_caps_calloc(
        MAX_HISTORY, sizeof(FinishedGame), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!staged) { cJSON_Delete(root); return ESP_ERR_NO_MEM; }
    int count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (count >= MAX_HISTORY) break;
        FinishedGame *fg = &staged[count];

        cJSON *jext   = cJSON_GetObjectItem(item, "externalId");
        cJSON *jdatum = cJSON_GetObjectItem(item, "datum");
        cJSON *jmodus = cJSON_GetObjectItem(item, "modus");
        cJSON *jlauf  = cJSON_GetObjectItem(item, "lauf");
        cJSON *jtpl   = cJSON_GetObjectItem(item, "taubenProLauf");
        cJSON *jdone  = cJSON_GetObjectItem(item, "abgeschlossen");

        if (jext   && cJSON_IsString(jext))
            strncpy(fg->base.externalId, jext->valuestring, sizeof(fg->base.externalId) - 1);
        if (jdatum && cJSON_IsString(jdatum)) {
            // Full ISO timestamp for finishedAt; date-only prefix for base.datum
            strncpy(fg->finishedAt, jdatum->valuestring, sizeof(fg->finishedAt) - 1);
            strncpy(fg->base.datum, jdatum->valuestring, 10);
            fg->base.datum[10] = '\0';
        }
        if (jmodus && cJSON_IsString(jmodus)) {
            const char *ms = jmodus->valuestring;
            if      (strcmp(ms, "NORMAL")   == 0) fg->base.modus = MODUS_NORMAL;
            else if (strcmp(ms, "CUSTOM_1") == 0) fg->base.modus = MODUS_CUSTOM_1;
            else if (strcmp(ms, "CUSTOM_2") == 0) fg->base.modus = MODUS_CUSTOM_2;
            else if (strcmp(ms, "CUSTOM_3") == 0) fg->base.modus = MODUS_CUSTOM_3;
            else if (strcmp(ms, "CUSTOM_4") == 0) fg->base.modus = MODUS_CUSTOM_4;
        }
        if (jlauf && cJSON_IsNumber(jlauf))  fg->base.lauf          = (int)jlauf->valuedouble;
        if (jtpl  && cJSON_IsNumber(jtpl))   fg->base.taubenProLauf = (int)jtpl->valuedouble;
        if (jdone && cJSON_IsBool(jdone))    fg->base.abgeschlossen = cJSON_IsTrue(jdone);

        // Teilnahmen array
        cJSON *jt = cJSON_GetObjectItem(item, "teilnahmen");
        if (jt && cJSON_IsArray(jt)) {
            cJSON *t;
            cJSON_ArrayForEach(t, jt) {
                if (fg->base.teilnahmen_count >= MAX_SPIELER) break;
                int ti = fg->base.teilnahmen_count++;
                cJSON *jsid = cJSON_GetObjectItem(t, "spielerId");
                cJSON *jsp  = cJSON_GetObjectItem(t, "startPosten");
                cJSON *jpkt = cJSON_GetObjectItem(t, "punkte");
                cJSON *jla  = cJSON_GetObjectItem(t, "lauf");
                if (jsid) fg->base.teilnahmen[ti].spielerId   = (int)jsid->valuedouble;
                if (jsp)  fg->base.teilnahmen[ti].startPosten = (int)jsp->valuedouble;
                if (jpkt) fg->base.teilnahmen[ti].punkte      = (int)jpkt->valuedouble;
                if (jla)  fg->base.teilnahmen[ti].lauf        = (int)jla->valuedouble;
            }
        }

        // spielerNamen: { "123": "Max Mustermann", ... }  (keys = spielerId strings)
        cJSON *jnamen = cJSON_GetObjectItem(item, "spielerNamen");
        if (jnamen && cJSON_IsObject(jnamen)) {
            cJSON *entry = jnamen->child;
            while (entry && fg->spieler_count < MAX_SPIELER) {
                int si = fg->spieler_count++;
                fg->spielerIds[si] = atoi(entry->string);
                if (cJSON_IsString(entry))
                    utf8_to_display(fg->spielerNamen[si], entry->valuestring, MAX_NAME_LEN);
                entry = entry->next;
            }
        }

        count++;
    }

    cJSON_Delete(root);
    TickType_t commit_started;
    if (!sync_commit_begin("history", &commit_started)) {
        heap_caps_free(staged);
        return ESP_ERR_TIMEOUT;
    }
    memcpy(g_store.history, staged, MAX_HISTORY * sizeof(FinishedGame));
    g_store.historyCount = count;
    sync_commit_end("history", commit_started);
    heap_caps_free(staged);
    if (!offline_cache_save(OFFLINE_CACHE_HISTORY)) {
        ESP_LOGE(TAG, "History pull rejected: FAT cache was not durable");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Fetched %d games from portal into history", count);
    return ESP_OK;
}

// ── http_push_spieler_updates ─────────────────────────────────
esp_err_t http_push_spieler_updates(void)
{
    if (g_store.spielerUpdateCount == 0) return ESP_OK;
    esp_err_t overall = ESP_OK;

    // ── Pass 1: create terminal-local players in the portal (one per request) ─
    // SPIELER_CREATE entries hold a negative local spielerId.  The API returns
    // the real portal ID which we patch into the local player record so
    // subsequent game syncs and UPDATE entries use the correct positive ID.
    for (int i = 0; i < g_store.spielerUpdateCount; i++) {
        SpielerUpdateEntry *e = &g_store.spielerUpdates[i];
        if (!e->used || e->typ != SPIELER_CREATE) continue;

        cJSON *req_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(req_obj, "externalId", e->externalId);
        cJSON_AddStringToObject(req_obj, "name",       e->name);
        if (e->email[0]) cJSON_AddStringToObject(req_obj, "email", e->email);
        char *body = cJSON_PrintUnformatted(req_obj);
        cJSON_Delete(req_obj);
        if (!body) {
            overall = ESP_ERR_NO_MEM;
            set_http_error("POST", "/api/sync/spieler-neu", "JSON allocation failed");
            continue;
        }

        char *resp = (char *)malloc(256);
        if (resp) {
            esp_err_t cerr = http_post_json("/api/sync/spieler-neu", body, resp, 256);
            if (cerr == ESP_OK) {
                // Parse { "id": <portal_id>, "name": "...", "externalId": "..." }
                int new_id = 0;
                cJSON *j = cJSON_Parse(resp);
                if (j) {
                    cJSON *id_item = cJSON_GetObjectItem(j, "id");
                    if (cJSON_IsNumber(id_item)) new_id = (int)id_item->valuedouble;
                    cJSON_Delete(j);
                }
                if (new_id > 0) {
                    int old_id = e->spielerId;          // negative local ID
                    // One coherent remap covers lineup, credit balances and
                    // every durable outbox. Persistence follows the short
                    // publication window, never inside it.
                    TickType_t commit_started;
                    if (!sync_commit_begin("player-remap", &commit_started)) {
                        free(resp);
                        free(body);
                        return ESP_ERR_TIMEOUT;
                    }
                    store_remap_spieler_id(old_id, new_id);
                    e->used = false;    // mark done — compacted below
                    sync_commit_end("player-remap", commit_started);
                    ESP_LOGI(TAG, "Spieler '%s' created in portal → id=%d (was local %d)",
                             e->name, new_id, old_id);
                    game_store_save();
                    (void)offline_cache_save(OFFLINE_CACHE_ROSTER);
                    (void)offline_cache_save(OFFLINE_CACHE_HISTORY);
                    (void)offline_cache_save(OFFLINE_CACHE_CREDITS);
                    (void)offline_cache_save(OFFLINE_CACHE_SALES);
                    (void)offline_cache_save(OFFLINE_CACHE_BILLS);
                } else {
                    ESP_LOGW(TAG, "spieler-neu: bad response for '%s' — retaining", e->name);
                    overall = ESP_FAIL;
                    set_http_error("POST", "/api/sync/spieler-neu",
                                   "invalid response");
                }
            } else {
                ESP_LOGW(TAG, "spieler-neu: HTTP error for '%s': %s", e->name, esp_err_to_name(cerr));
                overall = cerr;
            }
            free(resp);
        } else {
            overall = ESP_ERR_NO_MEM;
            set_http_error("POST", "/api/sync/spieler-neu", "response allocation failed");
        }
        free(body);
    }

    // ── Pass 2: push profile updates and password resets for portal players ───
    int update_count = 0;
    for (int i = 0; i < g_store.spielerUpdateCount; i++) {
        SpielerUpdateEntry *e = &g_store.spielerUpdates[i];
        if (e->used && e->typ != SPIELER_CREATE) update_count++;
    }

    if (update_count > 0) {
        cJSON *root = cJSON_CreateObject();
        cJSON *arr  = cJSON_CreateArray();
        cJSON_AddItemToObject(root, "updates", arr);

        for (int i = 0; i < g_store.spielerUpdateCount; i++) {
            SpielerUpdateEntry *e = &g_store.spielerUpdates[i];
            if (!e->used || e->typ == SPIELER_CREATE) continue;
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "externalId", e->externalId);
            cJSON_AddNumberToObject(item, "spielerId",  (double)e->spielerId);
            if (e->typ == SPIELER_UPDATE_PASSWORT_RESET) {
                cJSON_AddStringToObject(item, "typ", "PASSWORT_RESET");
            } else {
                cJSON_AddStringToObject(item, "typ", "UPDATE");
                cJSON_AddStringToObject(item, "name", e->name);
                if (e->email[0]) cJSON_AddStringToObject(item, "email", e->email);
                else             cJSON_AddNullToObject  (item, "email");
                cJSON_AddBoolToObject(item, "portalAktiv", e->portalAktiv);
            }
            cJSON_AddItemToArray(arr, item);
        }

        char *body = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (body) {
            char *resp = (char *)malloc(512);
            if (resp) {
                esp_err_t uerr = http_post_json("/api/sync/spieler-updates", body, resp, 512);
                if (uerr == ESP_OK) {
                    TickType_t commit_started;
                    if (!sync_commit_begin("player-update-ack", &commit_started)) {
                        free(resp);
                        free(body);
                        return ESP_ERR_TIMEOUT;
                    }
                    for (int i = 0; i < g_store.spielerUpdateCount; i++) {
                        SpielerUpdateEntry *e = &g_store.spielerUpdates[i];
                        if (e->used && e->typ != SPIELER_CREATE) e->used = false;
                    }
                    sync_commit_end("player-update-ack", commit_started);
                    ESP_LOGI(TAG, "Pushed %d spieler update(s)", update_count);
                } else {
                    ESP_LOGW(TAG, "Spieler updates push failed (%s) — queue retained", esp_err_to_name(uerr));
                    if (overall == ESP_OK) overall = uerr;
                }
                free(resp);
            } else {
                overall = ESP_ERR_NO_MEM;
                set_http_error("POST", "/api/sync/spieler-updates",
                               "response allocation failed");
            }
            free(body);
        } else {
            overall = ESP_ERR_NO_MEM;
            set_http_error("POST", "/api/sync/spieler-updates",
                           "JSON allocation failed");
        }
    }

    // ── Compact the queue — remove entries cleared above ──────────────────────
    {
        bool needs_compaction = false;
        for (int i = 0; i < g_store.spielerUpdateCount; ++i)
            if (!g_store.spielerUpdates[i].used) { needs_compaction = true; break; }
        if (!needs_compaction) return overall;
        TickType_t commit_started;
        if (!sync_commit_begin("player-updates", &commit_started))
            return ESP_ERR_TIMEOUT;
        int nc = 0;
        for (int i = 0; i < g_store.spielerUpdateCount; i++) {
            if (g_store.spielerUpdates[i].used) {
                if (nc != i) g_store.spielerUpdates[nc] = g_store.spielerUpdates[i];
                nc++;
            }
        }
        for (int i = nc; i < g_store.spielerUpdateCount; i++)
            memset(&g_store.spielerUpdates[i], 0, sizeof(SpielerUpdateEntry));
        g_store.spielerUpdateCount = nc;
        sync_commit_end("player-updates", commit_started);
        game_store_save();
    }

    return overall;
}

// ── http_push_kredit_events ───────────────────────────────────
esp_err_t http_push_kredit_events(void)
{
    KreditEvent *snapshot = (KreditEvent *)malloc(MAX_KREDIT_EVENTS * sizeof(KreditEvent));
    if (!snapshot) return ESP_ERR_NO_MEM;
    int snapshotCount = store_begin_kredit_event_sync(snapshot, MAX_KREDIT_EVENTS);
    if (snapshotCount == 0) {
        free(snapshot);
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "events", arr);

    for (int i = 0; i < snapshotCount; i++) {
        KreditEvent *ev = &snapshot[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "externalId", ev->externalId);
        cJSON_AddNumberToObject(item, "spielerId",  (double)ev->spielerId);
        cJSON_AddStringToObject(item, "datum",      ev->datum);
        cJSON_AddStringToObject(item, "typ",        ev->typ);
        cJSON_AddNumberToObject(item, "anzahl",     (double)ev->anzahl);
        // Receipt fields are ignored by older portals but retained on the
        // wire for audit-capable endpoints and deterministic reconciliation.
        if (!strcmp(ev->typ, "USE")) {
            if (ev->occurredAt[0])
                cJSON_AddStringToObject(item, "occurredAt", ev->occurredAt);
            cJSON_AddNumberToObject(item, "priceRevisionId", ev->preisRevisionId);
            cJSON_AddNumberToObject(item, "unitPriceCents", ev->unitPriceCent);
        }
        cJSON_AddItemToArray(arr, item);
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        esp_err_t finish_err = finish_kredit_sync_publication(snapshot, snapshotCount, false);
        free(snapshot);
        return finish_err == ESP_OK ? ESP_ERR_NO_MEM : finish_err;
    }

    char *resp = (char *)malloc(512);
    esp_err_t err = ESP_ERR_NO_MEM;
    if (resp) {
        err = http_post_json("/api/sync/kredite", body, resp, 512);
        free(resp);
    }
    free(body);

    esp_err_t finish_err = finish_kredit_sync_publication(snapshot, snapshotCount, err == ESP_OK);
    free(snapshot);
    if (finish_err != ESP_OK) return finish_err;
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Pushed %d kredit event(s)", snapshotCount);
    } else {
        ESP_LOGW(TAG, "Kredit push failed (%s) — queue retained", esp_err_to_name(err));
    }
    return err;
}

// ── http_push_payment_events ───────────────────────────────────
// Contract: POST /api/sync/payments {events:[{externalId,spielerId,datum}]} and
// top-level results with externalId/status (accepted, skipped, conflict).
esp_err_t http_push_payment_events(void)
{
    PaymentEvent *snapshot = (PaymentEvent *)malloc(
        MAX_PENDING_PAYMENTS * sizeof(PaymentEvent));
    if (!snapshot) return ESP_ERR_NO_MEM;
    int count = 0;
    if (!store_begin_payment_sync(snapshot, MAX_PENDING_PAYMENTS, &count)) {
        free(snapshot);
        set_http_error("POST", "/api/sync/payments", "payment queue persistence failed");
        return ESP_FAIL;
    }
    if (!count) { free(snapshot); return ESP_OK; }

    cJSON *root = cJSON_CreateObject(), *events = cJSON_CreateArray();
    if (!root || !events) {
        if (root) cJSON_Delete(root);
        if (events) cJSON_Delete(events);
        bool restored = finish_payment_sync_publication(snapshot, count, NULL, 0,
                                                        "Net genuch Speicher");
        free(snapshot);
        if (!restored) {
            set_http_error("POST", "/api/sync/payments",
                           "payment queue restore persistence failed");
            return ESP_FAIL;
        }
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddItemToObject(root, "events", events);
    for (int i = 0; i < count; ++i) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "externalId", snapshot[i].externalId);
        cJSON_AddNumberToObject(item, "spielerId", snapshot[i].spielerId);
        cJSON_AddStringToObject(item, "datum", snapshot[i].datum);
        cJSON_AddItemToArray(events, item);
    }
    char *body = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    char *response = (char *)malloc(4096);
    esp_err_t err = (!body || !response) ? ESP_ERR_NO_MEM :
        http_post_json("/api/sync/payments", body, response, 4096);
    if (body) cJSON_free(body);
    if (err != ESP_OK) {
        if (!finish_payment_sync_publication(snapshot, count, NULL, 0, "Portal/Netz Feeler"))
            err = ESP_FAIL;
        if (response) free(response);
        free(snapshot);
        return err;
    }
    cJSON *parsed = cJSON_Parse(response);
    free(response);
    if (!parsed) {
        bool restored = finish_payment_sync_publication(snapshot, count, NULL, 0,
                                                        "Onliesbar Portal-Äntwert");
        free(snapshot);
        if (!restored) {
            set_http_error("POST", "/api/sync/payments",
                           "payment queue restore persistence failed");
            return ESP_FAIL;
        }
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *outcomes = cJSON_GetObjectItemCaseSensitive(parsed, "results");
    const char *accepted[MAX_PENDING_PAYMENTS] = {};
    int accepted_count = 0;
    cJSON *outcome;
    if (cJSON_IsArray(outcomes)) cJSON_ArrayForEach(outcome, outcomes) {
            cJSON *id = cJSON_GetObjectItemCaseSensitive(outcome, "externalId");
            cJSON *status = cJSON_GetObjectItemCaseSensitive(outcome, "status");
            if (!cJSON_IsString(id)) continue;
            // skipped is an idempotent replay accepted by the portal.
            if (cJSON_IsString(status) &&
                (!strcmp(status->valuestring, "accepted") ||
                 !strcmp(status->valuestring, "skipped")))
                if (accepted_count < MAX_PENDING_PAYMENTS) accepted[accepted_count++] = id->valuestring;
    }
    bool finish_saved = finish_payment_sync_publication(snapshot, count, accepted, accepted_count,
                                                         "Portal conflict / net acceptéiert");
    cJSON_Delete(parsed);
    free(snapshot);
    // A 2xx with a conflict/rejection is not delivery success: preserve a
    // visible sync error and retry the unchanged externalId later.
    return finish_saved && accepted_count == count ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static int json_int(cJSON *object, const char *key)
{
    if (!object) return 0;
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsNumber(value) ? (int)value->valuedouble : 0;
}

static bool bill_add_category(BillCategoryTotal *totals, int *count,
                              const char *name, int cents)
{
    if (!name || !name[0] || !cents) return true;
    for (int i = 0; i < *count; ++i)
        if (!strcmp(totals[i].name, name)) { totals[i].totalCent += cents; return true; }
    if (*count >= MAX_BILL_CATEGORIES) return false;
    BillCategoryTotal *total = &totals[(*count)++];
    snprintf(total->name, sizeof(total->name), "%s", name);
    total->totalCent = cents;
    return true;
}

static bool parse_bill_day_summary(cJSON *root, const char *requested,
                                   BillDaySummary *summary)
{
    if (!cJSON_IsObject(root) || !summary) return false;
    cJSON *datum = cJSON_GetObjectItemCaseSensitive(root, "datum");
    cJSON *players = cJSON_GetObjectItemCaseSensitive(root, "players");
    if (!cJSON_IsString(datum) || strcmp(datum->valuestring, requested) ||
        !cJSON_IsArray(players) || cJSON_GetArraySize(players) > MAX_DAY_BILLS) return false;
    memset(summary, 0, sizeof(*summary));
    snprintf(summary->datum, sizeof(summary->datum), "%s", datum->valuestring);
    summary->generalTotalCent = json_int(root, "generalTotalCents");
    summary->uniquePlayers = json_int(root, "uniquePlayers");
    summary->paidPlayers = json_int(root, "paidPlayers");
    summary->games = json_int(root, "games");
    summary->completedGames = json_int(root, "completedGames");
    summary->confirmedClays = json_int(root, "confirmedClays");
    cJSON *day_categories = cJSON_GetObjectItemCaseSensitive(root, "categorySubtotals");
    if (cJSON_IsObject(day_categories)) {
        if (cJSON_GetArraySize(day_categories) > MAX_BILL_CATEGORIES) return false;
        cJSON *category;
        cJSON_ArrayForEach(category, day_categories)
            if (cJSON_IsNumber(category))
                if (!bill_add_category(summary->categories, &summary->categoryCount,
                                       category->string, (int)category->valuedouble))
                    return false;
    }
    cJSON *day_products = cJSON_GetObjectItemCaseSensitive(root, "productTotals");
    if (cJSON_IsObject(day_products)) {
        if (cJSON_GetArraySize(day_products) > MAX_DAY_PRODUCTS) return false;
        cJSON *item;
        cJSON_ArrayForEach(item, day_products) {
            if (!cJSON_IsObject(item)) continue;
            if (summary->productCount >= MAX_DAY_PRODUCTS) {
                return false;
            }
            BillLine *out = &summary->products[summary->productCount++];
            out->produktId = json_int(item, "productId");
            out->preisRevisionId = json_int(item, "priceRevisionId");
            out->quantity = json_int(item, "quantity");
            out->unitPriceCent = json_int(item, "unitPriceCents");
            out->lineTotalCent = json_int(item, "totalCents");
            cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "productName");
            cJSON *category = cJSON_GetObjectItemCaseSensitive(item, "category");
            if (cJSON_IsString(name))
                utf8_to_display(out->produktName, name->valuestring,
                                sizeof(out->produktName));
            if (cJSON_IsString(category))
                snprintf(out->category, sizeof(out->category), "%s",
                         category->valuestring);
        }
    }
    cJSON *player;
    cJSON_ArrayForEach(player, players) {
        cJSON *sid = cJSON_GetObjectItemCaseSensitive(player, "spielerId");
        if (!cJSON_IsNumber(sid)) continue;
        if (summary->playerCount >= MAX_DAY_BILLS) return false;
        PlayerBill *bill = &summary->players[summary->playerCount++];
        memset(bill, 0, sizeof(*bill));
        bill->spielerId = (int)sid->valuedouble;
        cJSON *name = cJSON_GetObjectItemCaseSensitive(player, "spielerName");
        if (cJSON_IsString(name))
            utf8_to_display(bill->spielerName, name->valuestring, sizeof(bill->spielerName));
        bill->totalCent = json_int(player, "totalCents");
        bill->games = json_int(player, "games");
        bill->completedGames = json_int(player, "completedGames");
        bill->confirmedClays = json_int(player, "confirmedClays");
        cJSON *credit = cJSON_GetObjectItemCaseSensitive(player, "credit");
        bill->creditGranted = json_int(credit, "granted");
        bill->creditUsed = json_int(credit, "used");
        bill->creditRemaining = json_int(credit, "remaining");
        cJSON *state = cJSON_GetObjectItemCaseSensitive(player, "state");
        bill->state = cJSON_IsString(state) && !strcmp(state->valuestring, "PAID")
            ? BILL_PAID : cJSON_IsString(state) && !strcmp(state->valuestring, "PENDING_NEUTRAL")
            ? BILL_PENDING_NEUTRAL : BILL_OPEN;
        cJSON *payment = cJSON_GetObjectItemCaseSensitive(player, "payment");
        if (cJSON_IsObject(payment)) {
            json_string_into(payment, "externalId", bill->paymentExternalId,
                             sizeof(bill->paymentExternalId));
            json_string_into(payment, "paidAt", bill->paidAt, sizeof(bill->paidAt));
            json_string_into(payment, "source", bill->paymentSource,
                             sizeof(bill->paymentSource));
        }
        cJSON *lines = cJSON_GetObjectItemCaseSensitive(player, "lines"), *line;
        if (cJSON_IsArray(lines) && cJSON_GetArraySize(lines) > MAX_BILL_LINES)
            return false;
        if (cJSON_IsArray(lines)) cJSON_ArrayForEach(line, lines) {
            if (bill->lineCount >= MAX_BILL_LINES) {
                return false;
            }
            BillLine *out = &bill->lines[bill->lineCount++];
            out->produktId = json_int(line, "productId");
            out->preisRevisionId = json_int(line, "priceRevisionId");
            out->quantity = json_int(line, "quantity");
            out->unitPriceCent = json_int(line, "unitPriceCents");
            out->lineTotalCent = json_int(line, "totalCents");
            cJSON *product_name = cJSON_GetObjectItemCaseSensitive(line, "productName");
            cJSON *category = cJSON_GetObjectItemCaseSensitive(line, "category");
            if (cJSON_IsString(product_name))
                utf8_to_display(out->produktName, product_name->valuestring,
                                sizeof(out->produktName));
            if (cJSON_IsString(category))
                snprintf(out->category, sizeof(out->category), "%s", category->valuestring);
            if (!bill_add_category(bill->categories, &bill->categoryCount,
                                   out->category, out->lineTotalCent))
                return false;
        }
    }
    summary->authoritative = true;
    return true;
}

esp_err_t http_fetch_bill_day_summary(void)
{
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char datum[11], path[80];
    strftime(datum, sizeof(datum), "%Y-%m-%d", &tm);
    snprintf(path, sizeof(path), "/api/sync/bills/day-summary?datum=%s", datum);
    char *response = (char *)malloc(HTTP_BUF_SIZE);
    if (!response) return ESP_ERR_NO_MEM;
    esp_err_t err = http_get_json(path, response, HTTP_BUF_SIZE);
    if (err == ESP_OK) {
        cJSON *summary = cJSON_Parse(response);
        BillDaySummary *parsed = (BillDaySummary *)heap_caps_calloc(
            1, sizeof(BillDaySummary), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!summary || !parsed || !parse_bill_day_summary(summary, datum, parsed)) {
            ESP_LOGW(TAG, "Bill day response was not valid JSON/schema (%u bytes)",
                     (unsigned)strlen(response));
            set_http_error("GET", path, "invalid bill summary response");
            err = ESP_ERR_INVALID_RESPONSE;
        } else {
            TickType_t commit_started;
            if (!sync_commit_begin("bills", &commit_started)) {
                err = ESP_ERR_TIMEOUT;
            } else {
                store_cache_bill_day(parsed);
                sync_commit_end("bills", commit_started);
                if (!offline_cache_save(OFFLINE_CACHE_BILLS)) err = ESP_FAIL;
            }
        }
        if (parsed) heap_caps_free(parsed);
        if (summary) cJSON_Delete(summary);
    }
    free(response);
    return err;
}

// ── http_pull_kredite ─────────────────────────────────────────
// Pulls today's credit totals from the portal and merges them into
// g_store.kredite[], reconciling any grants made via the web admin.
esp_err_t http_pull_kredite(void)
{
    struct timeval tv; gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec; struct tm tmi; localtime_r(&now, &tmi);  // local TZ (CET/CEST)
    char datum[11]; strftime(datum, sizeof(datum), "%Y-%m-%d", &tmi);

    char path[64];
    snprintf(path, sizeof(path), "/api/sync/kredite?datum=%s", datum);

    char *resp = (char *)malloc(8192);
    if (!resp) return ESP_ERR_NO_MEM;

    esp_err_t err = http_get_json(path, resp, 8192);
    if (err != ESP_OK) { free(resp); return err; }

    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) return ESP_ERR_INVALID_RESPONSE;

    cJSON *arr = cJSON_GetObjectItem(root, "kredite");
    if (arr && cJSON_IsArray(arr)) {
        TickType_t commit_started;
        if (!sync_commit_begin("credits", &commit_started)) {
            cJSON_Delete(root);
            return ESP_ERR_TIMEOUT;
        }
        cJSON *item;
        cJSON_ArrayForEach(item, arr) {
            cJSON *jsid = cJSON_GetObjectItem(item, "spielerId");
            cJSON *jgew = cJSON_GetObjectItem(item, "gewaehrt");
            cJSON *jver = cJSON_GetObjectItem(item, "verbraucht");
            if (!jsid || !cJSON_IsNumber(jsid)) continue;
            int sid = (int)jsid->valuedouble;
            int gew = jgew && cJSON_IsNumber(jgew) ? (int)jgew->valuedouble : 0;
            int ver = jver && cJSON_IsNumber(jver) ? (int)jver->valuedouble : 0;
            store_apply_portal_kredit(sid, gew, ver);
        }
        sync_commit_end("credits", commit_started);
        ESP_LOGI(TAG, "Pulled credits for %s", datum);
    }
    cJSON_Delete(root);
    if (!offline_cache_save(OFFLINE_CACHE_CREDITS)) return ESP_FAIL;
    return ESP_OK;
}

// ── Product catalog and sale-event sync ────────────────────────
esp_err_t http_fetch_produkte(void)
{
    char *resp = (char *)malloc(8192);
    if (!resp) return ESP_ERR_NO_MEM;
    esp_err_t err = http_get_json("/api/sync/products", resp, 8192);
    if (err != ESP_OK) { free(resp); return err; }
    cJSON *root = cJSON_Parse(resp); free(resp);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    cJSON *arr = cJSON_GetObjectItem(root, "products");
    if (!arr || !cJSON_IsArray(arr)) { cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE; }
    Produkt products[MAX_PRODUKTE] = {};
    int count = 0; cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (count == MAX_PRODUKTE || !cJSON_IsObject(item)) break;
        cJSON *id = cJSON_GetObjectItem(item, "id");
        cJSON *code = cJSON_GetObjectItem(item, "code");
        cJSON *category = cJSON_GetObjectItem(item, "category");
        cJSON *name = cJSON_GetObjectItem(item, "name");
        cJSON *active = cJSON_GetObjectItem(item, "active");
        cJSON *current_price = cJSON_GetObjectItem(item, "currentPrice");
        cJSON *price = current_price ? cJSON_GetObjectItem(current_price, "unitPriceCents") : NULL;
        cJSON *revision = current_price ? cJSON_GetObjectItem(current_price, "id") : NULL;
        if (!cJSON_IsNumber(id) || !cJSON_IsString(category) || !cJSON_IsString(name) ||
            !cJSON_IsBool(active)) continue;
        if (current_price && !cJSON_IsNull(current_price) && (!cJSON_IsObject(current_price) ||
            !cJSON_IsNumber(price) || !cJSON_IsNumber(revision))) continue;
        Produkt *p = &products[count++];
        p->id = (int)id->valuedouble;
        if (cJSON_IsString(code)) strncpy(p->code, code->valuestring, sizeof(p->code) - 1);
        strncpy(p->category, category->valuestring, sizeof(p->category) - 1);
        strncpy(p->name, name->valuestring, sizeof(p->name) - 1);
        p->active = cJSON_IsTrue(active);
        if (current_price && !cJSON_IsNull(current_price)) {
            p->preisCent = (int)price->valuedouble;
            p->preisRevisionId = (int)revision->valuedouble;
        }
    }
    // An empty array is an authoritative catalog replacement, not "no update".
    // Persist it before success so its manifest token cannot hide stale items.
    TickType_t commit_started;
    if (!sync_commit_begin("products", &commit_started)) {
        cJSON_Delete(root);
        return ESP_ERR_TIMEOUT;
    }
    store_replace_produkte(products, count);
    sync_commit_end("products", commit_started);
    game_store_save();
    if (!offline_cache_save(OFFLINE_CACHE_PRODUCTS)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t http_push_verkauf_events(void)
{
    VerkaufEvent *snapshot = (VerkaufEvent *)heap_caps_malloc(
        MAX_PENDING_VERKAEUFE * sizeof(VerkaufEvent), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!snapshot) return ESP_ERR_NO_MEM;
    int count = store_begin_verkauf_sync(snapshot, MAX_PENDING_VERKAEUFE);
    if (!count) { heap_caps_free(snapshot); return ESP_OK; }
    cJSON *root = cJSON_CreateObject(), *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "events", arr);
    for (int i = 0; i < count; ++i) {
        VerkaufEvent *ev = &snapshot[i]; cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "externalId", ev->externalId);
        cJSON_AddNumberToObject(item, "spielerId", ev->spielerId);
        cJSON_AddStringToObject(item, "datum", ev->datum);
        cJSON_AddNumberToObject(item, "productId", ev->produktId);
        // Negative quantities are unallocated reversals. Always put the
        // literal integer sentinel on the wire, including for an event
        // written by an older firmware revision with a stale cached price.
        cJSON_AddNumberToObject(item, "priceRevisionId",
                                ev->quantity < 0 ? VERKAUF_PRICE_REVISION_UNALLOCATED
                                                 : ev->preisRevisionId);
        cJSON_AddNumberToObject(item, "quantity", ev->quantity);
        cJSON_AddItemToArray(arr, item);
    }
    char *body = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    char *resp = (char *)malloc(512);
    esp_err_t err = (!body || !resp) ? ESP_ERR_NO_MEM :
        http_post_json("/api/sync/sales", body, resp, 512);
    if (body) free(body);
    if (resp) free(resp);
    // /sales accepts a batch atomically. Never partially acknowledge a
    // snapshot: a non-2xx leaves every externalId in the durable outbox for
    // an idempotent retry, while a 2xx removes the complete snapshot.
    esp_err_t finish_err = finish_verkauf_sync_publication(snapshot, count, err == ESP_OK);
    heap_caps_free(snapshot);
    if (finish_err != ESP_OK) return finish_err;
    return err;
}

esp_err_t http_pull_verkaeufe(void)
{
    time_t now = time(NULL); struct tm tmi; localtime_r(&now, &tmi);
    char date[11], path[64]; strftime(date, sizeof(date), "%Y-%m-%d", &tmi);
    snprintf(path, sizeof(path), "/api/sync/sales?datum=%s", date);
    char *resp = (char *)malloc(8192);
    if (!resp) return ESP_ERR_NO_MEM;
    esp_err_t err = http_get_json(path, resp, 8192);
    if (err != ESP_OK) { free(resp); return err; }
    cJSON *root = cJSON_Parse(resp); free(resp);
    if (!root) return ESP_ERR_INVALID_RESPONSE;
    cJSON *arr = cJSON_GetObjectItem(root, "sales");
    if (!arr || !cJSON_IsArray(arr)) { cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE; }
    TickType_t commit_started;
    if (!sync_commit_begin("sales", &commit_started)) {
        cJSON_Delete(root);
        return ESP_ERR_TIMEOUT;
    }
    memset(g_store.munition, 0, sizeof(g_store.munition));
    snprintf(g_store.verkaufDatum, sizeof(g_store.verkaufDatum), "%s", date);
    g_store.verkaufCal12Total = g_store.verkaufCal20Total = 0;
    cJSON *item; cJSON_ArrayForEach(item, arr) {
        cJSON *spieler = cJSON_GetObjectItem(item, "spielerId");
        cJSON *product = cJSON_GetObjectItem(item, "productId");
        cJSON *qty = cJSON_GetObjectItem(item, "quantity");
        // Canonical rows are player-attributed. Do not use a synthetic player
        // zero for aggregate data: it would make a later reboot lose the row.
        if (cJSON_IsNumber(spieler) && spieler->valuedouble > 0 &&
            cJSON_IsNumber(product) && cJSON_IsNumber(qty)) {
            store_apply_portal_verkauf((int)spieler->valuedouble,
                                       (int)product->valuedouble,
                                       (int)qty->valuedouble);
        }
    }
    // Reconciliation order is: clear cache → rebuild portal rows → replay
    // only today's unsent/in-flight local events. Events from another date
    // remain in the outbox for delivery but must not inflate this day's view.
    for (int i = 0; i < g_store.pendingVerkaufEventCount; ++i) {
        VerkaufEvent *ev = &g_store.pendingVerkaufEvents[i];
        if (strcmp(ev->datum, date) == 0)
            store_apply_portal_verkauf(ev->spielerId, ev->produktId, ev->quantity);
    }
    sync_commit_end("sales", commit_started);
    cJSON_Delete(root); game_store_save();
    if (!offline_cache_save(OFFLINE_CACHE_SALES)) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t http_sync_billing(void)
{
    portENTER_CRITICAL(&s_error_lock);
    s_last_error[0] = '\0';
    portEXIT_CRITICAL(&s_error_lock);
    if (!cop_wifi_is_connected()) {
        set_http_error("SYNC", "billing", "WiFi not connected");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting billing sync...");
    esp_err_t overall = ESP_OK;
    esp_err_t err = http_push_kredit_events();
    if (err != ESP_OK) overall = err;
    err = http_push_verkauf_events();
    if (err != ESP_OK && overall == ESP_OK) overall = err;
    err = http_push_payment_events();
    if (err != ESP_OK && overall == ESP_OK) overall = err;
    err = http_pull_kredite();
    if (err != ESP_OK && overall == ESP_OK) overall = err;
    err = http_pull_verkaeufe();
    if (err != ESP_OK && overall == ESP_OK) overall = err;
    err = http_fetch_bill_day_summary();
    if (err != ESP_OK && overall == ESP_OK) overall = err;

    if (overall == ESP_OK) {
        TickType_t commit_started;
        if (!sync_commit_begin("billing-sync-metadata", &commit_started))
            return ESP_ERR_TIMEOUT;
        g_store.lastSuccessfulSyncAt = time(NULL);
        g_store.offlineCacheHealthy = true;
        sync_commit_end("billing-sync-metadata", commit_started);
        (void)offline_cache_save_metadata();
    }
    ESP_LOGI(TAG, "Billing sync complete (%s)",
             overall == ESP_OK ? "success" : "partial failure");
    return overall;
}

// ── http_sync_all ─────────────────────────────────────────────
esp_err_t http_sync_all(void)
{
    portENTER_CRITICAL(&s_error_lock);
    s_last_error[0] = '\0';
    portEXIT_CRITICAL(&s_error_lock);
    if (!cop_wifi_is_connected()) {
        ESP_LOGW(TAG, "Sync skipped — WiFi not connected");
        set_http_error("SYNC", "all", "WiFi not connected");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Starting full sync...");
    // The manifest is optional for compatibility with older portals. It is
    // fetched before any data endpoint, but outbox pushes below force their
    // affected baseline pulls so locally accepted events are reconciled.
    SyncManifest manifest;
    if (!fetch_manifest(&manifest)) {
        // Manifest is strictly an optimisation; its absence must not turn a
        // successful compatibility full-sync into a stale UI error.
        portENTER_CRITICAL(&s_error_lock);
        s_last_error[0] = '\0';
        portEXIT_CRITICAL(&s_error_lock);
    }
    ESP_LOGI(TAG, "  URL: %s (authenticated)", g_store.apiUrl);
    ESP_LOGI(TAG, "  internal free=%u B  largest block=%u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    // Push queued player edits (non-critical)
    esp_err_t overall = ESP_OK;
    const bool had_player_updates = g_store.spielerUpdateCount > 0;
    const bool had_sales = g_store.pendingVerkaufEventCount > 0;
    const bool had_credits = g_store.pendingKreditEventCount > 0;
    const bool had_payments = g_store.pendingPaymentEventCount > 0;
    const bool had_games = g_store.pendingGamesCount > 0;
    esp_err_t pue = http_push_spieler_updates();
    if (pue != ESP_OK) overall = pue;
    if (pue != ESP_OK) ESP_LOGW(TAG, "Spieler updates push failed — continuing sync");
    bool pull_products = manifest_changed(&manifest, OFFLINE_CACHE_PRODUCTS);
    esp_err_t ppr = pull_products ? http_fetch_produkte() : ESP_OK;
    if (pull_products && ppr == ESP_OK) commit_manifest_token(&manifest, OFFLINE_CACHE_PRODUCTS);
    if (ppr != ESP_OK && overall == ESP_OK) overall = ppr;
    if (ppr != ESP_OK) ESP_LOGW(TAG, "Product pull failed — using cached catalog");
    esp_err_t pve = http_push_verkauf_events();
    if (pve != ESP_OK && overall == ESP_OK) overall = pve;
    if (pve != ESP_OK) ESP_LOGW(TAG, "Sale push failed — queue retained");
    bool pull_sales = manifest_changed(&manifest, OFFLINE_CACHE_SALES) || (had_sales && pve == ESP_OK);
    esp_err_t plv = pull_sales ? http_pull_verkaeufe() : ESP_OK;
    if (pull_sales && plv == ESP_OK) commit_manifest_token(&manifest, OFFLINE_CACHE_SALES);
    if (plv != ESP_OK && overall == ESP_OK) overall = plv;
    if (plv != ESP_OK) ESP_LOGW(TAG, "Sale pull failed — using local totals");

    // Push queued credit events (non-critical)
    esp_err_t pke = http_push_kredit_events();
    if (pke != ESP_OK && overall == ESP_OK) overall = pke;
    if (pke != ESP_OK) ESP_LOGW(TAG, "Kredit events push failed — continuing sync");
    esp_err_t ppe = http_push_payment_events();
    if (ppe != ESP_OK && overall == ESP_OK) overall = ppe;
    if (ppe != ESP_OK) ESP_LOGW(TAG, "Payment events not fully accepted — retained");
    bool pull_bills = manifest_changed(&manifest, OFFLINE_CACHE_BILLS) ||
                      (had_sales && pve == ESP_OK) || (had_credits && pke == ESP_OK) ||
                      (had_payments && ppe == ESP_OK);
    esp_err_t pbs = pull_bills ? http_fetch_bill_day_summary() : ESP_OK;
    if (pull_bills && pbs == ESP_OK) commit_manifest_token(&manifest, OFFLINE_CACHE_BILLS);
    if (pbs != ESP_OK && overall == ESP_OK) overall = pbs;
    if (pbs != ESP_OK)
        ESP_LOGW(TAG, "Bill day summary pull failed: %s", esp_err_to_name(pbs));

    esp_err_t err = http_push_pending_games();
    if (err != ESP_OK && overall == ESP_OK) overall = err;

    bool pull_history = manifest_changed(&manifest, OFFLINE_CACHE_HISTORY) || (had_games && err == ESP_OK);
    err = pull_history ? http_fetch_spielhistorie() : ESP_OK;
    if (pull_history && err == ESP_OK) commit_manifest_token(&manifest, OFFLINE_CACHE_HISTORY);
    if (err != ESP_OK && overall == ESP_OK) overall = err;

    // Pull today's credit totals (non-critical — portal grants appear on terminal)
    bool pull_credits = manifest_changed(&manifest, OFFLINE_CACHE_CREDITS) ||
                        (had_credits && pke == ESP_OK) || (had_payments && ppe == ESP_OK);
    esp_err_t pck = pull_credits ? http_pull_kredite() : ESP_OK;
    if (pull_credits && pck == ESP_OK) commit_manifest_token(&manifest, OFFLINE_CACHE_CREDITS);
    if (pck != ESP_OK && overall == ESP_OK) overall = pck;
    if (pck != ESP_OK) ESP_LOGW(TAG, "Credit pull failed — continuing");

    // Refresh player list — heap-allocated: 200×~104 B = ~20 KB would overflow
    // the 12 KB sync_task stack if declared as a local array.
    int count = 0;
    PortalSpieler *buf = (PortalSpieler *)malloc(MAX_PORTAL_SPIELER * sizeof(PortalSpieler));
    if (!buf) {
        ESP_LOGE(TAG, "OOM for portal spieler buf");
        set_http_error("SYNC", "spieler", "out of memory");
        return ESP_ERR_NO_MEM;
    }
    bool pull_roster = manifest_changed(&manifest, OFFLINE_CACHE_ROSTER) ||
                       (had_player_updates && pue == ESP_OK);
    err = pull_roster ? http_fetch_spieler(buf, MAX_PORTAL_SPIELER, &count) : ESP_OK;
    if (err == ESP_OK) {
        if (pull_roster) {
            TickType_t commit_started;
            if (!sync_commit_begin("roster", &commit_started)) {
                free(buf);
                return ESP_ERR_TIMEOUT;
            }
            store_apply_portal_roster(buf, count);
            sync_commit_end("roster", commit_started);
            game_store_save();
            if (offline_cache_save(OFFLINE_CACHE_ROSTER))
                commit_manifest_token(&manifest, OFFLINE_CACHE_ROSTER);
            else {
                ESP_LOGE(TAG, "Roster pull rejected: FAT cache was not durable");
                err = ESP_FAIL;
            }
        }
    }
    free(buf);
    if (err != ESP_OK && overall == ESP_OK) overall = err;

    // A configuration backup is deliberately last: operational outboxes are
    // processed first, and backup failure must never undo successful sync work.
    esp_err_t backup_err = http_backup_config_impl(true);
    if (backup_err != ESP_OK)
        ESP_LOGW(TAG, "Configuration backup failed — operational sync remains valid");

    if (overall == ESP_OK) {
        TickType_t commit_started;
        if (!sync_commit_begin("sync-metadata", &commit_started))
            return ESP_ERR_TIMEOUT;
        g_store.lastSuccessfulSyncAt = time(NULL);
        g_store.offlineCacheHealthy = true;
        sync_commit_end("sync-metadata", commit_started);
        (void)offline_cache_save_metadata();
    }
    ESP_LOGI(TAG, "Sync complete (%s)", overall == ESP_OK ? "success" : "partial failure");
    return overall;
}
