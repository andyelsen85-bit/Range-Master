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
#include "cJSON.h"

#include "http_sync.h"
#include "game_store.h"
#include "app_config.h"

static const char *TAG = "http_sync";

#define HTTP_BUF_SIZE  (32 * 1024)

// ── HTTP response accumulator ─────────────────────────────────
typedef struct {
    char  *buf;
    int    len;
    int    cap;
} http_acc_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_acc_t *acc = (http_acc_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && acc) {
        if (acc->len + evt->data_len < acc->cap - 1) {
            memcpy(acc->buf + acc->len, evt->data, evt->data_len);
            acc->len += evt->data_len;
            acc->buf[acc->len] = '\0';
        }
    }
    return ESP_OK;
}

// ── Generic POST helper (with retry) ─────────────────────────
static esp_err_t http_post_json(const char *path, const char *body,
                                char *resp_buf, size_t resp_cap)
{
    char url[256];
    snprintf(url, sizeof(url), "%s%s", g_store.apiUrl, path);

    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (resp_buf) resp_buf[0] = '\0';
        http_acc_t acc = { .buf = resp_buf, .len = 0, .cap = (int)resp_cap };

        esp_http_client_config_t cfg = {};
        cfg.url               = url;
        cfg.event_handler     = http_event_handler;
        cfg.user_data         = resp_buf ? &acc : NULL;
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.timeout_ms        = 12000;
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_header(client, "x-api-key", g_store.apiKey);
        esp_http_client_set_post_field(client, body, strlen(body));

        err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(client);
            if (status < 200 || status >= 300) {
                ESP_LOGW(TAG, "POST %s → HTTP %d", path, status);
                err = ESP_FAIL;
            }
        } else {
            ESP_LOGW(TAG, "POST %s attempt %d/3 failed: %s", path, attempt, esp_err_to_name(err));
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
    char url[256];
    snprintf(url, sizeof(url), "%s%s", g_store.apiUrl, path);

    esp_err_t err = ESP_FAIL;
    for (int attempt = 1; attempt <= 3; attempt++) {
        resp_buf[0] = '\0';
        http_acc_t acc = { .buf = resp_buf, .len = 0, .cap = (int)resp_cap };

        esp_http_client_config_t cfg = {};
        cfg.url               = url;
        cfg.event_handler     = http_event_handler;
        cfg.user_data         = &acc;
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.timeout_ms        = 12000;
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        esp_http_client_set_header(client, "x-api-key", g_store.apiKey);

        err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(client);
            if (status < 200 || status >= 300) {
                ESP_LOGW(TAG, "GET %s → HTTP %d", path, status);
                err = ESP_FAIL;
            }
        } else {
            ESP_LOGW(TAG, "GET %s attempt %d/3 failed: %s", path, attempt, esp_err_to_name(err));
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        if (err == ESP_OK) break;
        if (attempt < 3) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return err;
}

// ── Build JSON for a PendingGame ──────────────────────────────
static cJSON *pending_game_to_json(const PendingGame *g)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "externalId",     g->externalId);
    cJSON_AddStringToObject(obj, "datum",          g->datum);
    cJSON_AddStringToObject(obj, "modus",          modus_label(g->modus));
    cJSON_AddNumberToObject(obj, "lauf",           g->lauf);
    cJSON_AddNumberToObject(obj, "taubenProLauf",  g->taubenProLauf);
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
        if (!body) continue;

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
            g_store.pendingGamesCount = 0;
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

        cJSON *jid   = cJSON_GetObjectItem(item, "id");
        cJSON *jname = cJSON_GetObjectItem(item, "name");
        cJSON *jnr   = cJSON_GetObjectItem(item, "mitgliedNr");
        cJSON *jaktiv= cJSON_GetObjectItem(item, "portalAktiv");

        if (jid   && cJSON_IsNumber(jid))   ps->id = (int)jid->valuedouble;
        if (jname && cJSON_IsString(jname)) {
            strncpy(ps->name, jname->valuestring, MAX_NAME_LEN - 1);
            ps->name[MAX_NAME_LEN - 1] = '\0';
        }
        if (jnr && cJSON_IsString(jnr)) {
            strncpy(ps->mitgliedNr, jnr->valuestring, sizeof(ps->mitgliedNr) - 1);
            ps->mitgliedNr[sizeof(ps->mitgliedNr) - 1] = '\0';
        }
        if (jaktiv && cJSON_IsBool(jaktiv)) ps->portalAktiv = cJSON_IsTrue(jaktiv);
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

    char path[80];
    snprintf(path, sizeof(path), "/api/sync/spiele?limit=50");
    esp_err_t err = http_get_json(path, resp, HTTP_BUF_SIZE);
    if (err != ESP_OK) { free(resp); return err; }

    // Parse and update history (simplified: log count)
    cJSON *root = cJSON_Parse(resp);
    free(resp);
    if (!root) return ESP_ERR_INVALID_RESPONSE;

    cJSON *arr = cJSON_GetObjectItem(root, "spiele");
    if (!arr) arr = root;
    int n = cJSON_GetArraySize(arr);
    ESP_LOGI(TAG, "Fetched %d games from portal", n);
    cJSON_Delete(root);
    return ESP_OK;
}

// ── http_sync_all ─────────────────────────────────────────────
esp_err_t http_sync_all(void)
{
    ESP_LOGI(TAG, "Starting full sync...");
    ESP_LOGI(TAG, "  URL: %s  key: '%s'", g_store.apiUrl, g_store.apiKey);
    ESP_LOGI(TAG, "  internal free=%u B  largest block=%u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    esp_err_t err = http_push_pending_games();
    if (err != ESP_OK) return err;

    err = http_fetch_spielhistorie();
    if (err != ESP_OK) return err;

    // Refresh player list — heap-allocated: 200×~104 B = ~20 KB would overflow
    // the 12 KB sync_task stack if declared as a local array.
    int count = 0;
    PortalSpieler *buf = (PortalSpieler *)malloc(MAX_PORTAL_SPIELER * sizeof(PortalSpieler));
    if (!buf) { ESP_LOGE(TAG, "OOM for portal spieler buf"); return ESP_ERR_NO_MEM; }
    err = http_fetch_spieler(buf, MAX_PORTAL_SPIELER, &count);
    if (err == ESP_OK) {
        memcpy(g_store.portalSpieler, buf, count * sizeof(PortalSpieler));
        g_store.portalSpielerCount = count;
    }
    free(buf);

    ESP_LOGI(TAG, "Sync complete");
    return ESP_OK;
}
