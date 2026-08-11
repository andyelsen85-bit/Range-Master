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

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "updates", arr);

    for (int i = 0; i < g_store.spielerUpdateCount; i++) {
        SpielerUpdateEntry *e = &g_store.spielerUpdates[i];
        if (!e->used) continue;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "externalId", e->externalId);
        cJSON_AddNumberToObject(item, "spielerId", (double)e->spielerId);

        if (e->typ == SPIELER_UPDATE_PASSWORT_RESET) {
            cJSON_AddStringToObject(item, "typ", "PASSWORT_RESET");
        } else {
            cJSON_AddStringToObject(item, "typ", "UPDATE");
            cJSON_AddStringToObject(item, "name", e->name);
            if (e->email[0]) {
                cJSON_AddStringToObject(item, "email", e->email);
            } else {
                cJSON_AddNullToObject(item, "email");
            }
            cJSON_AddBoolToObject(item, "portalAktiv", e->portalAktiv);
        }
        cJSON_AddItemToArray(arr, item);
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return ESP_ERR_NO_MEM;

    char *resp = (char *)malloc(512);
    esp_err_t err = ESP_ERR_NO_MEM;
    if (resp) {
        err = http_post_json("/api/sync/spieler-updates", body, resp, 512);
        free(resp);
    }
    free(body);

    if (err == ESP_OK) {
        int pushed = g_store.spielerUpdateCount;
        memset(g_store.spielerUpdates, 0, sizeof(g_store.spielerUpdates));
        g_store.spielerUpdateCount = 0;
        ESP_LOGI(TAG, "Pushed %d spieler update(s) — queue cleared", pushed);
    } else {
        ESP_LOGW(TAG, "Spieler updates push failed (%s) — queue retained", esp_err_to_name(err));
    }
    return err;
}

// ── http_sync_all ─────────────────────────────────────────────
esp_err_t http_sync_all(void)
{
    if (!cop_wifi_is_connected()) {
        ESP_LOGW(TAG, "Sync skipped — WiFi not connected");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Starting full sync...");
    ESP_LOGI(TAG, "  URL: %s  key: '%s'", g_store.apiUrl, g_store.apiKey);
    ESP_LOGI(TAG, "  internal free=%u B  largest block=%u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    // Push queued player edits first (non-critical — don't abort sync on failure)
    esp_err_t pue = http_push_spieler_updates();
    if (pue != ESP_OK) {
        ESP_LOGW(TAG, "Spieler updates push failed — continuing sync");
    }

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
