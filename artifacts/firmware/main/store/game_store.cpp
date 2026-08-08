// ============================================================
// TrapMaster game store — C port of emulator gameStore.ts
// ============================================================
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"

#include "game_store.h"
#include "http_sync.h"
#include "app_config.h"

static const char *TAG = "game_store";

// Forward declaration (defined later in this file)
static void _store_finish_game(void);

EXT_RAM_BSS_ATTR GameStore g_store;

// ── NVS helpers ──────────────────────────────────────────────
static nvs_handle_t s_nvs;

static void nvs_open(void)
{
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs));
}

static void nvs_save_str(const char *key, const char *val) __attribute__((unused));
static void nvs_save_str(const char *key, const char *val)
{
    nvs_set_str(s_nvs, key, val);
    nvs_commit(s_nvs);
}

static bool nvs_load_str(const char *key, char *buf, size_t len)
{
    size_t sz = len;
    return nvs_get_str(s_nvs, key, buf, &sz) == ESP_OK;
}

// ── Helpers ──────────────────────────────────────────────────
const char *maschine_label(Maschine m)
{
    static const char *labels[] = {"A","B","C","D","E","F","G","H"};
    if (m < MASCHINE_COUNT) return labels[m];
    return "?";
}

const char *modus_label(Modus m)
{
    static const char *labels[] = {
        "Normal","Harakiri","Custom 1","Custom 2","Custom 3","Custom 4"
    };
    if (m < MODUS_COUNT) return labels[m];
    return "?";
}

// ── Sequence generation (mirrors generateSequenz in TS) ──────
static void shuffle(Maschine *arr, int n)
{
    // Fisher-Yates with rand()
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Maschine tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
    }
}

static int generate_sequenz(SequenzEintrag *out, Modus modus,
                             bool *aktiv, const Maschine *custom, int clen)
{
    int idx = 0;

    if (modus == MODUS_NORMAL) {
        // A-G (filtered by aktiv) + H (Doublette = 2 entries)
        for (Maschine m = MASCHINE_A; m <= MASCHINE_G; m = (Maschine)((int)m + 1)) {
            if (aktiv[m]) {
                out[idx++] = (SequenzEintrag){m, false};
            }
        }
        if (aktiv[MASCHINE_H]) {
            out[idx++] = (SequenzEintrag){MASCHINE_H, false};
            out[idx++] = (SequenzEintrag){MASCHINE_H, true};
        }
    } else if (modus == MODUS_HARAKIRI) {
        // Shuffle A-G then add H
        Maschine pool[7]; int pcnt = 0;
        for (Maschine m = MASCHINE_A; m <= MASCHINE_G; m = (Maschine)((int)m + 1)) {
            if (aktiv[m]) pool[pcnt++] = m;
        }
        shuffle(pool, pcnt);
        for (int i = 0; i < pcnt; i++) {
            out[idx++] = (SequenzEintrag){pool[i], false};
        }
        if (aktiv[MASCHINE_H]) {
            out[idx++] = (SequenzEintrag){MASCHINE_H, false};
            out[idx++] = (SequenzEintrag){MASCHINE_H, true};
        }
    } else {
        // Custom: use provided sequence, H expands to 2 entries
        for (int i = 0; i < clen; i++) {
            Maschine m = custom[i];
            if (m == MASCHINE_H) {
                out[idx++] = (SequenzEintrag){MASCHINE_H, false};
                out[idx++] = (SequenzEintrag){MASCHINE_H, true};
            } else {
                out[idx++] = (SequenzEintrag){m, false};
            }
        }
    }
    return idx;
}

// ── getCurrentPosten (mirrors TS) ────────────────────────────
// Returns 1-based post for player at spielerIndex, taubeIndex in sequenz
static int get_current_posten(int spielerIndex, int taubeIndex, int spielerCount)
{
    // Round-robin rotation: (startPosten-1 + taubeIndex) mod 5 → post 1..5
    int base = g_store.spieler[spielerIndex].startPosten - 1;
    return ((base + taubeIndex) % 5) + 1;
}

// ── Credit helpers ────────────────────────────────────────────
int store_kredite_verfuegbar(int spieler_id)
{
    for (int i = 0; i < MAX_PORTAL_SPIELER; i++) {
        if (g_store.kreditPlayerIds[i] == spieler_id) {
            KreditStand *k = &g_store.kredite[i];
            return k->gewaehrt - k->verbraucht;
        }
    }
    return 0;
}

void store_register_spieler_fuer_tag(int spieler_id)
{
    for (int i = 0; i < MAX_PORTAL_SPIELER; i++) {
        if (g_store.kreditPlayerIds[i] == spieler_id) return; // already listed
        if (g_store.kreditPlayerIds[i] == 0) {
            g_store.kreditPlayerIds[i] = spieler_id;
            g_store.kredite[i] = (KreditStand){0, 0};
            return;
        }
    }
}

void store_add_kredite(int spieler_id, int anzahl)
{
    if (anzahl <= 0) return;
    for (int i = 0; i < MAX_PORTAL_SPIELER; i++) {
        if (g_store.kreditPlayerIds[i] == spieler_id) {
            g_store.kredite[i].gewaehrt += anzahl;
            game_store_save();
            return;
        }
        if (g_store.kreditPlayerIds[i] == 0) {
            g_store.kreditPlayerIds[i] = spieler_id;
            g_store.kredite[i] = (KreditStand){anzahl, 0};
            game_store_save();
            return;
        }
    }
}

// ── store_start_spiel ────────────────────────────────────────
bool store_start_spiel(void)
{
    GameStore *s = &g_store;

    // Check all players have credits
    for (int i = 0; i < s->spielerCount; i++) {
        if (store_kredite_verfuegbar(s->spieler[i].id) <= 0) {
            ESP_LOGW(TAG, "Player %d has no credits", s->spieler[i].id);
            return false;
        }
    }

    // Deduct one credit per player
    for (int i = 0; i < s->spielerCount; i++) {
        for (int j = 0; j < MAX_PORTAL_SPIELER; j++) {
            if (s->kreditPlayerIds[j] == s->spieler[i].id) {
                s->kredite[j].verbraucht++;
                break;
            }
        }
    }

    // Generate sequence
    const Maschine *cseq = NULL;
    int clen = 0;
    if (s->modus >= MODUS_CUSTOM_1) {
        int ci = s->modus - MODUS_CUSTOM_1;
        cseq = s->customSequenzen[ci];
        clen = s->customSequenzLen[ci];
    }
    s->sequenzLen = generate_sequenz(s->sequenz, s->modus,
                                     s->maschinenAktiv, cseq, clen);

    // Reset game state
    s->lauf         = 1;
    s->taubeIndex   = 0;
    s->spielerIndex = 0;
    s->ergebnisseCount = 0;
    for (int i = 0; i < s->spielerCount; i++) s->spieler[i].punkte = 0;

    // Generate game ID (timestamp-based)
    struct timeval tv; gettimeofday(&tv, NULL);
    snprintf(s->spielId, sizeof(s->spielId), "%lld", (long long)tv.tv_sec);

    s->screen = SCREEN_SPIEL;
    game_store_save();
    ESP_LOGI(TAG, "Game started: modus=%s seq_len=%d players=%d",
             modus_label(s->modus), s->sequenzLen, s->spielerCount);
    return true;
}

// ── store_eintragen ──────────────────────────────────────────
void store_eintragen(int punkte)
{
    GameStore *s = &g_store;
    if (s->taubeIndex >= s->sequenzLen) return;

    SequenzEintrag *se = &s->sequenz[s->taubeIndex];
    Spieler *sp = &s->spieler[s->spielerIndex];

    Ergebnis e = {};
    e.spielerId  = sp->id;
    e.lauf       = s->lauf;
    e.taube      = s->taubeIndex + 1;
    e.maschine   = se->maschine;
    e.posten     = get_current_posten(s->spielerIndex, s->taubeIndex,
                                      s->spielerCount);
    e.punkte     = punkte;
    e.wiederholt = false;

    // Scoring rules (mirrors TS eintragenErgebnis):
    // Doublette (H): each shot is 2pts. Single: 1st=2pts,2nd=1pt
    if (se->maschine == MASCHINE_H) {
        e.schuss1 = (punkte >= 1);
        e.schuss2 = false; // each H entry is one shot
    } else {
        e.schuss1 = (punkte == 2);
        e.schuss2 = (punkte == 1);
    }
    sp->punkte += punkte;

    if (s->ergebnisseCount < MAX_ERGEBNISSE)
        s->ergebnisse[s->ergebnisseCount++] = e;

    // Advance: next player; if all players done → next taube
    s->spielerIndex++;
    if (s->spielerIndex >= s->spielerCount) {
        s->spielerIndex = 0;
        s->taubeIndex++;
        // Check if this lauf is done
        if (s->taubeIndex >= s->sequenzLen) {
            if (s->lauf < 2) {  // always 2 Läufe for Normal/Harakiri
                s->lauf++;
                s->taubeIndex = 0;
            } else {
                // Game finished
                _store_finish_game();
                return;
            }
        }
    }
    game_store_save();
}

// ── _store_finish_game ───────────────────────────────────────
static void _store_finish_game(void)
{
    GameStore *s = &g_store;

    // Build FinishedGame
    FinishedGame fg = {};
    fg.base.modus         = s->modus;
    fg.base.lauf          = s->lauf;
    fg.base.taubenProLauf = s->sequenzLen;
    fg.base.abgeschlossen = true;
    fg.base.ergebnisse_count = s->ergebnisseCount;
    memcpy(fg.base.ergebnisse, s->ergebnisse,
           s->ergebnisseCount * sizeof(Ergebnis));
    fg.spieler_count = s->spielerCount;

    struct timeval tv; gettimeofday(&tv, NULL);
    snprintf(fg.finishedAt, sizeof(fg.finishedAt), "%lld", (long long)tv.tv_sec);
    strncpy(fg.base.externalId, s->spielId, sizeof(fg.base.externalId) - 1);
    fg.base.externalId[sizeof(fg.base.externalId) - 1] = '\0';

    // Snapshot player names
    for (int i = 0; i < s->spielerCount; i++) {
        fg.spielerIds[i] = s->spieler[i].id;
        strncpy(fg.spielerNamen[i], s->spieler[i].name, MAX_NAME_LEN - 1);
        fg.spielerNamen[i][MAX_NAME_LEN - 1] = '\0';
        fg.base.teilnahmen[i] = (typeof(fg.base.teilnahmen[0])){
            .spielerId   = s->spieler[i].id,
            .startPosten = s->spieler[i].startPosten,
            .punkte      = s->spieler[i].punkte,
            .lauf        = s->lauf,
        };
        fg.base.teilnahmen_count = s->spielerCount;
    }

    // Store in history (ring buffer)
    if (s->historyCount < MAX_HISTORY) {
        s->history[s->historyCount++] = fg;
    } else {
        memmove(&s->history[0], &s->history[1],
                (MAX_HISTORY - 1) * sizeof(FinishedGame));
        s->history[MAX_HISTORY - 1] = fg;
    }

    // Add to pending queue for sync
    if (s->pendingGamesCount < MAX_PENDING_GAMES) {
        s->pendingGames[s->pendingGamesCount++] = fg.base;
    }

    s->lastFinished   = fg;
    s->hasLastFinished = true;
    s->screen         = SCREEN_RESULTATE;
    game_store_save();
    ESP_LOGI(TAG, "Game finished, navigating to resultate");
}

void store_dismiss_resultate(void)
{
    g_store.hasLastFinished = false;
    store_navigate(SCREEN_DASHBOARD);
}

void store_wiederholen(void)
{
    // Mark current entry as wiederholt and re-enter it
    if (g_store.ergebnisseCount > 0) {
        g_store.ergebnisse[g_store.ergebnisseCount - 1].wiederholt = true;
    }
    // Back up one step
    if (g_store.spielerIndex > 0) {
        g_store.spielerIndex--;
    } else if (g_store.taubeIndex > 0) {
        g_store.taubeIndex--;
        g_store.spielerIndex = g_store.spielerCount - 1;
    }
    game_store_save();
}

void store_skip_taube(void)
{
    store_eintragen(0);
}

// ── Navigation ───────────────────────────────────────────────
void store_navigate(Screen s)
{
    g_store.screen = s;
    // ui_manager_show is called by the UI layer watching g_store.screen
}

// ── Portal players ───────────────────────────────────────────
void store_load_portal_spieler(void)
{
    // Spawns a FreeRTOS task so it doesn't block the LVGL loop
    xTaskCreate([](void *arg) {
        int count = 0;
        PortalSpieler buf[MAX_PORTAL_SPIELER];
        if (http_fetch_spieler(buf, MAX_PORTAL_SPIELER, &count) == ESP_OK) {
            memcpy(g_store.portalSpieler, buf,
                   count * sizeof(PortalSpieler));
            g_store.portalSpielerCount = count;
            game_store_save();
            ESP_LOGI(TAG, "Loaded %d portal players", count);
        }
        vTaskDelete(NULL);
    }, "load_spieler", 8192, NULL, 5, NULL);
}

void store_add_lokal_spieler(const char *name, int *out_id)
{
    static int s_local_id = -1;
    PortalSpieler *ps = &g_store.portalSpieler[g_store.portalSpielerCount];
    ps->id        = s_local_id--;
    ps->lokal     = true;
    ps->portalAktiv = false;
    strncpy(ps->name, name, MAX_NAME_LEN - 1);
    ps->name[MAX_NAME_LEN - 1] = '\0';
    g_store.portalSpielerCount++;
    if (out_id) *out_id = ps->id;
    game_store_save();
}

// ── Sync ─────────────────────────────────────────────────────
void store_sync(void)
{
    g_store.syncStatus = SYNC_RUNNING;
    xTaskCreate([](void *arg) {
        esp_err_t err = http_sync_all();
        g_store.syncStatus = (err == ESP_OK) ? SYNC_SUCCESS : SYNC_ERROR;
        if (err != ESP_OK) {
            snprintf(g_store.syncError, sizeof(g_store.syncError),
                     "HTTP sync failed: %s", esp_err_to_name(err));
        }
        vTaskDelete(NULL);
    }, "sync_task", 12288, NULL, 5, NULL);
}

// ── Persistence (JSON in NVS) ─────────────────────────────────
void game_store_save(void)
{
    // Save key settings & pending queue to NVS as JSON strings.
    // Full game history is large; store in FAT partition in production.
    // For phase 1 we store the essentials.
    nvs_set_str(s_nvs, "api_url", g_store.apiUrl);
    nvs_set_str(s_nvs, "api_key", g_store.apiKey);
    nvs_set_str(s_nvs, "wifi_ssid", g_store.wifiSsid);
    nvs_set_str(s_nvs, "wifi_pass", g_store.wifiPass);
    nvs_set_i32(s_nvs, "modus", (int32_t)g_store.modus);
    nvs_commit(s_nvs);
}

// ── Init ─────────────────────────────────────────────────────
void game_store_init(void)
{
    memset(&g_store, 0, sizeof(g_store));
    nvs_open();

    // Defaults
    snprintf(g_store.apiUrl, MAX_URL_LEN, "%s", DEFAULT_API_URL);
    for (int m = 0; m < MASCHINE_COUNT; m++) g_store.maschinenAktiv[m] = true;
    g_store.customLaeufe[0] = g_store.customLaeufe[1] =
    g_store.customLaeufe[2] = g_store.customLaeufe[3] = 2;
    g_store.screen = SCREEN_DASHBOARD;

    // Load persisted values
    nvs_load_str("api_url",   g_store.apiUrl,   MAX_URL_LEN);
    nvs_load_str("api_key",   g_store.apiKey,   MAX_KEY_LEN);
    nvs_load_str("wifi_ssid", g_store.wifiSsid, MAX_SSID_LEN);
    nvs_load_str("wifi_pass", g_store.wifiPass, MAX_PASS_LEN);

    int32_t modus = 0;
    if (nvs_get_i32(s_nvs, "modus", &modus) == ESP_OK)
        g_store.modus = (Modus)modus;

    ESP_LOGI(TAG, "Store initialised. API: %s modus: %s",
             g_store.apiUrl, modus_label(g_store.modus));
}
