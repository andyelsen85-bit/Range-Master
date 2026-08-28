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
#include "coprocessor.h"

static const char *TAG = "http_sync";

#define HTTP_BUF_SIZE  (32 * 1024)
static portMUX_TYPE s_error_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_last_error[160];

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
    if (!path || !body || (resp_buf && resp_cap < 2)) {
        set_http_error("POST", path ? path : "(null)", "invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }
    char url[256];
    int url_len = snprintf(url, sizeof(url), "%s%s", g_store.apiUrl, path);
    if (url_len < 0 || url_len >= (int)sizeof(url)) {
        set_http_error("POST", path, "URL too long");
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
            set_http_error("POST", path, "client init failed");
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
                ESP_LOGW(TAG, "POST %s → HTTP %d", path, status);
                char reason[32];
                snprintf(reason, sizeof(reason), "HTTP %d", status);
                set_http_error("POST", path, reason);
                err = ESP_FAIL;
            }
        } else {
            ESP_LOGW(TAG, "POST %s attempt %d/3 failed: %s", path, attempt, esp_err_to_name(err));
            set_http_error("POST", path, esp_err_to_name(err));
        }
        if (acc.truncated) {
            set_http_error("POST", path, "response truncated");
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
    if (!path || !resp_buf || resp_cap < 2) {
        set_http_error("GET", path ? path : "(null)", "invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }
    char url[256];
    int url_len = snprintf(url, sizeof(url), "%s%s", g_store.apiUrl, path);
    if (url_len < 0 || url_len >= (int)sizeof(url)) {
        set_http_error("GET", path, "URL too long");
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
            set_http_error("GET", path, "client init failed");
            return ESP_ERR_NO_MEM;
        }
        esp_http_client_set_header(client, "x-api-key", g_store.apiKey);

        err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(client);
            if (status < 200 || status >= 300) {
                ESP_LOGW(TAG, "GET %s → HTTP %d", path, status);
                char reason[32];
                snprintf(reason, sizeof(reason), "HTTP %d", status);
                set_http_error("GET", path, reason);
                err = ESP_FAIL;
            }
        } else {
            ESP_LOGW(TAG, "GET %s attempt %d/3 failed: %s", path, attempt, esp_err_to_name(err));
            set_http_error("GET", path, esp_err_to_name(err));
        }
        if (acc.truncated) {
            set_http_error("GET", path, "response truncated");
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

    int count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (count >= MAX_HISTORY) break;
        FinishedGame *fg = &g_store.history[count];
        memset(fg, 0, sizeof(*fg));

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

    g_store.historyCount = count;
    cJSON_Delete(root);
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
                    for (int k = 0; k < g_store.portalSpielerCount; k++) {
                        if (g_store.portalSpieler[k].id == old_id) {
                            g_store.portalSpieler[k].id    = new_id;
                            g_store.portalSpieler[k].lokal = false;
                            break;
                        }
                    }
                    // Sales recorded before a local player was created must
                    // use the portal id on their first POST.
                    store_remap_verkauf_spieler(old_id, new_id);
                    game_store_save(); // remapped durable outbox must survive power loss
                    ESP_LOGI(TAG, "Spieler '%s' created in portal → id=%d (was local %d)",
                             e->name, new_id, old_id);
                    e->used = false;    // mark done — compacted below
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
                    for (int i = 0; i < g_store.spielerUpdateCount; i++) {
                        SpielerUpdateEntry *e = &g_store.spielerUpdates[i];
                        if (e->used && e->typ != SPIELER_CREATE) e->used = false;
                    }
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
        cJSON_AddItemToArray(arr, item);
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        store_finish_kredit_event_sync(snapshot, snapshotCount, false);
        free(snapshot);
        return ESP_ERR_NO_MEM;
    }

    char *resp = (char *)malloc(512);
    esp_err_t err = ESP_ERR_NO_MEM;
    if (resp) {
        err = http_post_json("/api/sync/kredite", body, resp, 512);
        free(resp);
    }
    free(body);

    store_finish_kredit_event_sync(snapshot, snapshotCount, err == ESP_OK);
    free(snapshot);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Pushed %d kredit event(s)", snapshotCount);
    } else {
        ESP_LOGW(TAG, "Kredit push failed (%s) — queue retained", esp_err_to_name(err));
    }
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
        ESP_LOGI(TAG, "Pulled credits for %s", datum);
    }
    cJSON_Delete(root);
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
    if (count) { store_replace_produkte(products, count); game_store_save(); }
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
        cJSON_AddNumberToObject(item, "priceRevisionId", ev->preisRevisionId);
        cJSON_AddNumberToObject(item, "quantity", ev->quantity);
        cJSON_AddItemToArray(arr, item);
    }
    char *body = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    char *resp = (char *)malloc(512);
    esp_err_t err = (!body || !resp) ? ESP_ERR_NO_MEM :
        http_post_json("/api/sync/sales", body, resp, 512);
    if (body) free(body); if (resp) free(resp);
    // /sales accepts a batch atomically. Never partially acknowledge a
    // snapshot: a non-2xx leaves every externalId in the durable outbox for
    // an idempotent retry, while a 2xx removes the complete snapshot.
    store_finish_verkauf_sync(snapshot, count, err == ESP_OK);
    heap_caps_free(snapshot);
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
    memset(g_store.munition, 0, sizeof(g_store.munition));
    strncpy(g_store.verkaufDatum, date, sizeof(g_store.verkaufDatum) - 1);
    g_store.verkaufCal12Total = g_store.verkaufCal20Total = 0;
    cJSON *arr = cJSON_GetObjectItem(root, "sales");
    if (!arr || !cJSON_IsArray(arr)) { cJSON_Delete(root); return ESP_ERR_INVALID_RESPONSE; }
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
    cJSON_Delete(root); game_store_save();
    return ESP_OK;
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
    ESP_LOGI(TAG, "  URL: %s (authenticated)", g_store.apiUrl);
    ESP_LOGI(TAG, "  internal free=%u B  largest block=%u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    // Push queued player edits (non-critical)
    esp_err_t overall = ESP_OK;
    esp_err_t pue = http_push_spieler_updates();
    if (pue != ESP_OK) overall = pue;
    if (pue != ESP_OK) ESP_LOGW(TAG, "Spieler updates push failed — continuing sync");
    esp_err_t ppr = http_fetch_produkte();
    if (ppr != ESP_OK && overall == ESP_OK) overall = ppr;
    if (ppr != ESP_OK) ESP_LOGW(TAG, "Product pull failed — using cached catalog");
    esp_err_t pve = http_push_verkauf_events();
    if (pve != ESP_OK && overall == ESP_OK) overall = pve;
    if (pve != ESP_OK) ESP_LOGW(TAG, "Sale push failed — queue retained");
    esp_err_t plv = http_pull_verkaeufe();
    if (plv != ESP_OK && overall == ESP_OK) overall = plv;
    if (plv != ESP_OK) ESP_LOGW(TAG, "Sale pull failed — using local totals");

    // Push queued credit events (non-critical)
    esp_err_t pke = http_push_kredit_events();
    if (pke != ESP_OK && overall == ESP_OK) overall = pke;
    if (pke != ESP_OK) ESP_LOGW(TAG, "Kredit events push failed — continuing sync");

    esp_err_t err = http_push_pending_games();
    if (err != ESP_OK && overall == ESP_OK) overall = err;

    err = http_fetch_spielhistorie();
    if (err != ESP_OK && overall == ESP_OK) overall = err;

    // Pull today's credit totals (non-critical — portal grants appear on terminal)
    esp_err_t pck = http_pull_kredite();
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
    err = http_fetch_spieler(buf, MAX_PORTAL_SPIELER, &count);
    if (err == ESP_OK) {
        memcpy(g_store.portalSpieler, buf, count * sizeof(PortalSpieler));
        g_store.portalSpielerCount = count;
    }
    free(buf);
    if (err != ESP_OK && overall == ESP_OK) overall = err;

    ESP_LOGI(TAG, "Sync complete (%s)", overall == ESP_OK ? "success" : "partial failure");
    return overall;
}
