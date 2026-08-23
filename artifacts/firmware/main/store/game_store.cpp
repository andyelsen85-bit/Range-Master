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
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"

#include "game_store.h"
#include "http_sync.h"
#include "app_config.h"

static const char *TAG = "game_store";

// Forward declaration (defined later in this file)
static void _store_finish_game(void);

// g_store is large (~40 KB with email fields) — place in SPIRAM to keep
// internal DRAM free for IDF's small BSS sections (FreeRTOS, panic, etc.)
EXT_RAM_BSS_ATTR GameStore g_store;

// ── NVS helpers ──────────────────────────────────────────────
static nvs_handle_t s_nvs;
static SemaphoreHandle_t s_kredit_events_mutex;

static void kredit_events_lock(void)
{
    configASSERT(s_kredit_events_mutex);
    xSemaphoreTake(s_kredit_events_mutex, portMAX_DELAY);
}

static void kredit_events_unlock(void)
{
    xSemaphoreGive(s_kredit_events_mutex);
}

static bool queue_kredit_event_unlocked(int spieler_id, const char *typ, int anzahl);

static void nvs_open(void)
{
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs));
}

static void set_default_custom_sequences(void)
{
    static const Maschine defaults[4][8] = {
        {MASCHINE_A, MASCHINE_B, MASCHINE_C, MASCHINE_D,
         MASCHINE_E, MASCHINE_F, MASCHINE_G, MASCHINE_H},
        {MASCHINE_A, MASCHINE_C, MASCHINE_E, MASCHINE_G,
         MASCHINE_B, MASCHINE_D, MASCHINE_F, MASCHINE_H},
        {MASCHINE_H, MASCHINE_G, MASCHINE_F, MASCHINE_E,
         MASCHINE_D, MASCHINE_C, MASCHINE_B, MASCHINE_A},
        {MASCHINE_A, MASCHINE_B, MASCHINE_C, MASCHINE_D,
         MASCHINE_E, MASCHINE_F, MASCHINE_G, MASCHINE_H},
    };
    for (int c = 0; c < 4; ++c) {
        g_store.customSequenzLen[c] = 8;
        for (int i = 0; i < 8; ++i) {
            Maschine m = defaults[c][i];
            g_store.customSequenzen[c][i] = (CustomSequenzEintrag){
                .maschine = m,
                .partner = m,
                .isDoublette = (m == MASCHINE_H),
                .delayMs = 0,
            };
        }
    }
}

static void sanitize_custom_sequences(void)
{
    for (int c = 0; c < 4; ++c) {
        if (g_store.customSequenzLen[c] < 0) g_store.customSequenzLen[c] = 0;
        if (g_store.customSequenzLen[c] > CUSTOM_SEQ_MAX)
            g_store.customSequenzLen[c] = CUSTOM_SEQ_MAX;
        if (g_store.customLaeufe[c] != 1 && g_store.customLaeufe[c] != 2)
            g_store.customLaeufe[c] = 2;
        for (int i = 0; i < g_store.customSequenzLen[c]; ++i) {
            CustomSequenzEintrag *entry = &g_store.customSequenzen[c][i];
            if (entry->maschine < MASCHINE_A || entry->maschine >= MASCHINE_COUNT)
                entry->maschine = MASCHINE_A;
            if (entry->maschine == MASCHINE_H) {
                entry->isDoublette = true;
                entry->partner = MASCHINE_H;
                entry->delayMs = 0;
            } else if (!entry->isDoublette ||
                       entry->partner < MASCHINE_A || entry->partner > MASCHINE_G ||
                       entry->partner == entry->maschine) {
                entry->isDoublette = false;
                entry->partner = entry->maschine;
                entry->delayMs = 0;
            } else if (entry->delayMs > 10000) {
                entry->delayMs = 10000;
            }
        }
    }
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

static bool append_single(SequenzEintrag *out, int *idx, Maschine machine)
{
    if (*idx >= MAX_SEQUENZ) return false;
    out[(*idx)++] = (SequenzEintrag){machine, false, false, machine, 0};
    return true;
}

static bool append_pair(SequenzEintrag *out, int *idx, Maschine first,
                        Maschine second, uint16_t delay_ms)
{
    if (*idx + 1 >= MAX_SEQUENZ) return false;
    out[(*idx)++] = (SequenzEintrag){first, false, true, second, delay_ms};
    out[(*idx)++] = (SequenzEintrag){second, true, true, first, delay_ms};
    return true;
}

static int generate_sequenz(SequenzEintrag *out, Modus modus,
                             bool *aktiv, const CustomSequenzEintrag *custom, int clen)
{
    int idx = 0;

    if (modus == MODUS_NORMAL) {
        // A-G (filtered by aktiv) + the H doublette unit.
        for (Maschine m = MASCHINE_A; m <= MASCHINE_G; m = (Maschine)((int)m + 1)) {
            if (aktiv[m]) append_single(out, &idx, m);
        }
        if (aktiv[MASCHINE_H]) {
            append_pair(out, &idx, MASCHINE_H, MASCHINE_H, 0);
        }
    } else if (modus == MODUS_HARAKIRI) {
        // Shuffle logical launch units, including the H doublette unit.
        Maschine pool[MASCHINE_COUNT]; int pcnt = 0;
        for (Maschine m = MASCHINE_A; m <= MASCHINE_G; m = (Maschine)((int)m + 1)) {
            if (aktiv[m]) pool[pcnt++] = m;
        }
        if (aktiv[MASCHINE_H]) pool[pcnt++] = MASCHINE_H;
        shuffle(pool, pcnt);
        for (int i = 0; i < pcnt; i++) {
            if (pool[i] == MASCHINE_H)
                append_pair(out, &idx, MASCHINE_H, MASCHINE_H, 0);
            else
                append_single(out, &idx, pool[i]);
        }
    } else {
        // Custom: singles stay single. H is always an H1/H2 unit. A-G
        // doublettes expand into their selected ordered machine pair.
        for (int i = 0; i < clen; i++) {
            const CustomSequenzEintrag *entry = &custom[i];
            if (entry->maschine == MASCHINE_H) {
                append_pair(out, &idx, MASCHINE_H, MASCHINE_H, 0);
            } else if (entry->isDoublette &&
                       entry->partner >= MASCHINE_A && entry->partner <= MASCHINE_G &&
                       entry->partner != entry->maschine) {
                append_pair(out, &idx, entry->maschine, entry->partner,
                            entry->delayMs > 10000 ? 10000 : entry->delayMs);
            } else {
                append_single(out, &idx, entry->maschine);
            }
        }
    }
    return idx;
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

static int find_kredit_slot(const GameStore *s, int spieler_id)
{
    for (int i = 0; i < MAX_PORTAL_SPIELER; i++) {
        if (s->kreditPlayerIds[i] == spieler_id) return i;
    }
    return -1;
}

static int find_pending_kredit_event(const GameStore *s, const char *external_id)
{
    if (!external_id || external_id[0] == '\0') return -1;
    for (int i = 0; i < s->pendingKreditEventCount; i++) {
        if (strcmp(s->pendingKreditEvents[i].externalId, external_id) == 0) return i;
    }
    return -1;
}

static void remove_pending_kredit_event(GameStore *s, int index)
{
    if (index < 0 || index >= s->pendingKreditEventCount) return;
    int tail = s->pendingKreditEventCount - index - 1;
    if (tail > 0) {
        memmove(&s->pendingKreditEvents[index], &s->pendingKreditEvents[index + 1],
                tail * sizeof(KreditEvent));
    }
    s->pendingKreditEventCount--;
    memset(&s->pendingKreditEvents[s->pendingKreditEventCount], 0,
           sizeof(KreditEvent));
}

static void clear_active_game_credit_tracking(GameStore *s)
{
    memset(s->activeGameCreditPlayerIds, 0, sizeof(s->activeGameCreditPlayerIds));
    memset(s->activeGameCreditUseIds, 0, sizeof(s->activeGameCreditUseIds));
    s->activeGameCreditCount = 0;
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
            store_queue_kredit_event(spieler_id, "GRANT", anzahl);
            game_store_save();
            return;
        }
        if (g_store.kreditPlayerIds[i] == 0) {
            g_store.kreditPlayerIds[i] = spieler_id;
            g_store.kredite[i] = (KreditStand){anzahl, 0};
            store_queue_kredit_event(spieler_id, "GRANT", anzahl);
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

    // Deduct one credit per player and queue USE event for portal sync
    kredit_events_lock();
    // A start must reserve room for every USE event. Otherwise a later quit
    // could not prove which players were charged or restore them safely.
    if (s->pendingKreditEventCount + s->spielerCount > MAX_KREDIT_EVENTS) {
        kredit_events_unlock();
        ESP_LOGW(TAG, "Not enough room to queue all game credit events");
        return false;
    }
    clear_active_game_credit_tracking(s);
    for (int i = 0; i < s->spielerCount; i++) {
        int kreditSlot = find_kredit_slot(s, s->spieler[i].id);
        if (kreditSlot < 0) {
            kredit_events_unlock();
            ESP_LOGE(TAG, "Missing credit slot for player %d", s->spieler[i].id);
            return false;
        }
        int eventIndex = s->pendingKreditEventCount;
        s->kredite[kreditSlot].verbraucht++;
        if (!queue_kredit_event_unlocked(s->spieler[i].id, "USE", 1)) {
            // Capacity was checked before charging. This is a defensive
            // failure path; retain the game setup instead of starting with a
            // credit that cannot be tracked for a later refund.
            s->kredite[kreditSlot].verbraucht--;
            kredit_events_unlock();
            return false;
        }
        s->activeGameCreditPlayerIds[s->activeGameCreditCount] = s->spieler[i].id;
        strncpy(s->activeGameCreditUseIds[s->activeGameCreditCount],
                s->pendingKreditEvents[eventIndex].externalId,
                sizeof(s->activeGameCreditUseIds[0]) - 1);
        s->activeGameCreditCount++;
    }
    kredit_events_unlock();

    // Generate sequence
    const CustomSequenzEintrag *cseq = NULL;
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
    s->currentFireSent = false;
    for (int i = 0; i < s->spielerCount; i++) s->spieler[i].punkte = 0;

    // Generate game UUID — random, globally unique, idempotent for portal sync
    {
        uint32_t r1 = esp_random(), r2 = esp_random(),
                 r3 = esp_random(), r4 = esp_random();
        snprintf(s->spielId, sizeof(s->spielId),
                 "%08x-%04x-%04x-%04x-%04x%08x",
                 (unsigned)r1, (unsigned)(r2 >> 16), (unsigned)(r2 & 0xFFFFu),
                 (unsigned)(r3 >> 16), (unsigned)(r3 & 0xFFFFu), (unsigned)r4);
    }

    s->screen = SCREEN_SPIEL;
    game_store_save();
    ESP_LOGI(TAG, "Game started: modus=%s seq_len=%d players=%d",
             modus_label(s->modus), s->sequenzLen, s->spielerCount);
    return true;
}

bool store_cancel_spiel(void)
{
    GameStore *s = &g_store;
    if (s->screen != SCREEN_SPIEL) return false;

    // A USE still in the local queue can be removed. If it has already been
    // synced, reserve one GRANT event to compensate it at the portal. Check
    // capacity before altering any local balance, so the UI never reports a
    // completed quit with only some players refunded.
    kredit_events_lock();
    int removableUses = 0;
    int grantsNeeded = 0;
    for (int i = 0; i < s->activeGameCreditCount; i++) {
        if (s->activeGameCreditUseIds[i][0] == '\0') continue;
        int eventIndex = find_pending_kredit_event(s, s->activeGameCreditUseIds[i]);
        if (eventIndex >= 0 && !s->pendingKreditEvents[eventIndex].inFlight) {
            removableUses++;
        } else {
            // An in-flight request may already have reached the portal. Keep
            // its USE event for a failed POST retry and queue a durable GRANT.
            grantsNeeded++;
        }
    }
    if (s->pendingKreditEventCount - removableUses + grantsNeeded > MAX_KREDIT_EVENTS) {
        kredit_events_unlock();
        ESP_LOGW(TAG, "Cannot quit game: no room for %d credit refund event(s)",
                 grantsNeeded);
        return false;
    }

    for (int i = 0; i < s->activeGameCreditCount; i++) {
        const char *useId = s->activeGameCreditUseIds[i];
        if (useId[0] == '\0') continue; // already refunded during a retry

        int pendingIndex = find_pending_kredit_event(s, useId);
        if (pendingIndex >= 0 && !s->pendingKreditEvents[pendingIndex].inFlight) {
            remove_pending_kredit_event(s, pendingIndex);
        } else if (!queue_kredit_event_unlocked(s->activeGameCreditPlayerIds[i],
                                                 "GRANT", 1)) {
            // Keep the dialog open and leave any remaining player entries
            // intact. The operator can sync/clear capacity and retry.
            kredit_events_unlock();
            ESP_LOGE(TAG, "Could not queue credit refund for player %d",
                     s->activeGameCreditPlayerIds[i]);
            return false;
        }

        int kreditSlot = find_kredit_slot(s, s->activeGameCreditPlayerIds[i]);
        if (kreditSlot >= 0 && s->kredite[kreditSlot].verbraucht > 0)
            s->kredite[kreditSlot].verbraucht--;
        s->activeGameCreditUseIds[i][0] = '\0';
    }
    kredit_events_unlock();

    // Never call _store_finish_game here: a quit must not create results,
    // history, a pending game, or a result screen.
    clear_active_game_credit_tracking(s);
    memset(s->spieler, 0, sizeof(s->spieler));
    memset(s->sequenz, 0, sizeof(s->sequenz));
    memset(s->ergebnisse, 0, sizeof(s->ergebnisse));
    s->spielerCount = 0;
    s->sequenzLen = 0;
    s->ergebnisseCount = 0;
    s->lauf = 1;
    s->taubeIndex = 0;
    s->spielerIndex = 0;
    s->spielId[0] = '\0';
    s->currentFireSent = false;
    s->screen = SCREEN_DASHBOARD;
    game_store_save();
    ESP_LOGI(TAG, "Game canceled; all participant credits restored");
    return true;
}

// Count pair-second entries at indices strictly before `before`.
// A pair's first and second result share one physical position.
// Subtracting this count converts a raw taubeIndex into a logical position index.
static int count_h2_before(const GameStore *s, int before) {
    int n = 0;
    for (int i = 0; i < before && i < s->sequenzLen; i++)
        if (s->sequenz[i].isPair && s->sequenz[i].isDoublette) n++;
    return n;
}

// ── store_eintragen ──────────────────────────────────────────
// Pair interleaving rule:
//   Pair first entry:
//     → record for current player, keep spielerIndex, advance taubeIndex to H2
//   Pair second entry:
//     → record for same player, then advance spielerIndex.
//       If more players remain → step taubeIndex back to H1 so next player
//       also shoots H1 then H2.
//       If all players done → advance past H2, reset spielerIndex.
void store_eintragen(int punkte)
{
    GameStore *s = &g_store;
    if (s->taubeIndex >= s->sequenzLen) return;

    SequenzEintrag *se = &s->sequenz[s->taubeIndex];
    Spieler *sp = &s->spieler[s->spielerIndex];

    // Classify current entry
    bool isPairSecond = se->isPair && se->isDoublette;
    bool isPairFirst = se->isPair && !se->isDoublette;

    // Logical position index: H1 + H2 together = ONE physical position step.
    // rawIdx: align H2 back to H1's slot; then subtract H2 entries seen before
    // that slot (each one represents a slot that does NOT advance the position).
    int rawIdx = (isPairSecond && s->taubeIndex > 0) ? s->taubeIndex - 1 : s->taubeIndex;
    int posIdx = rawIdx - count_h2_before(s, rawIdx);
    int base   = s->spieler[s->spielerIndex].startPosten - 1;

    Ergebnis e = {};
    e.spielerId  = sp->id;
    e.lauf       = s->lauf;
    e.taube      = s->taubeIndex + 1;
    e.maschine   = se->maschine;
    e.posten     = ((base + posIdx) % 5) + 1;
    e.punkte     = punkte;
    e.wiederholt = false;

    // Scoring rules
    if (se->maschine == MASCHINE_H && se->isPair) {
        e.schuss1 = (punkte >= 1);
        e.schuss2 = false;
    } else {
        e.schuss1 = (punkte == 2);
        e.schuss2 = (punkte == 1);
    }
    sp->punkte += punkte;

    if (s->ergebnisseCount < MAX_ERGEBNISSE)
        s->ergebnisse[s->ergebnisseCount++] = e;

    // ── Advance state ────────────────────────────────────────
    auto finish_lauf_or_game = [&]() -> bool {
        int max_laeufe = (s->modus >= MODUS_CUSTOM_1)
                       ? s->customLaeufe[s->modus - MODUS_CUSTOM_1]
                       : 2;
        if (s->lauf < max_laeufe) {
            // Advance each player's startPosten by the number of logical
            // position advances in lauf 1. Each pair's second entry shares the
            // same physical position as its first partner, so it does
            // NOT count as an independent position step.
            int pair_second_total = 0;
            for (int k = 0; k < s->sequenzLen; k++)
                if (s->sequenz[k].isPair && s->sequenz[k].isDoublette) pair_second_total++;
            int advances = s->sequenzLen - pair_second_total;
            for (int i = 0; i < s->spielerCount; i++) {
                s->spieler[i].startPosten =
                    ((s->spieler[i].startPosten - 1 + advances) % 5) + 1;
            }
            s->lauf++;
            s->taubeIndex   = 0;
            s->spielerIndex = 0;
            s->currentFireSent = false;
            return false;
        }
        _store_finish_game();
        return true;
    };

    if (isPairFirst) {
        // Same player shoots the second result immediately — only advance index.
        s->taubeIndex++;

    } else if (isPairSecond) {
        // Move to next player
        s->spielerIndex++;
        if (s->spielerIndex < s->spielerCount) {
            // Step back to pair first so the next player gets both results.
            s->taubeIndex--;
            s->currentFireSent = false;
        } else {
            // All players done with this pair.
            s->spielerIndex = 0;
            s->taubeIndex++;   // advance past pair second
            s->currentFireSent = false;
            if (s->taubeIndex >= s->sequenzLen) {
                if (finish_lauf_or_game()) return;
            }
        }

    } else {
        // Normal advance: next player; when all done → next taube
        s->spielerIndex++;
        // A normal single is a new physical launch for every shooter.
        s->currentFireSent = false;
        if (s->spielerIndex >= s->spielerCount) {
            s->spielerIndex = 0;
            s->taubeIndex++;
            if (s->taubeIndex >= s->sequenzLen) {
                if (finish_lauf_or_game()) return;
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
    {
        time_t now = tv.tv_sec;
        struct tm tmi_local, tmi_utc;
        localtime_r(&now, &tmi_local); // local time (CET/CEST) — for date display on terminal
        gmtime_r(&now,    &tmi_utc);   // UTC — for API timestamp (Z suffix must mean UTC)
        strftime(fg.base.datum, sizeof(fg.base.datum), "%Y-%m-%d", &tmi_local);
        strftime(fg.finishedAt, sizeof(fg.finishedAt),
                 "%Y-%m-%dT%H:%M:%S.000Z", &tmi_utc);
    }
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

    // Add to pending queue for sync — copy finishedAt into base so
    // pending_game_to_json can send the real UTC timestamp (not midnight).
    if (s->pendingGamesCount < MAX_PENDING_GAMES) {
        fg.base.finishedAt[0] = '\0';
        strncpy(fg.base.finishedAt, fg.finishedAt, sizeof(fg.base.finishedAt) - 1);
        s->pendingGames[s->pendingGamesCount++] = fg.base;
    }

    s->lastFinished   = fg;
    s->hasLastFinished = true;
    clear_active_game_credit_tracking(s);
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
    g_store.currentFireSent = false;
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

// ── Persistent workers — created once at boot while internal RAM is
// healthy.  After all 10 screens are built, internal RAM is nearly gone,
// so any xTaskCreate() at tap time silently fails (TCBs need internal
// RAM regardless of stack placement) and the action hangs.  Same pattern
// as screen_wifi_create_workers(): workers block on queues; call sites
// just queue-send, which costs no allocations.
static QueueHandle_t s_load_spieler_queue = NULL;
static QueueHandle_t s_sync_queue         = NULL;

void store_create_workers(void)
{
    // Portal-player load worker
    s_load_spieler_queue = xQueueCreate(1, sizeof(uint32_t));
    xTaskCreateWithCaps([](void *arg) {
        uint32_t dummy;
        for (;;) {
            xQueueReceive(s_load_spieler_queue, &dummy, portMAX_DELAY);
            // Heap-allocate in SPIRAM: 200×~104 B = ~20 KB would blow the
            // stack, and internal RAM may be exhausted by now.
            int count = 0;
            PortalSpieler *buf = (PortalSpieler *)heap_caps_malloc(
                MAX_PORTAL_SPIELER * sizeof(PortalSpieler),
                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (buf) {
                if (http_fetch_spieler(buf, MAX_PORTAL_SPIELER, &count) == ESP_OK) {
                    memcpy(g_store.portalSpieler, buf,
                           count * sizeof(PortalSpieler));
                    g_store.portalSpielerCount = count;
                    game_store_save();
                    ESP_LOGI(TAG, "Loaded %d portal players", count);
                }
                heap_caps_free(buf);
            }
        }
    }, "load_spieler_w", 8192, NULL, 5, NULL,
       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    // Sync worker — stack MUST be in internal RAM.
    // NVS (called from game_store_save inside the sync path) writes to SPI
    // flash, which requires disabling the cache.  If the task stack is in
    // PSRAM the cache-disable assert fires immediately (esp_task_stack_is_sane_
    // cache_disabled).  Use MALLOC_CAP_INTERNAL so the stack survives the
    // cache-off window.
    s_sync_queue = xQueueCreate(1, sizeof(uint32_t));
    xTaskCreateWithCaps([](void *arg) {
        uint32_t dummy;
        for (;;) {
            xQueueReceive(s_sync_queue, &dummy, portMAX_DELAY);
            esp_err_t err = http_sync_all();
            g_store.syncStatus = (err == ESP_OK) ? SYNC_SUCCESS : SYNC_ERROR;
            if (err != ESP_OK) {
                snprintf(g_store.syncError, sizeof(g_store.syncError),
                         "HTTP sync failed: %s", esp_err_to_name(err));
            }
        }
    }, "sync_w", 16384, NULL, 5, NULL,
       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    ESP_LOGI(TAG, "Store workers created. Internal RAM remaining: %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

// ── Portal players ───────────────────────────────────────────
void store_load_portal_spieler(void)
{
    // Just trigger the persistent worker — costs no RAM at call time.
    if (!s_load_spieler_queue) return;
    uint32_t trigger = 1;
    xQueueSend(s_load_spieler_queue, &trigger, 0);
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

    // Queue a CREATE entry so the next sync pushes this player to the portal.
    if (g_store.spielerUpdateCount < MAX_SPIELER_UPDATES) {
        SpielerUpdateEntry *e = &g_store.spielerUpdates[g_store.spielerUpdateCount++];
        e->used      = true;
        e->spielerId = ps->id;   // negative local ID; sync replaces with portal ID
        e->typ       = SPIELER_CREATE;
        snprintf(e->externalId, sizeof(e->externalId), "new-%08x", (unsigned)esp_random());
        strncpy(e->name, name, MAX_NAME_LEN - 1);
        e->name[MAX_NAME_LEN - 1] = '\0';
        e->email[0]    = '\0';
        e->portalAktiv = false;
        ESP_LOGI(TAG, "Queued CREATE for local spieler '%s' (localId=%d)", name, ps->id);
    }

    game_store_save();
}

// ── Player update queue ──────────────────────────────────────
static bool queue_kredit_event_unlocked(int spieler_id, const char *typ, int anzahl)
{
    if (anzahl == 0) return true;
    if (g_store.pendingKreditEventCount >= MAX_KREDIT_EVENTS) {
        ESP_LOGW(TAG, "Kredit event queue full — dropping event for player %d", spieler_id);
        return false;
    }
    KreditEvent *ev = &g_store.pendingKreditEvents[g_store.pendingKreditEventCount++];
    snprintf(ev->externalId, sizeof(ev->externalId),
             "kre-%d-%08x", spieler_id, (unsigned)esp_random());
    ev->spielerId = spieler_id;
    ev->anzahl    = anzahl;
    strncpy(ev->typ, typ, sizeof(ev->typ) - 1);
    ev->typ[sizeof(ev->typ) - 1] = '\0';
    struct timeval tv; gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    struct tm tmi; localtime_r(&now, &tmi);  // use local TZ (CET/CEST)
    strftime(ev->datum, sizeof(ev->datum), "%Y-%m-%d", &tmi);
    return true;
}

bool store_queue_kredit_event(int spieler_id, const char *typ, int anzahl)
{
    kredit_events_lock();
    bool queued = queue_kredit_event_unlocked(spieler_id, typ, anzahl);
    kredit_events_unlock();
    return queued;
}

int store_begin_kredit_event_sync(KreditEvent *snapshot, int capacity)
{
    if (!snapshot || capacity <= 0) return 0;
    kredit_events_lock();
    int count = 0;
    for (int i = 0; i < g_store.pendingKreditEventCount && count < capacity; i++) {
        KreditEvent *event = &g_store.pendingKreditEvents[i];
        if (event->spielerId <= 0 || event->inFlight) continue;
        event->inFlight = true;
        snapshot[count++] = *event;
    }
    kredit_events_unlock();
    return count;
}

void store_finish_kredit_event_sync(const KreditEvent *snapshot, int count,
                                    bool delivered)
{
    if (!snapshot || count <= 0) return;
    kredit_events_lock();
    for (int i = 0; i < count; i++) {
        int eventIndex = find_pending_kredit_event(&g_store, snapshot[i].externalId);
        if (eventIndex < 0) continue;
        if (delivered) {
            remove_pending_kredit_event(&g_store, eventIndex);
        } else {
            g_store.pendingKreditEvents[eventIndex].inFlight = false;
        }
    }
    kredit_events_unlock();
}

void store_apply_portal_kredit(int spieler_id, int gewaehrt, int verbraucht)
{
    kredit_events_lock();
    int kreditSlot = find_kredit_slot(&g_store, spieler_id);
    if (kreditSlot < 0) {
        for (int i = 0; i < MAX_PORTAL_SPIELER; i++) {
            if (g_store.kreditPlayerIds[i] == 0) {
                kreditSlot = i;
                g_store.kreditPlayerIds[i] = spieler_id;
                break;
            }
        }
    }
    if (kreditSlot >= 0) {
        // Portal totals are the baseline. Reapply every local event that is
        // still queued, including a cancellation GRANT created while a USE
        // was in-flight, so a pull never hides the locally restored credit.
        for (int i = 0; i < g_store.pendingKreditEventCount; i++) {
            const KreditEvent *event = &g_store.pendingKreditEvents[i];
            if (event->spielerId != spieler_id) continue;
            if (strcmp(event->typ, "GRANT") == 0) {
                gewaehrt += event->anzahl;
            } else if (strcmp(event->typ, "USE") == 0) {
                verbraucht += event->anzahl;
            }
        }
        g_store.kredite[kreditSlot].gewaehrt = gewaehrt;
        g_store.kredite[kreditSlot].verbraucht = verbraucht;
    }
    kredit_events_unlock();
}

void store_queue_spieler_update(int spieler_id, const char *name, const char *email, bool portal_aktiv)
{
    if (spieler_id < 0) {
        // Local player — a SPIELER_CREATE entry is already in the queue.
        // Update that entry's name/email in-place so the next sync uses the
        // latest values; do not queue a separate UPDATE (portal doesn't know
        // this player yet so it has no spielerId to update).
        for (int i = 0; i < g_store.spielerUpdateCount; i++) {
            SpielerUpdateEntry *e = &g_store.spielerUpdates[i];
            if (e->used && e->spielerId == spieler_id && e->typ == SPIELER_CREATE) {
                if (name)  { strncpy(e->name, name, MAX_NAME_LEN - 1); e->name[MAX_NAME_LEN - 1] = '\0'; }
                if (email) { strncpy(e->email, email, MAX_EMAIL_LEN - 1); e->email[MAX_EMAIL_LEN - 1] = '\0'; }
                ESP_LOGI(TAG, "Updated pending CREATE for local spieler %d (name='%s')", spieler_id, e->name);
                return;
            }
        }
        ESP_LOGW(TAG, "No pending CREATE found for local spieler %d — ignoring UPDATE", spieler_id);
        return;
    }
    if (g_store.spielerUpdateCount >= MAX_SPIELER_UPDATES) {
        ESP_LOGW(TAG, "spielerUpdates queue full");
        return;
    }
    SpielerUpdateEntry *e = &g_store.spielerUpdates[g_store.spielerUpdateCount++];
    e->used       = true;
    e->spielerId  = spieler_id;
    e->typ        = SPIELER_UPDATE_PROFILE;
    snprintf(e->externalId, sizeof(e->externalId), "upd-%d-%u",
             spieler_id, (unsigned)xTaskGetTickCount());
    strncpy(e->name,  name  ? name  : "", MAX_NAME_LEN - 1);
    e->name[MAX_NAME_LEN - 1] = '\0';
    strncpy(e->email, email ? email : "", MAX_EMAIL_LEN - 1);
    e->email[MAX_EMAIL_LEN - 1] = '\0';
    e->portalAktiv = portal_aktiv;

    // Reflect changes in local cache immediately
    for (int i = 0; i < g_store.portalSpielerCount; i++) {
        if (g_store.portalSpieler[i].id == spieler_id) {
            if (name) {
                strncpy(g_store.portalSpieler[i].name, name, MAX_NAME_LEN - 1);
                g_store.portalSpieler[i].name[MAX_NAME_LEN - 1] = '\0';
            }
            g_store.portalSpieler[i].portalAktiv = portal_aktiv;
            break;
        }
    }
    ESP_LOGI(TAG, "Queued spieler update id=%d name='%s' aktiv=%d", spieler_id, e->name, (int)portal_aktiv);
}

void store_queue_passwort_reset(int spieler_id)
{
    if (g_store.spielerUpdateCount >= MAX_SPIELER_UPDATES) {
        ESP_LOGW(TAG, "spielerUpdates queue full");
        return;
    }
    SpielerUpdateEntry *e = &g_store.spielerUpdates[g_store.spielerUpdateCount++];
    e->used      = true;
    e->spielerId = spieler_id;
    e->typ       = SPIELER_UPDATE_PASSWORT_RESET;
    snprintf(e->externalId, sizeof(e->externalId), "pwd-%d-%u",
             spieler_id, (unsigned)xTaskGetTickCount());
    ESP_LOGI(TAG, "Queued passwort reset for spieler id=%d", spieler_id);
}

int store_pending_update_count(void) { return g_store.spielerUpdateCount; }

// ── Sync ─────────────────────────────────────────────────────
void store_sync(void)
{
    // Just trigger the persistent worker — costs no RAM at call time.
    if (!s_sync_queue) return;
    g_store.syncStatus = SYNC_RUNNING;
    uint32_t trigger = 1;
    xQueueSend(s_sync_queue, &trigger, 0);
}

// ── Persistence (JSON in NVS) ─────────────────────────────────
void game_store_save(void)
{
    // Save key settings & pending queue to NVS as JSON strings.
    // Full game history is large; store in FAT partition in production.
    // For phase 1 we store the essentials.
    nvs_set_str(s_nvs, "api_url", g_store.apiUrl);
    nvs_set_str(s_nvs, "api_key", g_store.apiKey);
    nvs_set_str(s_nvs, "gateway_url", g_store.gatewayUrl);
    nvs_set_str(s_nvs, "gateway_token", g_store.gatewayToken);
    nvs_set_u32(s_nvs, "gateway_seq", g_store.gatewaySequence);
    nvs_set_str(s_nvs, "wifi_ssid", g_store.wifiSsid);
    nvs_set_str(s_nvs, "wifi_pass", g_store.wifiPass);
    nvs_set_i32(s_nvs, "modus", (int32_t)g_store.modus);
    nvs_set_i32(s_nvs, "click_snd", g_store.clickSoundEnabled ? 1 : 0);
    nvs_set_blob(s_nvs, "custom_seq", g_store.customSequenzen,
                 sizeof(g_store.customSequenzen));
    nvs_set_blob(s_nvs, "custom_len", g_store.customSequenzLen,
                 sizeof(g_store.customSequenzLen));
    nvs_set_blob(s_nvs, "custom_run", g_store.customLaeufe,
                 sizeof(g_store.customLaeufe));
    nvs_commit(s_nvs);
}

// ── Init ─────────────────────────────────────────────────────
void game_store_init(void)
{
    memset(&g_store, 0, sizeof(g_store));
    s_kredit_events_mutex = xSemaphoreCreateMutex();
    configASSERT(s_kredit_events_mutex);
    nvs_open();

    // Defaults
    snprintf(g_store.apiUrl, MAX_URL_LEN, "%s", DEFAULT_API_URL);
    snprintf(g_store.apiKey, MAX_KEY_LEN, "%s", DEFAULT_API_KEY);
    for (int m = 0; m < MASCHINE_COUNT; m++) g_store.maschinenAktiv[m] = true;
    g_store.customLaeufe[0] = g_store.customLaeufe[1] =
    g_store.customLaeufe[2] = g_store.customLaeufe[3] = 2;
    set_default_custom_sequences();
    g_store.screen = SCREEN_DASHBOARD;

    // Load persisted values
    nvs_load_str("api_url",   g_store.apiUrl,   MAX_URL_LEN);
    nvs_load_str("api_key",   g_store.apiKey,   MAX_KEY_LEN);
    nvs_load_str("gateway_url", g_store.gatewayUrl, MAX_URL_LEN);
    nvs_load_str("gateway_token", g_store.gatewayToken, MAX_KEY_LEN);
    nvs_get_u32(s_nvs, "gateway_seq", &g_store.gatewaySequence);
    // NVS may have stored an empty string from a previous flash — restore default
    if (g_store.apiKey[0] == '\0')
        snprintf(g_store.apiKey, MAX_KEY_LEN, "%s", DEFAULT_API_KEY);
    if (g_store.apiUrl[0] == '\0')
        snprintf(g_store.apiUrl, MAX_URL_LEN, "%s", DEFAULT_API_URL);
    nvs_load_str("wifi_ssid", g_store.wifiSsid, TM_MAX_SSID_LEN);
    nvs_load_str("wifi_pass", g_store.wifiPass, MAX_PASS_LEN);

    size_t custom_seq_size = sizeof(g_store.customSequenzen);
    size_t custom_len_size = sizeof(g_store.customSequenzLen);
    size_t custom_run_size = sizeof(g_store.customLaeufe);
    bool custom_loaded =
        nvs_get_blob(s_nvs, "custom_seq", g_store.customSequenzen, &custom_seq_size) == ESP_OK &&
        custom_seq_size == sizeof(g_store.customSequenzen) &&
        nvs_get_blob(s_nvs, "custom_len", g_store.customSequenzLen, &custom_len_size) == ESP_OK &&
        custom_len_size == sizeof(g_store.customSequenzLen) &&
        nvs_get_blob(s_nvs, "custom_run", g_store.customLaeufe, &custom_run_size) == ESP_OK &&
        custom_run_size == sizeof(g_store.customLaeufe);
    if (!custom_loaded) set_default_custom_sequences();
    sanitize_custom_sequences();

    int32_t modus = 0;
    if (nvs_get_i32(s_nvs, "modus", &modus) == ESP_OK)
        g_store.modus = (Modus)modus;

    int32_t click_snd = 1;  // default ON
    if (nvs_get_i32(s_nvs, "click_snd", &click_snd) == ESP_OK)
        g_store.clickSoundEnabled = (click_snd != 0);
    else
        g_store.clickSoundEnabled = true;  // first boot — enable by default

    ESP_LOGI(TAG, "Store initialised. API: %s modus: %s",
             g_store.apiUrl, modus_label(g_store.modus));
    ESP_LOGI(TAG, "API key in use: '%s'", g_store.apiKey);
}
