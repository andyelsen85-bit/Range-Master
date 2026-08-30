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
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"

#include "game_store.h"
#include "http_sync.h"
#include "coprocessor.h"
#include "app_config.h"

static const char *TAG = "game_store";
static_assert(AUTO_SYNC_MIN_SECONDS <= AUTO_SYNC_DEFAULT_SECONDS &&
              AUTO_SYNC_DEFAULT_SECONDS <= AUTO_SYNC_MAX_SECONDS,
              "Auto-sync default must remain within persisted validation bounds");
static_assert(MAX_KREDIT_EVENTS > 0,
              "Credit event persistence requires a non-empty outbox");
static_assert(sizeof(((KreditEvent *)0)->externalId) >= 16,
              "Credit event IDs must remain durable idempotency keys");
static_assert(VERKAUF_PRICE_REVISION_UNALLOCATED == 0,
              "Sales correction wire sentinel is part of the API contract");

// Forward declaration (defined later in this file)
static void _store_finish_game(void);

// g_store is large (~40 KB with email fields) — place in SPIRAM to keep
// internal DRAM free for IDF's small BSS sections (FreeRTOS, panic, etc.)
EXT_RAM_BSS_ATTR GameStore g_store;

// ── NVS helpers ──────────────────────────────────────────────
static nvs_handle_t s_nvs;
static SemaphoreHandle_t s_kredit_events_mutex;
static SemaphoreHandle_t s_verkauf_events_mutex;

#define CATERING_PIN_MIN_LEN 4
#define CATERING_PIN_MAX_LEN 16
#define CATERING_PIN_ITERATIONS 120000
#define CATERING_PIN_MAX_FAILURES 5
#define CATERING_PIN_LOCK_SECONDS 30
#define VALID_UNIX_TIME 1704067200LL /* 2024-01-01 */

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
static bool save_kredit_state_unlocked(void);

// This is deliberately a single NVS transaction: the projected balance must
// never be made durable without the immutable event which explains it.
static bool save_kredit_state_unlocked(void)
{
    esp_err_t err = nvs_set_str(s_nvs, "credit_date", g_store.kreditDatum);
    if (err == ESP_OK)
        err = nvs_set_blob(s_nvs, "credit_ids", g_store.kreditPlayerIds,
                           sizeof(g_store.kreditPlayerIds));
    if (err == ESP_OK)
        err = nvs_set_blob(s_nvs, "credits", g_store.kredite, sizeof(g_store.kredite));
    if (err == ESP_OK)
        err = nvs_set_blob(s_nvs, "credit_events", g_store.pendingKreditEvents,
                           sizeof(g_store.pendingKreditEvents));
    if (err == ESP_OK)
        err = nvs_set_i32(s_nvs, "credit_evt_cnt", g_store.pendingKreditEventCount);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) ESP_LOGE(TAG, "Could not persist credit state: %s", esp_err_to_name(err));
    return err == ESP_OK;
}

static int find_munition_slot(int spieler_id)
{
    for (int i = 0; i < MAX_PORTAL_SPIELER; ++i)
        if (g_store.munition[i].spielerId == spieler_id) return i;
    for (int i = 0; i < MAX_PORTAL_SPIELER; ++i) {
        if (g_store.munition[i].spielerId == 0) {
            g_store.munition[i].spielerId = spieler_id;
            return i;
        }
    }
    return -1;
}

const Produkt *store_produkt(const char *produkt_code)
{
    if (!produkt_code) return NULL;
    for (int i = 0; i < g_store.produkteCount; ++i)
        if (g_store.produkte[i].active &&
            strcmp(g_store.produkte[i].code, produkt_code) == 0) return &g_store.produkte[i];
    return NULL;
}

int store_munition_cal12(int spieler_id) {
    for (int i = 0; i < MAX_PORTAL_SPIELER; ++i)
        if (g_store.munition[i].spielerId == spieler_id) return g_store.munition[i].cal12;
    return 0;
}
int store_munition_cal20(int spieler_id) {
    for (int i = 0; i < MAX_PORTAL_SPIELER; ++i)
        if (g_store.munition[i].spielerId == spieler_id) return g_store.munition[i].cal20;
    return 0;
}

bool store_payment_pending(int spieler_id)
{
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &tm);
    for (int i = 0; i < g_store.pendingPaymentEventCount; ++i)
        if (g_store.pendingPaymentEvents[i].spielerId == spieler_id &&
            strcmp(g_store.pendingPaymentEvents[i].datum, today) == 0) return true;
    return false;
}

bool store_queue_payment(int spieler_id)
{
    if (spieler_id <= 0) return false;
    kredit_events_lock();
    // One unresolved Paid action per player/day is the local idempotency
    // boundary. Re-tapping a pending bill can never create a second payment.
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &tm);
    bool duplicate = false;
    for (int i = 0; i < g_store.pendingPaymentEventCount; ++i)
        if (g_store.pendingPaymentEvents[i].spielerId == spieler_id &&
            strcmp(g_store.pendingPaymentEvents[i].datum, today) == 0) {
            duplicate = true; break;
        }
    if (duplicate || g_store.pendingPaymentEventCount >= MAX_PENDING_PAYMENTS) {
        kredit_events_unlock();
        return false;
    }
    bool active = false;
    for (int i = 0; i < MAX_PORTAL_SPIELER; ++i)
        if (g_store.kreditPlayerIds[i] == spieler_id) { active = true; break; }
    if (!active) { kredit_events_unlock(); return false; }
    PaymentEvent *event =
        &g_store.pendingPaymentEvents[g_store.pendingPaymentEventCount++];
    memset(event, 0, sizeof(*event));
    snprintf(event->externalId, sizeof(event->externalId), "pay-%d-%08x",
             spieler_id, (unsigned)esp_random());
    event->spielerId = spieler_id;
    strftime(event->datum, sizeof(event->datum), "%Y-%m-%d", &tm);
    kredit_events_unlock();
    game_store_save();
    return true;
}

int store_begin_payment_sync(PaymentEvent *snapshot, int capacity)
{
    if (!snapshot || capacity <= 0) return 0;
    kredit_events_lock();
    int count = 0;
    for (int i = 0; i < g_store.pendingPaymentEventCount && count < capacity; ++i) {
        PaymentEvent *event = &g_store.pendingPaymentEvents[i];
        if (event->spielerId <= 0 || event->inFlight) continue;
        event->inFlight = true;
        snapshot[count++] = *event;
    }
    kredit_events_unlock();
    game_store_save();
    return count;
}

void store_finish_payment_sync(const PaymentEvent *snapshot, int count,
                               const char *const *acceptedIds, int acceptedCount,
                               const char *error)
{
    if (!snapshot || count <= 0) return;
    kredit_events_lock();
    for (int n = 0; n < count; ++n) {
        bool accepted = false;
        for (int a = 0; a < acceptedCount; ++a)
            if (acceptedIds[a] && !strcmp(snapshot[n].externalId, acceptedIds[a]))
                { accepted = true; break; }
        for (int i = 0; i < g_store.pendingPaymentEventCount; ++i) {
            PaymentEvent *event = &g_store.pendingPaymentEvents[i];
            if (strcmp(event->externalId, snapshot[n].externalId)) continue;
            if (accepted) {
                // Portal acceptance is the sole authorization to retire a
                // Player of Day.  Sales/history remain append-only elsewhere.
                time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
                char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &tm);
                for (int k = 0; k < MAX_PORTAL_SPIELER; ++k)
                    if (strcmp(event->datum, today) == 0 &&
                        g_store.kreditPlayerIds[k] == event->spielerId) {
                        g_store.kreditPlayerIds[k] = 0;
                        g_store.kredite[k] = (KreditStand){};
                        break;
                    }
                memmove(event, event + 1, (size_t)(g_store.pendingPaymentEventCount - i - 1) *
                                         sizeof(*event));
                memset(&g_store.pendingPaymentEvents[--g_store.pendingPaymentEventCount],
                       0, sizeof(*event));
            } else {
                event->inFlight = false;
                snprintf(event->lastError, sizeof(event->lastError), "%s",
                         error && error[0] ? error : "Portal nicht bestaetegt");
            }
            break;
        }
    }
    kredit_events_unlock();
    game_store_save();
}

void store_cache_bill_day(const BillDaySummary *summary)
{
    if (!summary || summary->playerCount < 0 || summary->playerCount > MAX_DAY_BILLS)
        return;
    g_store.billDay = *summary;
    // Keep the large offline snapshot out of game_store_save(): scoring and
    // FIRE sequence commits must not rewrite ~20KB on the LVGL path.
    nvs_set_blob(s_nvs, "bill_day", &g_store.billDay, sizeof(g_store.billDay));
    nvs_commit(s_nvs);
}

static int munition_caliber_for_product(int produkt_id)
{
    for (int i = 0; i < g_store.produkteCount; ++i) {
        const Produkt *p = &g_store.produkte[i];
        if (p->id != produkt_id) continue;
        if (strcmp(p->code, "AMMO_CAL12") == 0 ||
            strcmp(p->category, "AMMO_CAL12") == 0) return 12;
        if (strcmp(p->code, "AMMO_CAL20") == 0 ||
            strcmp(p->category, "AMMO_CAL20") == 0) return 20;
    }
    return 0;
}

void store_apply_portal_verkauf(int spieler_id, int produkt_id, int quantity)
{
    int caliber = munition_caliber_for_product(produkt_id);
    if (!caliber) return;
    if (caliber == 12) g_store.verkaufCal12Total += quantity;
    else g_store.verkaufCal20Total += quantity;
    if (spieler_id <= 0) return; // sales aggregate has no player attribution
    int slot = find_munition_slot(spieler_id);
    if (slot < 0) return;
    if (caliber == 12) g_store.munition[slot].cal12 += quantity;
    else g_store.munition[slot].cal20 += quantity;
}

void store_replace_produkte(const Produkt *produkte, int count)
{
    if (!produkte || count < 0) return;
    if (count > MAX_PRODUKTE) count = MAX_PRODUKTE;
    memset(g_store.produkte, 0, sizeof(g_store.produkte));
    memcpy(g_store.produkte, produkte, count * sizeof(Produkt));
    g_store.produkteCount = count;
}

bool store_queue_verkauf(int spieler_id, const char *produkt_code, int quantity)
{
    const Produkt *produkt = store_produkt(produkt_code);
    // A correction must remain possible when its product has since been
    // deactivated. It still requires a known ammunition product ID, but must
    // not inherit the current price revision: the portal allocates it to the
    // original immutable sale lot using the zero sentinel.
    if (!produkt && quantity < 0 && produkt_code) {
        for (int i = 0; i < g_store.produkteCount; ++i)
            if (strcmp(g_store.produkte[i].code, produkt_code) == 0) {
                produkt = &g_store.produkte[i];
                break;
            }
    }
    if (!produkt || produkt->id <= 0 || spieler_id == 0 || quantity == 0)
        return false;
    int caliber = munition_caliber_for_product(produkt->id);
    if (quantity > 0 && (!produkt->active || produkt->preisRevisionId <= 0))
        return false;
    if (quantity < 0 && (caliber != 12 && caliber != 20))
        return false;
    time_t now = time(NULL); struct tm tmi; localtime_r(&now, &tmi);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &tmi);
    xSemaphoreTake(s_verkauf_events_mutex, portMAX_DELAY);
    if (strcmp(g_store.verkaufDatum, today) != 0) {
        memset(g_store.munition, 0, sizeof(g_store.munition));
        g_store.verkaufCal12Total = g_store.verkaufCal20Total = 0;
        strncpy(g_store.verkaufDatum, today, sizeof(g_store.verkaufDatum) - 1);
    }
    if (g_store.pendingVerkaufEventCount >= MAX_PENDING_VERKAEUFE) {
        xSemaphoreGive(s_verkauf_events_mutex);
        ESP_LOGW(TAG, "Sale outbox full");
        return false;
    }
    // Corrections are signed immutable sale events.  Reject an invalid
    // correction at the transaction boundary as well as in the UI.
    if (quantity < 0) {
        int current = 0;
        for (int i = 0; i < MAX_PORTAL_SPIELER; ++i)
            if (g_store.munition[i].spielerId == spieler_id) {
                current = caliber == 12 ? g_store.munition[i].cal12 :
                          caliber == 20 ? g_store.munition[i].cal20 : 0;
                break;
            }
        if (current < -quantity) {
            xSemaphoreGive(s_verkauf_events_mutex);
            return false;
        }
    }
    VerkaufEvent *event = &g_store.pendingVerkaufEvents[g_store.pendingVerkaufEventCount++];
    memset(event, 0, sizeof(*event));
    snprintf(event->externalId, sizeof(event->externalId), "sal-%d-%08x",
             spieler_id, (unsigned)esp_random());
    event->spielerId = spieler_id;
    event->produktId = produkt->id;
    event->preisRevisionId = quantity < 0 ? VERKAUF_PRICE_REVISION_UNALLOCATED
                                          : produkt->preisRevisionId;
    event->quantity = quantity;
    strftime(event->datum, sizeof(event->datum), "%Y-%m-%d", &tmi);
    store_apply_portal_verkauf(spieler_id, produkt->id, quantity);
    xSemaphoreGive(s_verkauf_events_mutex);
    game_store_save();
    return true;
}

static bool catering_player_is_active(int spieler_id)
{
    if (spieler_id <= 0) return false;
    // The daily registration is part of the sale authorization, not merely a
    // UI filter. Do this check at the enqueue transaction boundary.
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &tm);
    if (strcmp(g_store.kreditDatum, today) != 0) return false;
    for (int i = 0; i < g_store.portalSpielerCount; ++i)
        if (g_store.portalSpieler[i].id == spieler_id && g_store.portalSpieler[i].portalAktiv)
            for (int k = 0; k < MAX_PORTAL_SPIELER; ++k)
                if (g_store.kreditPlayerIds[k] == spieler_id) return true;
    return false;
}

bool store_queue_catering_basket(int spieler_id, const int *produkt_ids,
                                 const int *quantities, int line_count)
{
    if (!produkt_ids || !quantities || line_count < 1 || line_count > MAX_PRODUKTE)
        return false;
    kredit_events_lock();
    bool authorized = catering_player_is_active(spieler_id);
    kredit_events_unlock();
    if (!authorized) return false;
    time_t now = time(NULL); struct tm tmi; localtime_r(&now, &tmi);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &tmi);
    xSemaphoreTake(s_verkauf_events_mutex, portMAX_DELAY);
    // Revalidate the cached catalog under the same lock as reservation.
    for (int line = 0; line < line_count; ++line) {
        const Produkt *p = NULL;
        for (int i = 0; i < g_store.produkteCount; ++i)
            if (g_store.produkte[i].id == produkt_ids[line]) { p = &g_store.produkte[i]; break; }
        if (!p || !p->active || p->preisRevisionId <= 0 || quantities[line] <= 0 ||
            (strcmp(p->category, "FOOD") && strcmp(p->category, "DRINK"))) {
            xSemaphoreGive(s_verkauf_events_mutex);
            return false;
        }
    }
    // Reserve all slots before writing any event: a full outbox is all-or-nothing.
    if (g_store.pendingVerkaufEventCount + line_count > MAX_PENDING_VERKAEUFE) {
        xSemaphoreGive(s_verkauf_events_mutex);
        return false;
    }
    if (strcmp(g_store.verkaufDatum, today) != 0) {
        memset(g_store.munition, 0, sizeof(g_store.munition));
        g_store.verkaufCal12Total = g_store.verkaufCal20Total = 0;
        snprintf(g_store.verkaufDatum, sizeof(g_store.verkaufDatum), "%s", today);
    }
    for (int line = 0; line < line_count; ++line) {
        const Produkt *p = NULL;
        for (int i = 0; i < g_store.produkteCount; ++i)
            if (g_store.produkte[i].id == produkt_ids[line]) { p = &g_store.produkte[i]; break; }
        VerkaufEvent *event = &g_store.pendingVerkaufEvents[g_store.pendingVerkaufEventCount++];
        memset(event, 0, sizeof(*event));
        snprintf(event->externalId, sizeof(event->externalId), "cat-%d-%08x",
                 spieler_id, (unsigned)esp_random());
        event->spielerId = spieler_id; event->produktId = p->id;
        event->preisRevisionId = p->preisRevisionId; event->quantity = quantities[line];
        snprintf(event->datum, sizeof(event->datum), "%s", today);
    }
    // Commit while the outbox remains locked. The NVS commit is atomic; on a
    // failure remove the entire just-created basket from RAM as well, so a
    // caller can never observe a partially accepted basket.
    int first_event = g_store.pendingVerkaufEventCount - line_count;
    esp_err_t persist = nvs_set_blob(s_nvs, "sales", g_store.pendingVerkaufEvents,
                                     sizeof(g_store.pendingVerkaufEvents));
    if (persist == ESP_OK)
        persist = nvs_set_i32(s_nvs, "sale_count", g_store.pendingVerkaufEventCount);
    if (persist == ESP_OK)
        persist = nvs_set_str(s_nvs, "sale_date", g_store.verkaufDatum);
    if (persist == ESP_OK) persist = nvs_commit(s_nvs);
    if (persist != ESP_OK) {
        memset(&g_store.pendingVerkaufEvents[first_event], 0,
               (size_t)line_count * sizeof(VerkaufEvent));
        g_store.pendingVerkaufEventCount = first_event;
    }
    xSemaphoreGive(s_verkauf_events_mutex);
    return persist == ESP_OK;
}

static bool catering_pin_digest(const char *pin, const uint8_t salt[16], uint8_t out[32])
{
    size_t len = pin ? strnlen(pin, CATERING_PIN_MAX_LEN + 1) : 0;
    if (len < CATERING_PIN_MIN_LEN || len > CATERING_PIN_MAX_LEN) return false;
    return mbedtls_pkcs5_pbkdf2_hmac_ext(
               MBEDTLS_MD_SHA256, (const unsigned char *)pin, len,
               salt, 16, CATERING_PIN_ITERATIONS,
               sizeof(g_store.cateringPinHash), out) == 0;
}

bool store_set_catering_pin(const char *pin)
{
    uint8_t digest[32], salt[16];
    for (size_t i = 0; i < sizeof(salt); i += sizeof(uint32_t)) {
        uint32_t r = esp_random(); memcpy(salt + i, &r, sizeof(r));
    }
    if (!catering_pin_digest(pin, salt, digest)) return false;
    memcpy(g_store.cateringPinSalt, salt, sizeof(salt));
    memcpy(g_store.cateringPinHash, digest, sizeof(digest));
    memset(digest, 0, sizeof(digest));
    g_store.cateringPinConfigured = true;
    g_store.cateringPinFailures = 0;
    g_store.cateringPinLockoutUntil = 0;
    game_store_save();
    return true;
}

bool store_catering_pin_configured(void) { return g_store.cateringPinConfigured; }
CateringPinVerifyResult store_verify_catering_pin(const char *pin)
{
    time_t now = time(NULL);
    if (!g_store.cateringPinConfigured) return CATERING_PIN_NOT_CONFIGURED;
    if ((int64_t)now >= VALID_UNIX_TIME &&
        g_store.cateringPinLockoutUntil > (int64_t)now) return CATERING_PIN_LOCKED;
    uint8_t digest[32]; bool equal = true;
    if (!catering_pin_digest(pin, g_store.cateringPinSalt, digest)) return CATERING_PIN_WRONG;
    for (size_t i = 0; i < sizeof(digest); ++i) equal &= digest[i] == g_store.cateringPinHash[i];
    memset(digest, 0, sizeof(digest));
    if (equal) {
        g_store.cateringPinFailures = 0; g_store.cateringPinLockoutUntil = 0;
        game_store_save();
        return CATERING_PIN_OK;
    }
    if (g_store.cateringPinFailures < UINT8_MAX) g_store.cateringPinFailures++;
    if (g_store.cateringPinFailures >= CATERING_PIN_MAX_FAILURES &&
        (int64_t)now >= VALID_UNIX_TIME)
        g_store.cateringPinLockoutUntil = (int64_t)now + CATERING_PIN_LOCK_SECONDS;
    game_store_save();
    // Without a trustworthy RTC an absolute persisted expiry cannot be made
    // safe. Retain the persisted failure threshold (valid PIN still clears
    // it) rather than inventing a permanent time-based lockout.
    if ((int64_t)now < VALID_UNIX_TIME &&
        g_store.cateringPinFailures >= CATERING_PIN_MAX_FAILURES)
        return CATERING_PIN_LOCKED;
    return g_store.cateringPinLockoutUntil > (int64_t)now ? CATERING_PIN_LOCKED : CATERING_PIN_WRONG;
}
uint32_t store_catering_pin_lockout_remaining(void)
{
    time_t now = time(NULL);
    if ((int64_t)now < VALID_UNIX_TIME || g_store.cateringPinLockoutUntil <= (int64_t)now) return 0;
    return (uint32_t)(g_store.cateringPinLockoutUntil - (int64_t)now);
}
bool store_set_operating_mode(TerminalOperatingMode mode)
{
    if (mode != TERMINAL_MODE_NORMAL && mode != TERMINAL_MODE_CATERING) return false;
    if (mode == TERMINAL_MODE_CATERING && !g_store.cateringPinConfigured) return false;
    g_store.operatingMode = mode; game_store_save(); return true;
}

int store_begin_verkauf_sync(VerkaufEvent *snapshot, int capacity)
{
    if (!snapshot || capacity <= 0) return 0;
    xSemaphoreTake(s_verkauf_events_mutex, portMAX_DELAY);
    int count = 0;
    for (int i = 0; i < g_store.pendingVerkaufEventCount && count < capacity; ++i) {
        VerkaufEvent *event = &g_store.pendingVerkaufEvents[i];
        if (event->spielerId <= 0 || event->inFlight) continue;
        event->inFlight = true; snapshot[count++] = *event;
    }
    xSemaphoreGive(s_verkauf_events_mutex);
    return count;
}

void store_finish_verkauf_sync(const VerkaufEvent *snapshot, int count, bool delivered)
{
    if (!snapshot || count <= 0) return;
    xSemaphoreTake(s_verkauf_events_mutex, portMAX_DELAY);
    for (int n = 0; n < count; ++n) for (int i = 0; i < g_store.pendingVerkaufEventCount; ++i) {
        VerkaufEvent *event = &g_store.pendingVerkaufEvents[i];
        if (strcmp(event->externalId, snapshot[n].externalId)) continue;
        if (!delivered) event->inFlight = false;
        else {
            memmove(event, event + 1, (g_store.pendingVerkaufEventCount - i - 1) * sizeof(*event));
            memset(&g_store.pendingVerkaufEvents[--g_store.pendingVerkaufEventCount], 0, sizeof(*event));
        }
        break;
    }
    xSemaphoreGive(s_verkauf_events_mutex);
}

void store_remap_verkauf_spieler(int old_id, int new_id)
{
    if (old_id == new_id || old_id >= 0 || new_id <= 0) return;
    xSemaphoreTake(s_verkauf_events_mutex, portMAX_DELAY);
    for (int i = 0; i < g_store.pendingVerkaufEventCount; ++i)
        if (g_store.pendingVerkaufEvents[i].spielerId == old_id) g_store.pendingVerkaufEvents[i].spielerId = new_id;
    int from = -1, to = -1;
    for (int i = 0; i < MAX_PORTAL_SPIELER; ++i) {
        if (g_store.munition[i].spielerId == old_id) from = i;
        if (g_store.munition[i].spielerId == new_id) to = i;
    }
    if (from >= 0 && to < 0) {
        g_store.munition[from].spielerId = new_id;
        to = from;
    }
    if (from >= 0 && to >= 0 && from != to) {
        g_store.munition[to].cal12 += g_store.munition[from].cal12;
        g_store.munition[to].cal20 += g_store.munition[from].cal20;
        memset(&g_store.munition[from], 0, sizeof(g_store.munition[from]));
    }
    xSemaphoreGive(s_verkauf_events_mutex);
}

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

bool store_set_lineup_post(int post, int spieler_id)
{
    if (post < 1 || post > MAX_SPIELER) return false;
    int slot = post - 1;
    if (spieler_id != 0) {
        bool found = false;
        for (int i = 0; i < g_store.portalSpielerCount; ++i)
            if (g_store.portalSpieler[i].id == spieler_id) { found = true; break; }
        if (!found) return false;
    }
    if (spieler_id) for (int i = 0; i < MAX_SPIELER; ++i)
        if (i != slot && g_store.lineupIds[i] == spieler_id) g_store.lineupIds[i] = 0;
    g_store.lineupIds[slot] = spieler_id;
    g_store.lineupWarning[0] = '\0';
    game_store_save();
    return true;
}

void store_clear_lineup(void) {
    memset(g_store.lineupIds, 0, sizeof(g_store.lineupIds));
    g_store.lineupWarning[0] = '\0';
    game_store_save();
}

void store_mix_lineup(void)
{
    int ids[MAX_SPIELER], count = 0;
    for (int i = 0; i < MAX_SPIELER; ++i) if (g_store.lineupIds[i]) ids[count++] = g_store.lineupIds[i];
    // Fisher-Yates; esp_random avoids modulo bias by rejecting the short tail.
    for (int i = count - 1; i > 0; --i) {
        uint64_t range = (uint64_t)(i + 1);
        uint64_t limit = ((UINT64_C(1) << 32) / range) * range;
        uint32_t r; do { r = esp_random(); } while ((uint64_t)r >= limit);
        int j = (int)(r % (uint32_t)(i + 1));
        int tmp = ids[i]; ids[i] = ids[j]; ids[j] = tmp;
    }
    memset(g_store.lineupIds, 0, sizeof(g_store.lineupIds));
    for (int i = 0; i < count; ++i) g_store.lineupIds[i] = ids[i];
    game_store_save();
}

void store_move_lineup(int post, int direction)
{
    int target = post + direction;
    if (post < 1 || post > MAX_SPIELER || target < 1 || target > MAX_SPIELER) return;
    int tmp = g_store.lineupIds[post - 1];
    g_store.lineupIds[post - 1] = g_store.lineupIds[target - 1];
    g_store.lineupIds[target - 1] = tmp;
    game_store_save();
}

void store_remap_lineup_spieler(int old_id, int new_id)
{
    if (old_id == new_id || old_id == 0 || new_id == 0) return;
    for (int i = 0; i < MAX_SPIELER; ++i)
        if (g_store.lineupIds[i] == old_id) g_store.lineupIds[i] = new_id;
}

static void reconcile_lineup_with_roster(void)
{
    bool removed = false;
    for (int post = 0; post < MAX_SPIELER; ++post) {
        int id = g_store.lineupIds[post];
        if (!id) continue;
        bool known = false;
        for (int i = 0; i < g_store.portalSpielerCount; ++i)
            if (g_store.portalSpieler[i].id == id) { known = true; break; }
        if (!known) { g_store.lineupIds[post] = 0; removed = true; }
    }
    if (removed)
        snprintf(g_store.lineupWarning, sizeof(g_store.lineupWarning),
                 "Net existente Spiller aus Opstellung geläscht.");
}

void store_apply_portal_roster(const PortalSpieler *spieler, int count)
{
    if (!spieler || count < 0) return;
    if (count > MAX_PORTAL_SPIELER) count = MAX_PORTAL_SPIELER;
    static EXT_RAM_BSS_ATTR PortalSpieler merged[MAX_PORTAL_SPIELER];
    memcpy(merged, spieler, (size_t)count * sizeof(PortalSpieler));
    // A portal GET cannot know terminal-local creates. Keep them by their
    // stable negative ID until the create response remaps them.
    for (int old = 0; old < g_store.portalSpielerCount && count < MAX_PORTAL_SPIELER; ++old) {
        const PortalSpieler *local = &g_store.portalSpieler[old];
        if (!local->lokal) continue;
        bool pending = false;
        for (int u = 0; u < g_store.spielerUpdateCount; ++u)
            if (g_store.spielerUpdates[u].used &&
                g_store.spielerUpdates[u].typ == SPIELER_CREATE &&
                g_store.spielerUpdates[u].spielerId == local->id) pending = true;
        if (pending) merged[count++] = *local;
    }
    memset(g_store.portalSpieler, 0, sizeof(g_store.portalSpieler));
    memcpy(g_store.portalSpieler, merged, (size_t)count * sizeof(PortalSpieler));
    g_store.portalSpielerCount = count;
    reconcile_lineup_with_roster();
    game_store_save();
}

void store_remap_spieler_id(int old_id, int new_id)
{
    if (old_id == new_id || old_id == 0 || new_id == 0) return;
    for (int i = 0; i < g_store.portalSpielerCount; ++i)
        if (g_store.portalSpieler[i].id == old_id) {
            g_store.portalSpieler[i].id = new_id; g_store.portalSpieler[i].lokal = false;
        }
    for (int i = 0; i < g_store.spielerCount; ++i) if (g_store.spieler[i].id == old_id) g_store.spieler[i].id = new_id;
    for (int i = 0; i < g_store.ergebnisseCount; ++i) if (g_store.ergebnisse[i].spielerId == old_id) g_store.ergebnisse[i].spielerId = new_id;
    store_remap_lineup_spieler(old_id, new_id);
    for (int i = 0; i < MAX_PORTAL_SPIELER; ++i) {
        if (g_store.kreditPlayerIds[i] != old_id) continue;
        int target = -1;
        for (int j = 0; j < MAX_PORTAL_SPIELER; ++j) if (g_store.kreditPlayerIds[j] == new_id) target = j;
        if (target >= 0) {
            g_store.kredite[target].gewaehrt += g_store.kredite[i].gewaehrt;
            g_store.kredite[target].verbraucht += g_store.kredite[i].verbraucht;
            g_store.kreditPlayerIds[i] = 0; g_store.kredite[i] = {};
        } else g_store.kreditPlayerIds[i] = new_id;
    }
    for (int i = 0; i < MAX_PORTAL_SPIELER; ++i)
        if (g_store.munition[i].spielerId == old_id) g_store.munition[i].spielerId = new_id;
    for (int i = 0; i < g_store.pendingKreditEventCount; ++i) if (g_store.pendingKreditEvents[i].spielerId == old_id) g_store.pendingKreditEvents[i].spielerId = new_id;
    for (int i = 0; i < g_store.activeGameCreditCount; ++i) if (g_store.activeGameCreditPlayerIds[i] == old_id) g_store.activeGameCreditPlayerIds[i] = new_id;
    for (int i = 0; i < g_store.pendingVerkaufEventCount; ++i) if (g_store.pendingVerkaufEvents[i].spielerId == old_id) g_store.pendingVerkaufEvents[i].spielerId = new_id;
    for (int g = 0; g < g_store.pendingGamesCount; ++g) {
        for (int t = 0; t < g_store.pendingGames[g].teilnahmen_count; ++t) if (g_store.pendingGames[g].teilnahmen[t].spielerId == old_id) g_store.pendingGames[g].teilnahmen[t].spielerId = new_id;
        for (int r = 0; r < g_store.pendingGames[g].ergebnisse_count; ++r) if (g_store.pendingGames[g].ergebnisse[r].spielerId == old_id) g_store.pendingGames[g].ergebnisse[r].spielerId = new_id;
    }
    for (int h = 0; h < g_store.historyCount; ++h) {
        for (int p = 0; p < g_store.history[h].spieler_count; ++p) if (g_store.history[h].spielerIds[p] == old_id) g_store.history[h].spielerIds[p] = new_id;
        for (int t = 0; t < g_store.history[h].base.teilnahmen_count; ++t) if (g_store.history[h].base.teilnahmen[t].spielerId == old_id) g_store.history[h].base.teilnahmen[t].spielerId = new_id;
        for (int r = 0; r < g_store.history[h].base.ergebnisse_count; ++r) if (g_store.history[h].base.ergebnisse[r].spielerId == old_id) g_store.history[h].base.ergebnisse[r].spielerId = new_id;
    }
    if (g_store.hasLastFinished) {
        for (int p = 0; p < g_store.lastFinished.spieler_count; ++p) if (g_store.lastFinished.spielerIds[p] == old_id) g_store.lastFinished.spielerIds[p] = new_id;
        for (int t = 0; t < g_store.lastFinished.base.teilnahmen_count; ++t) if (g_store.lastFinished.base.teilnahmen[t].spielerId == old_id) g_store.lastFinished.base.teilnahmen[t].spielerId = new_id;
        for (int r = 0; r < g_store.lastFinished.base.ergebnisse_count; ++r) if (g_store.lastFinished.base.ergebnisse[r].spielerId == old_id) g_store.lastFinished.base.ergebnisse[r].spielerId = new_id;
    }
    for (int u = 0; u < g_store.spielerUpdateCount; ++u) if (g_store.spielerUpdates[u].spielerId == old_id) g_store.spielerUpdates[u].spielerId = new_id;
    // Keep the persisted daily player authorization in step with ID remaps.
    game_store_save();
}

void store_register_spieler_fuer_tag(int spieler_id)
{
    for (int i = 0; i < MAX_PORTAL_SPIELER; i++) {
        if (g_store.kreditPlayerIds[i] == spieler_id) return; // already listed
        if (g_store.kreditPlayerIds[i] == 0) {
            g_store.kreditPlayerIds[i] = spieler_id;
            g_store.kredite[i] = (KreditStand){0, 0};
            game_store_save();
            return;
        }
    }
}

void store_add_kredite(int spieler_id, int anzahl)
{
    if (anzahl > 0) (void)store_adjust_kredite(spieler_id, anzahl);
}

bool store_adjust_kredite(int spieler_id, int delta)
{
    if (spieler_id == 0 || delta == 0) return false;
    kredit_events_lock();
    for (int i = 0; i < MAX_PORTAL_SPIELER; i++) {
        if (g_store.kreditPlayerIds[i] == spieler_id) {
            // A correction is an immutable, signed GRANT event.  Never alter
            // the projection unless its matching event was reserved first.
            if (delta < 0 &&
                g_store.kredite[i].gewaehrt - g_store.kredite[i].verbraucht < -delta) {
                kredit_events_unlock();
                return false;
            }
            if (!queue_kredit_event_unlocked(spieler_id, "GRANT", delta)) {
                kredit_events_unlock();
                return false;
            }
            KreditStand prior = g_store.kredite[i];
            g_store.kredite[i].gewaehrt += delta;
            if (!save_kredit_state_unlocked()) {
                g_store.kredite[i] = prior;
                memset(&g_store.pendingKreditEvents[--g_store.pendingKreditEventCount], 0,
                       sizeof(g_store.pendingKreditEvents[0]));
                kredit_events_unlock();
                return false;
            }
            kredit_events_unlock();
            return true;
        }
        if (delta > 0 && g_store.kreditPlayerIds[i] == 0) {
            if (!queue_kredit_event_unlocked(spieler_id, "GRANT", delta)) {
                kredit_events_unlock();
                return false;
            }
            g_store.kreditPlayerIds[i] = spieler_id;
            g_store.kredite[i] = (KreditStand){delta, 0};
            if (!save_kredit_state_unlocked()) {
                g_store.kreditPlayerIds[i] = 0;
                g_store.kredite[i] = (KreditStand){};
                memset(&g_store.pendingKreditEvents[--g_store.pendingKreditEventCount], 0,
                       sizeof(g_store.pendingKreditEvents[0]));
                kredit_events_unlock();
                return false;
            }
            kredit_events_unlock();
            return true;
        }
    }
    kredit_events_unlock();
    return false;
}

// ── store_start_spiel ────────────────────────────────────────
bool store_start_spiel(void)
{
    GameStore *s = &g_store;
    if (s->operatingMode == TERMINAL_MODE_CATERING) return false;
    s->activeAcknowledgedClays = 0;
    // Snapshot the durable setup in post order. Never persist this active
    // game's points or second-run rotations back into lineupIds.
    s->spielerCount = 0;
    for (int post = 0; post < MAX_SPIELER; ++post) {
        int id = s->lineupIds[post];
        if (!id) continue;
        bool duplicate = false; int pidx = -1;
        for (int i = 0; i < s->spielerCount; ++i) if (s->spieler[i].id == id) duplicate = true;
        for (int i = 0; i < s->portalSpielerCount; ++i) if (s->portalSpieler[i].id == id) { pidx = i; break; }
        if (duplicate || pidx < 0) {
            snprintf(s->lineupWarning, sizeof(s->lineupWarning),
                     "Opstellung enthält en onbekannte Spiller.");
            return false;
        }
        Spieler *sp = &s->spieler[s->spielerCount++];
        sp->id = id; sp->startPosten = post + 1; sp->punkte = 0;
        snprintf(sp->name, sizeof(sp->name), "%s", s->portalSpieler[pidx].name);
    }
    if (s->spielerCount == 0) {
        snprintf(s->lineupWarning, sizeof(s->lineupWarning), "Mindestens 1 SPILLER auswielen!");
        return false;
    }

    // Check all players have credits
    for (int i = 0; i < s->spielerCount; i++) {
        if (store_kredite_verfuegbar(s->spieler[i].id) <= 0) {
            ESP_LOGW(TAG, "Player %d has no credits", s->spieler[i].id);
            snprintf(s->lineupWarning, sizeof(s->lineupWarning),
                     "%s huet keng Kreditter.", s->spieler[i].name);
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
    s->lineupWarning[0] = '\0';
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
    s->activeAcknowledgedClays = 0;
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

void store_account_acknowledged_clays(int count)
{
    // Called only by the gateway worker after an actual FIRE ACK. It is
    // deliberately independent of scoring so skipped/rejected launches never
    // become clay consumption.
    if (count <= 0 || g_store.screen != SCREEN_SPIEL) return;
    g_store.activeAcknowledgedClays += count;
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
    fg.base.confirmedLaunches = s->activeAcknowledgedClays;
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
    s->activeAcknowledgedClays = 0;
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
    if (g_store.operatingMode == TERMINAL_MODE_CATERING && s != SCREEN_CATERING) {
        g_store.screen = SCREEN_CATERING;
        return;
    }
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
static portMUX_TYPE s_sync_request_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_boot_sync_requested = false;
static bool s_boot_sync_consumed = false;
static bool s_sync_pending = false;
static TickType_t s_next_auto_sync;

static void queue_boot_sync_if_pending(void);

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
                    store_apply_portal_roster(buf, count);
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
    QueueHandle_t sync_queue = xQueueCreate(1, sizeof(uint32_t));
    if (!sync_queue) {
        ESP_LOGE(TAG, "Sync worker queue allocation failed");
        g_store.syncStatus = SYNC_ERROR;
        snprintf(g_store.syncError, sizeof(g_store.syncError),
                 "Sync worker unavailable");
        return;
    }

    // Start the worker with its private queue handle first. Publishing the
    // handle only after task creation succeeds prevents callers from
    // accepting a request that no worker can ever consume.
    BaseType_t task_created = xTaskCreateWithCaps([](void *arg) {
        QueueHandle_t queue = (QueueHandle_t)arg;
        uint32_t dummy;
        for (;;) {
            if (xQueueReceive(queue, &dummy, pdMS_TO_TICKS(1000)) != pdTRUE) {
                bool due = g_store.autoSyncEnabled &&
                           (int32_t)(xTaskGetTickCount() - s_next_auto_sync) >= 0;
                if (due && cop_wifi_is_connected() &&
                    !store_sync_is_queued_or_running()) {
                    store_sync();
                }
                continue;
            }
            g_store.syncStatus = SYNC_RUNNING;
            esp_err_t err = http_sync_all();
            g_store.syncStatus = (err == ESP_OK) ? SYNC_SUCCESS : SYNC_ERROR;
            if (err != ESP_OK) {
                http_sync_copy_last_error(g_store.syncError,
                                          sizeof(g_store.syncError));
                if (!g_store.syncError[0])
                    snprintf(g_store.syncError, sizeof(g_store.syncError),
                             "HTTP sync failed: %s", esp_err_to_name(err));
            } else {
                g_store.syncError[0] = '\0';
            }
            portENTER_CRITICAL(&s_sync_request_lock);
            s_sync_pending = false;
            s_next_auto_sync = xTaskGetTickCount() +
                pdMS_TO_TICKS(g_store.autoSyncSeconds * 1000u);
            portEXIT_CRITICAL(&s_sync_request_lock);
            queue_boot_sync_if_pending();
        }
    }, "sync_w", 16384, sync_queue, 5, NULL,
       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (task_created != pdPASS) {
        vQueueDelete(sync_queue);
        ESP_LOGE(TAG, "Sync worker task creation failed");
        g_store.syncStatus = SYNC_ERROR;
        snprintf(g_store.syncError, sizeof(g_store.syncError),
                 "Sync worker unavailable");
        return;
    }

    portENTER_CRITICAL(&s_sync_request_lock);
    s_sync_queue = sync_queue;
    s_next_auto_sync = xTaskGetTickCount() +
        pdMS_TO_TICKS(g_store.autoSyncSeconds * 1000u);
    portEXIT_CRITICAL(&s_sync_request_lock);

    ESP_LOGI(TAG, "Store workers created. Internal RAM remaining: %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    queue_boot_sync_if_pending();
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
    if (count > 0 && !save_kredit_state_unlocked()) {
        // Do not send a snapshot whose reservation was not made durable.
        // Retrying will retain the same event IDs and is therefore idempotent.
        for (int i = 0; i < count; ++i) {
            int eventIndex = find_pending_kredit_event(&g_store, snapshot[i].externalId);
            if (eventIndex >= 0) g_store.pendingKreditEvents[eventIndex].inFlight = false;
        }
        count = 0;
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
        // An acknowledgement may only settle the specific persisted snapshot,
        // never an event appended while the HTTP request was in progress.
        if (!g_store.pendingKreditEvents[eventIndex].inFlight) continue;
        if (delivered) {
            remove_pending_kredit_event(&g_store, eventIndex);
        } else {
            g_store.pendingKreditEvents[eventIndex].inFlight = false;
        }
    }
    // The helper reports NVS failures and leaves the committed prior state
    // intact for a safe idempotent retry after a reboot.
    (void)save_kredit_state_unlocked();
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
bool store_sync(void)
{
    // Just trigger the persistent worker — costs no RAM at call time.
    QueueHandle_t sync_queue = NULL;
    portENTER_CRITICAL(&s_sync_request_lock);
    sync_queue = s_sync_queue;
    portEXIT_CRITICAL(&s_sync_request_lock);
    if (!sync_queue) {
        g_store.syncStatus = SYNC_ERROR;
        snprintf(g_store.syncError, sizeof(g_store.syncError),
                 "Sync worker unavailable");
        return false;
    }

    portENTER_CRITICAL(&s_sync_request_lock);
    if (s_sync_pending) {
        portEXIT_CRITICAL(&s_sync_request_lock);
        snprintf(g_store.syncError, sizeof(g_store.syncError),
                 "Sync request already queued or running");
        return false;
    }
    s_sync_pending = true;
    portEXIT_CRITICAL(&s_sync_request_lock);

    g_store.syncStatus = SYNC_RUNNING;
    uint32_t trigger = 1;
    if (xQueueSend(sync_queue, &trigger, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_sync_request_lock);
        s_sync_pending = false;
        portEXIT_CRITICAL(&s_sync_request_lock);
        g_store.syncStatus = SYNC_ERROR;
        snprintf(g_store.syncError, sizeof(g_store.syncError),
                 "Sync request already queued");
        return false;
    }
    return true;
}

bool store_sync_is_queued_or_running(void)
{
    portENTER_CRITICAL(&s_sync_request_lock);
    bool pending = s_sync_pending;
    portEXIT_CRITICAL(&s_sync_request_lock);
    return pending;
}

bool store_set_auto_sync(bool enabled, uint32_t seconds)
{
    if (seconds < AUTO_SYNC_MIN_SECONDS || seconds > AUTO_SYNC_MAX_SECONDS)
        return false;
    g_store.autoSyncEnabled = enabled;
    g_store.autoSyncSeconds = seconds;
    portENTER_CRITICAL(&s_sync_request_lock);
    s_next_auto_sync = xTaskGetTickCount() + pdMS_TO_TICKS(seconds * 1000u);
    portEXIT_CRITICAL(&s_sync_request_lock);
    game_store_save();
    return true;
}

void store_sync_after_boot_wifi_connected(void)
{
    portENTER_CRITICAL(&s_sync_request_lock);
    // This is deliberately one-shot per boot. A connection flap before the
    // initial request is accepted can retry queueing; once accepted, normal
    // scheduler/manual paths handle all later syncs.
    if (!s_boot_sync_consumed)
        s_boot_sync_requested = true;
    portEXIT_CRITICAL(&s_sync_request_lock);

    queue_boot_sync_if_pending();
}

static void queue_boot_sync_if_pending(void)
{
    bool queue_sync_now = false;
    portENTER_CRITICAL(&s_sync_request_lock);
    if (s_boot_sync_requested && !s_sync_pending && s_sync_queue) {
        // Consume the request before leaving the lock. If queueing fails it
        // is restored below; success/failure of the HTTP operation never
        // leaves a permanent boot latch behind.
        s_boot_sync_requested = false;
        s_boot_sync_consumed = true;
        queue_sync_now = true;
    }
    portEXIT_CRITICAL(&s_sync_request_lock);

    if (queue_sync_now && !store_sync()) {
        portENTER_CRITICAL(&s_sync_request_lock);
        s_boot_sync_requested = true;
        s_boot_sync_consumed = false;
        portEXIT_CRITICAL(&s_sync_request_lock);
        ESP_LOGE(TAG, "Initial boot sync could not be queued");
    }
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
    nvs_set_i32(s_nvs, "op_mode", (int32_t)g_store.operatingMode);
    nvs_set_i32(s_nvs, "cat_pin_set", g_store.cateringPinConfigured ? 1 : 0);
    nvs_set_blob(s_nvs, "cat_pin_salt", g_store.cateringPinSalt, sizeof(g_store.cateringPinSalt));
    nvs_set_blob(s_nvs, "cat_pin_hash", g_store.cateringPinHash, sizeof(g_store.cateringPinHash));
    nvs_set_i32(s_nvs, "cat_pin_fail", g_store.cateringPinFailures);
    nvs_set_i64(s_nvs, "cat_pin_lock", g_store.cateringPinLockoutUntil);
    nvs_set_i32(s_nvs, "click_snd", g_store.clickSoundEnabled ? 1 : 0);
    nvs_set_i32(s_nvs, "auto_sync", g_store.autoSyncEnabled ? 1 : 0);
    nvs_set_u32(s_nvs, "auto_secs", g_store.autoSyncSeconds);
    nvs_set_str(s_nvs, "cfg_backup_at", g_store.lastConfigBackupAt);
    nvs_set_str(s_nvs, "cfg_backup_st", g_store.configBackupStatus);
    nvs_set_blob(s_nvs, "custom_seq", g_store.customSequenzen,
                 sizeof(g_store.customSequenzen));
    nvs_set_blob(s_nvs, "custom_len", g_store.customSequenzLen,
                 sizeof(g_store.customSequenzLen));
    nvs_set_blob(s_nvs, "custom_run", g_store.customLaeufe,
                 sizeof(g_store.customLaeufe));
    nvs_set_blob(s_nvs, "products", g_store.produkte, sizeof(g_store.produkte));
    nvs_set_i32(s_nvs, "prod_count", g_store.produkteCount);
    nvs_set_blob(s_nvs, "sales", g_store.pendingVerkaufEvents, sizeof(g_store.pendingVerkaufEvents));
    nvs_set_i32(s_nvs, "sale_count", g_store.pendingVerkaufEventCount);
    nvs_set_blob(s_nvs, "ammo", g_store.munition, sizeof(g_store.munition));
    nvs_set_str(s_nvs, "sale_date", g_store.verkaufDatum);
    nvs_set_i32(s_nvs, "sale_12", g_store.verkaufCal12Total);
    nvs_set_i32(s_nvs, "sale_20", g_store.verkaufCal20Total);
    nvs_set_blob(s_nvs, "payments", g_store.pendingPaymentEvents,
                 sizeof(g_store.pendingPaymentEvents));
    nvs_set_i32(s_nvs, "payment_cnt", g_store.pendingPaymentEventCount);
    nvs_set_blob(s_nvs, "lineup_ids", g_store.lineupIds, sizeof(g_store.lineupIds));
    nvs_set_str(s_nvs, "credit_date", g_store.kreditDatum);
    nvs_set_blob(s_nvs, "credit_ids", g_store.kreditPlayerIds, sizeof(g_store.kreditPlayerIds));
    nvs_set_blob(s_nvs, "credits", g_store.kredite, sizeof(g_store.kredite));
    nvs_set_blob(s_nvs, "credit_events", g_store.pendingKreditEvents,
                 sizeof(g_store.pendingKreditEvents));
    nvs_set_i32(s_nvs, "credit_evt_cnt", g_store.pendingKreditEventCount);
    // Cache roster and unsynced creates too: an offline reboot must still be
    // able to render the saved setup and later complete its create sync.
    nvs_set_blob(s_nvs, "plrs", g_store.portalSpieler, sizeof(g_store.portalSpieler));
    nvs_set_i32(s_nvs, "plr_cnt", g_store.portalSpielerCount);
    nvs_set_blob(s_nvs, "sp_updates", g_store.spielerUpdates, sizeof(g_store.spielerUpdates));
    nvs_set_i32(s_nvs, "sp_up_cnt", g_store.spielerUpdateCount);
    nvs_commit(s_nvs);
}

// ── Init ─────────────────────────────────────────────────────
void game_store_init(void)
{
    memset(&g_store, 0, sizeof(g_store));
    s_kredit_events_mutex = xSemaphoreCreateMutex();
    configASSERT(s_kredit_events_mutex);
    s_verkauf_events_mutex = xSemaphoreCreateMutex();
    configASSERT(s_verkauf_events_mutex);
    nvs_open();

    // Defaults
    snprintf(g_store.apiUrl, MAX_URL_LEN, "%s", DEFAULT_API_URL);
    snprintf(g_store.apiKey, MAX_KEY_LEN, "%s", DEFAULT_API_KEY);
    for (int m = 0; m < MASCHINE_COUNT; m++) g_store.maschinenAktiv[m] = true;
    g_store.customLaeufe[0] = g_store.customLaeufe[1] =
    g_store.customLaeufe[2] = g_store.customLaeufe[3] = 2;
    set_default_custom_sequences();
    g_store.screen = SCREEN_DASHBOARD;
    g_store.autoSyncEnabled = true;
    g_store.autoSyncSeconds = AUTO_SYNC_DEFAULT_SECONDS;
    time_t credit_now = time(NULL); struct tm credit_tm; localtime_r(&credit_now, &credit_tm);
    strftime(g_store.kreditDatum, sizeof(g_store.kreditDatum), "%Y-%m-%d", &credit_tm);

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
    nvs_load_str("cfg_backup_at", g_store.lastConfigBackupAt,
                 sizeof(g_store.lastConfigBackupAt));
    nvs_load_str("cfg_backup_st", g_store.configBackupStatus,
                 sizeof(g_store.configBackupStatus));
    size_t credit_ids_size = sizeof(g_store.kreditPlayerIds);
    size_t credits_size = sizeof(g_store.kredite);
    char saved_credit_date[11] = {};
    nvs_load_str("credit_date", saved_credit_date, sizeof(saved_credit_date));
    if (strcmp(saved_credit_date, g_store.kreditDatum) == 0 &&
        nvs_get_blob(s_nvs, "credit_ids", g_store.kreditPlayerIds, &credit_ids_size) == ESP_OK &&
        credit_ids_size == sizeof(g_store.kreditPlayerIds) &&
        nvs_get_blob(s_nvs, "credits", g_store.kredite, &credits_size) == ESP_OK &&
        credits_size == sizeof(g_store.kredite)) {
        // Current day only; otherwise the zero defaults are deliberately retained.
    } else {
        memset(g_store.kreditPlayerIds, 0, sizeof(g_store.kreditPlayerIds));
        memset(g_store.kredite, 0, sizeof(g_store.kredite));
    }
    size_t credit_events_size = sizeof(g_store.pendingKreditEvents);
    int32_t credit_event_count = 0;
    if (nvs_get_blob(s_nvs, "credit_events", g_store.pendingKreditEvents,
                     &credit_events_size) == ESP_OK &&
        credit_events_size == sizeof(g_store.pendingKreditEvents) &&
        nvs_get_i32(s_nvs, "credit_evt_cnt", &credit_event_count) == ESP_OK &&
        credit_event_count >= 0 && credit_event_count <= MAX_KREDIT_EVENTS) {
        bool valid = true;
        for (int i = 0; i < credit_event_count; ++i) {
            const KreditEvent *event = &g_store.pendingKreditEvents[i];
            if (event->spielerId == 0 || event->anzahl == 0 ||
                !memchr(event->externalId, '\0', sizeof(event->externalId)) ||
                (strcmp(event->typ, "GRANT") != 0 && strcmp(event->typ, "USE") != 0)) {
                valid = false;
                break;
            }
        }
        if (valid) g_store.pendingKreditEventCount = credit_event_count;
        else memset(g_store.pendingKreditEvents, 0, sizeof(g_store.pendingKreditEvents));
    } else {
        memset(g_store.pendingKreditEvents, 0, sizeof(g_store.pendingKreditEvents));
    }
    // An interrupted POST may have been accepted by the portal. Retry its
    // stable external ID rather than leaving the outbox permanently in flight.
    bool reset_credit_flights = false;
    for (int i = 0; i < g_store.pendingKreditEventCount; ++i) {
        reset_credit_flights |= g_store.pendingKreditEvents[i].inFlight;
        g_store.pendingKreditEvents[i].inFlight = false;
    }
    if (reset_credit_flights) {
        kredit_events_lock();
        (void)save_kredit_state_unlocked();
        kredit_events_unlock();
    }
    size_t lineup_size = sizeof(g_store.lineupIds);
    if (nvs_get_blob(s_nvs, "lineup_ids", g_store.lineupIds, &lineup_size) != ESP_OK ||
        lineup_size != sizeof(g_store.lineupIds))
        memset(g_store.lineupIds, 0, sizeof(g_store.lineupIds));
    size_t portal_size = sizeof(g_store.portalSpieler);
    size_t update_size = sizeof(g_store.spielerUpdates);
    int32_t portal_count = 0, update_count = 0;
    if (nvs_get_blob(s_nvs, "plrs", g_store.portalSpieler, &portal_size) != ESP_OK ||
        portal_size != sizeof(g_store.portalSpieler) ||
        nvs_get_i32(s_nvs, "plr_cnt", &portal_count) != ESP_OK ||
        portal_count < 0 || portal_count > MAX_PORTAL_SPIELER) {
        memset(g_store.portalSpieler, 0, sizeof(g_store.portalSpieler));
    } else g_store.portalSpielerCount = portal_count;
    if (nvs_get_blob(s_nvs, "sp_updates", g_store.spielerUpdates, &update_size) == ESP_OK &&
        update_size == sizeof(g_store.spielerUpdates) &&
        nvs_get_i32(s_nvs, "sp_up_cnt", &update_count) == ESP_OK &&
        update_count >= 0 && update_count <= MAX_SPIELER_UPDATES)
        g_store.spielerUpdateCount = update_count;
    reconcile_lineup_with_roster();
    if (g_store.lineupWarning[0]) {
        nvs_set_blob(s_nvs, "lineup_ids", g_store.lineupIds, sizeof(g_store.lineupIds));
        nvs_commit(s_nvs);
    }

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
    size_t products_size = sizeof(g_store.produkte), sales_size = sizeof(g_store.pendingVerkaufEvents);
    size_t ammo_size = sizeof(g_store.munition); int32_t product_count = 0, sale_count = 0;
    if (nvs_get_blob(s_nvs, "products", g_store.produkte, &products_size) == ESP_OK &&
        products_size == sizeof(g_store.produkte) &&
        nvs_get_i32(s_nvs, "prod_count", &product_count) == ESP_OK &&
        product_count >= 0 && product_count <= MAX_PRODUKTE) g_store.produkteCount = product_count;
    if (nvs_get_blob(s_nvs, "sales", g_store.pendingVerkaufEvents, &sales_size) == ESP_OK &&
        sales_size == sizeof(g_store.pendingVerkaufEvents) &&
        nvs_get_i32(s_nvs, "sale_count", &sale_count) == ESP_OK &&
        sale_count >= 0 && sale_count <= MAX_PENDING_VERKAEUFE) g_store.pendingVerkaufEventCount = sale_count;
    nvs_get_blob(s_nvs, "ammo", g_store.munition, &ammo_size);
    nvs_load_str("sale_date", g_store.verkaufDatum, sizeof(g_store.verkaufDatum));
    nvs_get_i32(s_nvs, "sale_12", &g_store.verkaufCal12Total);
    nvs_get_i32(s_nvs, "sale_20", &g_store.verkaufCal20Total);
    size_t payments_size = sizeof(g_store.pendingPaymentEvents);
    int32_t payment_count = 0;
    if (nvs_get_blob(s_nvs, "payments", g_store.pendingPaymentEvents, &payments_size) == ESP_OK &&
        payments_size == sizeof(g_store.pendingPaymentEvents) &&
        nvs_get_i32(s_nvs, "payment_cnt", &payment_count) == ESP_OK &&
        payment_count >= 0 && payment_count <= MAX_PENDING_PAYMENTS)
        g_store.pendingPaymentEventCount = payment_count;
    // A power loss can occur after inFlight was committed but before an HTTP
    // response reached us. Reset it at boot and retry its stable externalId.
    bool reset_payment_flights = false;
    for (int i = 0; i < g_store.pendingPaymentEventCount; ++i) {
        reset_payment_flights |= g_store.pendingPaymentEvents[i].inFlight;
        g_store.pendingPaymentEvents[i].inFlight = false;
    }
    if (reset_payment_flights) {
        nvs_set_blob(s_nvs, "payments", g_store.pendingPaymentEvents,
                     sizeof(g_store.pendingPaymentEvents));
        nvs_commit(s_nvs);
    }
    size_t bill_day_size = sizeof(g_store.billDay);
    if (nvs_get_blob(s_nvs, "bill_day", &g_store.billDay, &bill_day_size) != ESP_OK ||
        bill_day_size != sizeof(g_store.billDay) ||
        g_store.billDay.playerCount < 0 || g_store.billDay.playerCount > MAX_DAY_BILLS ||
        g_store.billDay.categoryCount < 0 ||
        g_store.billDay.categoryCount > MAX_BILL_CATEGORIES ||
        g_store.billDay.productCount < 0 ||
        g_store.billDay.productCount > MAX_DAY_PRODUCTS) {
        memset(&g_store.billDay, 0, sizeof(g_store.billDay));
    } else {
        bool nested_valid = true;
        for (int i = 0; i < g_store.billDay.playerCount; ++i)
            if (g_store.billDay.players[i].lineCount < 0 ||
                g_store.billDay.players[i].lineCount > MAX_BILL_LINES ||
                g_store.billDay.players[i].categoryCount < 0 ||
                g_store.billDay.players[i].categoryCount > MAX_BILL_CATEGORIES)
                nested_valid = false;
        if (!nested_valid) memset(&g_store.billDay, 0, sizeof(g_store.billDay));
        else g_store.billDay.authoritative = false; // valid portal snapshot, now offline cache
    }
    time_t today_now = time(NULL); struct tm today_tm; localtime_r(&today_now, &today_tm);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &today_tm);
    if (strcmp(g_store.verkaufDatum, today) != 0) {
        memset(g_store.munition, 0, sizeof(g_store.munition));
        g_store.verkaufCal12Total = g_store.verkaufCal20Total = 0;
        strncpy(g_store.verkaufDatum, today, sizeof(g_store.verkaufDatum) - 1);
    }

    int32_t modus = 0;
    if (nvs_get_i32(s_nvs, "modus", &modus) == ESP_OK)
        g_store.modus = (Modus)modus;
    // PIN material is independent of active mode. A terminal may have a PIN
    // configured for a future Catering session while booting in normal mode.
    int32_t operating_mode = TERMINAL_MODE_NORMAL, pin_set = 0;
    size_t pin_salt_size = sizeof(g_store.cateringPinSalt);
    size_t pin_hash_size = sizeof(g_store.cateringPinHash);
    bool valid_pin = nvs_get_i32(s_nvs, "cat_pin_set", &pin_set) == ESP_OK && pin_set == 1 &&
        nvs_get_blob(s_nvs, "cat_pin_salt", g_store.cateringPinSalt, &pin_salt_size) == ESP_OK &&
        pin_salt_size == sizeof(g_store.cateringPinSalt) &&
        nvs_get_blob(s_nvs, "cat_pin_hash", g_store.cateringPinHash, &pin_hash_size) == ESP_OK &&
        pin_hash_size == sizeof(g_store.cateringPinHash);
    if (valid_pin) g_store.cateringPinConfigured = true;
    if (valid_pin) {
        int32_t failures = 0;
        if (nvs_get_i32(s_nvs, "cat_pin_fail", &failures) == ESP_OK &&
            failures >= 0 && failures <= UINT8_MAX) g_store.cateringPinFailures = (uint8_t)failures;
        nvs_get_i64(s_nvs, "cat_pin_lock", &g_store.cateringPinLockoutUntil);
        time_t now = time(NULL);
        if ((int64_t)now < VALID_UNIX_TIME ||
            g_store.cateringPinLockoutUntil <= (int64_t)now)
            g_store.cateringPinLockoutUntil = 0;
    }
    // An invalid/unknown mode or an incomplete PIN state is intentionally
    // normal mode; never enter a locked-down operational state accidentally.
    if (nvs_get_i32(s_nvs, "op_mode", &operating_mode) == ESP_OK &&
        operating_mode == TERMINAL_MODE_CATERING && valid_pin) {
        g_store.cateringPinConfigured = true;
        g_store.operatingMode = TERMINAL_MODE_CATERING;
        g_store.screen = SCREEN_CATERING;
    }

    int32_t click_snd = 1;  // default ON
    if (nvs_get_i32(s_nvs, "click_snd", &click_snd) == ESP_OK)
        g_store.clickSoundEnabled = (click_snd != 0);
    else
        g_store.clickSoundEnabled = true;  // first boot — enable by default

    int32_t auto_sync = 1;
    uint32_t auto_secs = AUTO_SYNC_DEFAULT_SECONDS;
    if (nvs_get_i32(s_nvs, "auto_sync", &auto_sync) == ESP_OK)
        g_store.autoSyncEnabled = auto_sync != 0;
    if (nvs_get_u32(s_nvs, "auto_secs", &auto_secs) == ESP_OK &&
        auto_secs >= AUTO_SYNC_MIN_SECONDS &&
        auto_secs <= AUTO_SYNC_MAX_SECONDS)
        g_store.autoSyncSeconds = auto_secs;

    ESP_LOGI(TAG, "Store initialised. API: %s modus: %s",
             g_store.apiUrl, modus_label(g_store.modus));
}
