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
#include "offline_cache.h"

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
static SemaphoreHandle_t s_nvs_mutex;
static SemaphoreHandle_t s_kredit_events_mutex;
static SemaphoreHandle_t s_verkauf_events_mutex;
static const char *s_catering_error = "";

#define CATERING_PIN_MIN_LEN 4
#define CATERING_PIN_MAX_LEN 16
#define CATERING_PIN_ITERATIONS 120000
#define CATERING_PIN_MAX_FAILURES 5
#define CATERING_PIN_LOCK_SECONDS 30
#define VALID_UNIX_TIME 1704067200LL /* 2024-01-01 */
#define NVS_LAYOUT_VERSION 4

// Layout used by released firmware before VerkaufEvent gained local receipt
// snapshots. Keep this private wire-format mirror so old compact and
// maximum-capacity NVS blobs remain recoverable after the struct grows.
typedef struct {
    char externalId[40];
    int  spielerId;
    char datum[11];
    int  produktId;
    int  preisRevisionId;
    int  quantity;
    bool inFlight;
} LegacyVerkaufEvent;

// KreditEvent before billable USE events gained their immutable receipt data.
// Keep this mirror so an upgrade never drops an offline credit outbox.
typedef struct {
    char externalId[40];
    int  spielerId;
    char datum[11];
    char typ[8];
    int  anzahl;
    bool inFlight;
} LegacyKreditEvent;

static void kredit_events_lock(void)
{
    configASSERT(s_kredit_events_mutex);
    xSemaphoreTake(s_kredit_events_mutex, portMAX_DELAY);
}

static void kredit_events_unlock(void)
{
    xSemaphoreGive(s_kredit_events_mutex);
}

static void nvs_lock(void)
{
    configASSERT(s_nvs_mutex);
    xSemaphoreTake(s_nvs_mutex, portMAX_DELAY);
}

static void nvs_unlock(void)
{
    xSemaphoreGive(s_nvs_mutex);
}

static void nvs_reopen_after_failure(void)
{
    nvs_close(s_nvs);
    s_nvs = 0;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs));
}

static bool queue_kredit_event_unlocked(int spieler_id, const char *typ, int anzahl);
static bool save_kredit_state_unlocked(void);
static esp_err_t set_counted_blob(const char *key, const void *data,
                                  int count, size_t item_size);

// Payments and the active-day roster are one state transition: a portal
// acceptance must never survive locally without retiring the player, nor may a
// player be retired before the durable acceptance transition commits.
static bool save_payment_state_unlocked(void)
{
    nvs_lock();
    esp_err_t err = set_counted_blob("payments", g_store.pendingPaymentEvents,
                                     g_store.pendingPaymentEventCount,
                                     sizeof(g_store.pendingPaymentEvents[0]));
    if (err == ESP_OK)
        err = nvs_set_i32(s_nvs, "payment_cnt", g_store.pendingPaymentEventCount);
    if (err == ESP_OK)
        err = nvs_set_blob(s_nvs, "credit_ids", g_store.kreditPlayerIds,
                           sizeof(g_store.kreditPlayerIds));
    if (err == ESP_OK)
        err = nvs_set_blob(s_nvs, "credits", g_store.kredite, sizeof(g_store.kredite));
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not persist payment state: %s", esp_err_to_name(err));
        nvs_reopen_after_failure();
    }
    nvs_unlock();
    return err == ESP_OK;
}

static bool load_sales_blob_compat(void)
{
    int32_t saved_count = 0;
    if (nvs_get_i32(s_nvs, "sale_count", &saved_count) != ESP_OK ||
        saved_count < 0 || saved_count > MAX_PENDING_VERKAEUFE) return false;
    if (saved_count == 0) {
        g_store.pendingVerkaufEventCount = 0;
        return true;
    }
    size_t bytes = 0;
    if (nvs_get_blob(s_nvs, "sales", NULL, &bytes) != ESP_OK) return false;
    const size_t current_compact = (size_t)saved_count * sizeof(VerkaufEvent);
    const size_t current_full = sizeof(g_store.pendingVerkaufEvents);
    if (bytes == current_compact || bytes == current_full) {
        size_t capacity = current_full;
        if (nvs_get_blob(s_nvs, "sales", g_store.pendingVerkaufEvents, &capacity) != ESP_OK)
            return false;
        g_store.pendingVerkaufEventCount = saved_count;
        return true;
    }
    const size_t legacy_compact = (size_t)saved_count * sizeof(LegacyVerkaufEvent);
    const size_t legacy_full = (size_t)MAX_PENDING_VERKAEUFE * sizeof(LegacyVerkaufEvent);
    if (bytes != legacy_compact && bytes != legacy_full) return false;
    static EXT_RAM_BSS_ATTR LegacyVerkaufEvent legacy[MAX_PENDING_VERKAEUFE];
    size_t capacity = sizeof(legacy);
    if (nvs_get_blob(s_nvs, "sales", legacy, &capacity) != ESP_OK) return false;
    memset(g_store.pendingVerkaufEvents, 0, sizeof(g_store.pendingVerkaufEvents));
    for (int i = 0; i < saved_count; ++i) {
        VerkaufEvent *out = &g_store.pendingVerkaufEvents[i];
        const LegacyVerkaufEvent *old = &legacy[i];
        memcpy(out->externalId, old->externalId, sizeof(out->externalId));
        out->spielerId = old->spielerId;
        memcpy(out->datum, old->datum, sizeof(out->datum));
        out->produktId = old->produktId;
        out->preisRevisionId = old->preisRevisionId;
        out->quantity = old->quantity;
        out->inFlight = old->inFlight;
        // Old records have no trustworthy receipt data. Enrich only from an
        // exact cached revision; otherwise retain an explicit unknown line.
        out->unitPriceCent = VERKAUF_UNIT_PRICE_UNKNOWN;
        const Produkt *product = NULL;
        for (int p = 0; out->preisRevisionId > VERKAUF_PRICE_REVISION_UNALLOCATED &&
                        p < g_store.produkteCount; ++p)
            if (g_store.produkte[p].id == out->produktId &&
                g_store.produkte[p].preisRevisionId == out->preisRevisionId) {
                product = &g_store.produkte[p]; break;
            }
        if (product) {
            snprintf(out->produktName, sizeof(out->produktName), "%s", product->name);
            snprintf(out->category, sizeof(out->category), "%s", product->category);
            out->unitPriceCent = product->preisCent;
        } else {
            snprintf(out->produktName, sizeof(out->produktName), "ONBEKANNT");
            snprintf(out->category, sizeof(out->category), "ONBEKANNT");
        }
    }
    g_store.pendingVerkaufEventCount = saved_count;
    ESP_LOGW(TAG, "Migrated %d legacy sale records without receipt snapshots", (int)saved_count);
    return true;
}

static esp_err_t set_counted_blob(const char *key, const void *data,
                                  int count, size_t item_size)
{
    if (count == 0) {
        esp_err_t err = nvs_erase_key(s_nvs, key);
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
    }
    return nvs_set_blob(s_nvs, key, data, (size_t)count * item_size);
}

static bool load_counted_blob_compat(const char *key, const char *count_key,
                                     void *data, int capacity, size_t item_size,
                                     int *count)
{
    int32_t saved_count = 0;
    if (nvs_get_i32(s_nvs, count_key, &saved_count) != ESP_OK ||
        saved_count < 0 || saved_count > capacity)
        return false;
    if (saved_count == 0) {
        *count = 0;
        return true;
    }
    size_t size = (size_t)capacity * item_size;
    if (nvs_get_blob(s_nvs, key, data, &size) != ESP_OK ||
        (size != (size_t)saved_count * item_size &&
         size != (size_t)capacity * item_size))
        return false;
    *count = saved_count;
    return true;
}

static esp_err_t set_credit_events_blob_unlocked(void)
{
    return set_counted_blob("credit_events", g_store.pendingKreditEvents,
                            g_store.pendingKreditEventCount,
                            sizeof(g_store.pendingKreditEvents[0]));
}

static esp_err_t set_ammo_blob_unlocked(void)
{
    MunitionStand compact[MAX_PORTAL_SPIELER];
    int count = 0;
    for (int i = 0; i < MAX_PORTAL_SPIELER; ++i) {
        if (g_store.munition[i].spielerId <= 0) continue;
        if (g_store.munition[i].cal12 == 0 && g_store.munition[i].cal20 == 0)
            continue;
        compact[count++] = g_store.munition[i];
    }
    esp_err_t err = set_counted_blob("ammo", compact, count, sizeof(compact[0]));
    if (err == ESP_OK) err = nvs_set_i32(s_nvs, "ammo_count", count);
    return err;
}

static esp_err_t save_sale_state_unlocked(void)
{
    nvs_lock();
    esp_err_t err = set_counted_blob("sales", g_store.pendingVerkaufEvents,
                                     g_store.pendingVerkaufEventCount,
                                     sizeof(g_store.pendingVerkaufEvents[0]));
    if (err == ESP_OK)
        err = nvs_set_i32(s_nvs, "sale_count", g_store.pendingVerkaufEventCount);
    if (err == ESP_OK) err = set_ammo_blob_unlocked();
    if (err == ESP_OK)
        err = nvs_set_str(s_nvs, "sale_date", g_store.verkaufDatum);
    if (err == ESP_OK)
        err = nvs_set_i32(s_nvs, "sale_12", g_store.verkaufCal12Total);
    if (err == ESP_OK)
        err = nvs_set_i32(s_nvs, "sale_20", g_store.verkaufCal20Total);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not persist sale state: %s", esp_err_to_name(err));
        nvs_reopen_after_failure();
    }
    nvs_unlock();
    return err;
}

static esp_err_t save_catering_state_unlocked(bool persist_daily_reset)
{
    nvs_lock();
    esp_err_t err = set_counted_blob("sales", g_store.pendingVerkaufEvents,
                                     g_store.pendingVerkaufEventCount,
                                     sizeof(g_store.pendingVerkaufEvents[0]));
    if (err == ESP_OK)
        err = nvs_set_i32(s_nvs, "sale_count", g_store.pendingVerkaufEventCount);
    if (err == ESP_OK)
        err = nvs_set_str(s_nvs, "sale_date", g_store.verkaufDatum);
    if (err == ESP_OK && persist_daily_reset)
        err = set_ammo_blob_unlocked();
    if (err == ESP_OK && persist_daily_reset)
        err = nvs_set_i32(s_nvs, "sale_12", g_store.verkaufCal12Total);
    if (err == ESP_OK && persist_daily_reset)
        err = nvs_set_i32(s_nvs, "sale_20", g_store.verkaufCal20Total);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not persist Catering sale: %s", esp_err_to_name(err));
        nvs_reopen_after_failure();
    }
    nvs_unlock();
    return err;
}

// This is deliberately a single NVS transaction: the projected balance must
// never be made durable without the immutable event which explains it.
static bool save_kredit_state_unlocked(void)
{
    nvs_lock();
    esp_err_t err = nvs_set_str(s_nvs, "credit_date", g_store.kreditDatum);
    if (err == ESP_OK)
        err = nvs_set_blob(s_nvs, "credit_ids", g_store.kreditPlayerIds,
                           sizeof(g_store.kreditPlayerIds));
    if (err == ESP_OK)
        err = nvs_set_blob(s_nvs, "credits", g_store.kredite, sizeof(g_store.kredite));
    if (err == ESP_OK) err = set_credit_events_blob_unlocked();
    if (err == ESP_OK)
        err = nvs_set_i32(s_nvs, "credit_evt_cnt", g_store.pendingKreditEventCount);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not persist credit state: %s", esp_err_to_name(err));
        nvs_reopen_after_failure();
    }
    nvs_unlock();
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

static void snapshot_sale_product(VerkaufEvent *event, const Produkt *product)
{
    configASSERT(event && product);
    snprintf(event->produktName, sizeof(event->produktName), "%s", product->name);
    snprintf(event->category, sizeof(event->category), "%s", product->category);
    event->unitPriceCent = product->preisCent;
}

static bool snapshot_original_sale_price(VerkaufEvent *event, int spieler_id,
                                         int produkt_id, const char *datum)
{
    // An unallocated correction is priced by the portal against its original
    // lot.  Only reuse a prior positive event's immutable receipt snapshot;
    // the current catalog is explicitly not evidence of that historic price.
    for (int i = g_store.pendingVerkaufEventCount - 1; i >= 0; --i) {
        const VerkaufEvent *prior = &g_store.pendingVerkaufEvents[i];
        if (prior->spielerId != spieler_id || prior->produktId != produkt_id ||
            prior->quantity <= 0 || prior->preisRevisionId <= 0 ||
            strcmp(prior->datum, datum) || prior->unitPriceCent == VERKAUF_UNIT_PRICE_UNKNOWN)
            continue;
        size_t product_name_len = strnlen(prior->produktName, sizeof(event->produktName) - 1);
        memmove(event->produktName, prior->produktName, product_name_len);
        event->produktName[product_name_len] = '\0';
        size_t category_len = strnlen(prior->category, sizeof(event->category) - 1);
        memmove(event->category, prior->category, category_len);
        event->category[category_len] = '\0';
        event->unitPriceCent = prior->unitPriceCent;
        return true;
    }
    return false;
}

const Produkt *store_produkt(const char *produkt_code)
{
    if (!produkt_code) return NULL;
    for (int i = 0; i < g_store.produkteCount; ++i)
        if (g_store.produkte[i].active &&
            strcmp(g_store.produkte[i].code, produkt_code) == 0) return &g_store.produkte[i];
    return NULL;
}

bool store_game_credit_price_valid(void)
{
    const Produkt *credit = store_produkt("GAME_CREDIT");
    return credit && credit->active && credit->id > 0 &&
           credit->preisRevisionId > 0 && credit->preisCent >= 0;
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

static bool has_pending_billable_activity(int spieler_id, const char *today)
{
    for (int i = 0; i < g_store.pendingVerkaufEventCount; ++i)
        if (g_store.pendingVerkaufEvents[i].spielerId == spieler_id &&
            !strcmp(g_store.pendingVerkaufEvents[i].datum, today))
            return true;
    for (int i = 0; i < g_store.pendingKreditEventCount; ++i)
        if (g_store.pendingKreditEvents[i].spielerId == spieler_id &&
            !strcmp(g_store.pendingKreditEvents[i].datum, today) &&
            !strcmp(g_store.pendingKreditEvents[i].typ, "USE"))
            return true;
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
    // A portal-accepted payment removes the daily credit roster. A later sale
    // or USE is nevertheless a fresh open bill and must be settleable without
    // first depending on that roster being rebuilt.
    if (!active && !has_pending_billable_activity(spieler_id, today)) {
        kredit_events_unlock();
        return false;
    }
    PaymentEvent *event =
        &g_store.pendingPaymentEvents[g_store.pendingPaymentEventCount++];
    memset(event, 0, sizeof(*event));
    snprintf(event->externalId, sizeof(event->externalId), "pay-%d-%08x",
             spieler_id, (unsigned)esp_random());
    event->spielerId = spieler_id;
    strftime(event->datum, sizeof(event->datum), "%Y-%m-%d", &tm);
    if (!save_payment_state_unlocked()) {
        memset(event, 0, sizeof(*event));
        --g_store.pendingPaymentEventCount;
        kredit_events_unlock();
        return false;
    }
    kredit_events_unlock();
    return true;
}

bool store_begin_payment_sync(PaymentEvent *snapshot, int capacity, int *out_count)
{
    if (out_count) *out_count = 0;
    if (!snapshot || capacity <= 0 || !out_count) return false;
    kredit_events_lock();
    PaymentEvent prior[MAX_PENDING_PAYMENTS];
    memcpy(prior, g_store.pendingPaymentEvents, sizeof(prior));
    int count = 0;
    for (int i = 0; i < g_store.pendingPaymentEventCount && count < capacity; ++i) {
        PaymentEvent *event = &g_store.pendingPaymentEvents[i];
        if (event->spielerId <= 0 || event->inFlight) continue;
        event->inFlight = true;
        snapshot[count++] = *event;
    }
    if (count && !save_payment_state_unlocked()) {
        memcpy(g_store.pendingPaymentEvents, prior, sizeof(prior));
        kredit_events_unlock();
        return false;
    }
    *out_count = count;
    kredit_events_unlock();
    return true;
}

bool store_finish_payment_sync(const PaymentEvent *snapshot, int count,
                               const char *const *acceptedIds, int acceptedCount,
                               const char *error)
{
    if (!snapshot || count <= 0) return true;
    kredit_events_lock();
    PaymentEvent prior_events[MAX_PENDING_PAYMENTS];
    int prior_count = g_store.pendingPaymentEventCount;
    int prior_ids[MAX_PORTAL_SPIELER];
    KreditStand prior_credits[MAX_PORTAL_SPIELER];
    memcpy(prior_events, g_store.pendingPaymentEvents, sizeof(prior_events));
    memcpy(prior_ids, g_store.kreditPlayerIds, sizeof(prior_ids));
    memcpy(prior_credits, g_store.kredite, sizeof(prior_credits));
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
    if (!save_payment_state_unlocked()) {
        memcpy(g_store.pendingPaymentEvents, prior_events, sizeof(prior_events));
        g_store.pendingPaymentEventCount = prior_count;
        memcpy(g_store.kreditPlayerIds, prior_ids, sizeof(prior_ids));
        memcpy(g_store.kredite, prior_credits, sizeof(prior_credits));
        kredit_events_unlock();
        return false;
    }
    kredit_events_unlock();
    return true;
}

void store_cache_bill_day(const BillDaySummary *summary)
{
    if (!summary || summary->playerCount < 0 || summary->playerCount > MAX_DAY_BILLS ||
        summary->categoryCount < 0 || summary->categoryCount > MAX_BILL_CATEGORIES ||
        summary->productCount < 0 || summary->productCount > MAX_DAY_PRODUCTS)
        return;
    for (int i = 0; i < summary->playerCount; ++i)
        if (summary->players[i].lineCount < 0 ||
            summary->players[i].lineCount > MAX_BILL_LINES ||
            summary->players[i].categoryCount < 0 ||
            summary->players[i].categoryCount > MAX_BILL_CATEGORIES)
            return;
    // Keep an unmodified portal snapshot.  The visible copy is reconstructed
    // below, rather than patched in place, so a later cache refresh cannot
    // count a still-pending idempotent event twice.
    g_store.billDayBaseline = *summary;
    store_rebuild_bill_projection();
}

static PlayerBill *projection_bill_for_player(BillDaySummary *summary, int spieler_id)
{
    for (int i = 0; i < summary->playerCount; ++i)
        if (summary->players[i].spielerId == spieler_id) return &summary->players[i];
    if (summary->playerCount >= MAX_DAY_BILLS) return NULL;
    PlayerBill *bill = &summary->players[summary->playerCount++];
    memset(bill, 0, sizeof(*bill));
    bill->spielerId = spieler_id;
    for (int i = 0; i < g_store.portalSpielerCount; ++i)
        if (g_store.portalSpieler[i].id == spieler_id) {
            snprintf(bill->spielerName, sizeof(bill->spielerName), "%s",
                     g_store.portalSpieler[i].name);
            break;
        }
    return bill;
}

static void projection_add_category(BillCategoryTotal *categories, int *count,
                                    const char *name, int cents)
{
    if (!name || !name[0] || !cents) return;
    for (int i = 0; i < *count; ++i)
        if (!strcmp(categories[i].name, name)) {
            categories[i].totalCent += cents;
            return;
        }
    if (*count >= MAX_BILL_CATEGORIES) return;
    snprintf(categories[*count].name, sizeof(categories[*count].name), "%s", name);
    categories[(*count)++].totalCent = cents;
}

static void projection_add_money_line(BillDaySummary *summary, int spieler_id,
                                      int product_id, int revision, const char *name,
                                      const char *category, int quantity, int unit_price)
{
    PlayerBill *bill = projection_bill_for_player(summary, spieler_id);
    if (!bill) { summary->productOverflow = true; return; }
    // A local action after a portal-paid bill is a new open balance.  It does
    // not revive the daily roster; that remains governed by payment acceptance.
    bill->state = BILL_OPEN;
    if (bill->lineCount >= MAX_BILL_LINES) {
        bill->lineOverflow = true;
        return;
    }
    BillLine *line = &bill->lines[bill->lineCount++];
    memset(line, 0, sizeof(*line));
    line->produktId = product_id;
    line->preisRevisionId = revision;
    line->quantity = quantity;
    line->unitPriceCent = unit_price;
    line->localPending = true;
    snprintf(line->produktName, sizeof(line->produktName), "%s", name && name[0] ? name : "ONBEKANNT");
    snprintf(line->category, sizeof(line->category), "%s", category && category[0] ? category : "ONBEKANNT");
    int cents = unit_price == VERKAUF_UNIT_PRICE_UNKNOWN ? 0 : quantity * unit_price;
    line->lineTotalCent = cents;
    bill->totalCent += cents;
    summary->generalTotalCent += cents;
    projection_add_category(bill->categories, &bill->categoryCount, line->category, cents);
    projection_add_category(summary->categories, &summary->categoryCount, line->category, cents);
}

void store_rebuild_bill_projection(void)
{
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &tm);
    if (strcmp(g_store.billDayBaseline.datum, today)) {
        // Offline activity must be payable even before the first bill GET.
        // Start an explicitly non-authoritative empty baseline for today.
        memset(&g_store.billDay, 0, sizeof(g_store.billDay));
        snprintf(g_store.billDay.datum, sizeof(g_store.billDay.datum), "%s", today);
    } else {
        g_store.billDay = g_store.billDayBaseline;
    }
    for (int i = 0; i < g_store.pendingVerkaufEventCount; ++i) {
        const VerkaufEvent *event = &g_store.pendingVerkaufEvents[i];
        if (!strcmp(event->datum, today))
            projection_add_money_line(&g_store.billDay, event->spielerId,
                event->produktId, event->preisRevisionId, event->produktName,
                event->category, event->quantity, event->unitPriceCent);
    }
    // Only consumption represents a billable game. Grants and unused credits
    // are credit-accounting facts, never money.
    const Produkt *game_credit = store_produkt("GAME_CREDIT");
    for (int i = 0; i < g_store.pendingKreditEventCount; ++i) {
        const KreditEvent *event = &g_store.pendingKreditEvents[i];
        if (strcmp(event->datum, today)) continue;
        PlayerBill *bill = projection_bill_for_player(&g_store.billDay, event->spielerId);
        if (!bill) continue;
        if (!strcmp(event->typ, "GRANT")) bill->creditGranted += event->anzahl;
        else if (!strcmp(event->typ, "USE")) {
            bill->creditUsed += event->anzahl;
            // New events have an immutable receipt. Only zero-valued legacy
            // records may fall back to the current cached catalog.
            int revision = event->preisRevisionId;
            int unit_price = event->unitPriceCent;
            if (revision <= 0 && game_credit) {
                revision = game_credit->preisRevisionId;
                unit_price = game_credit->preisCent;
            }
            if (revision > 0)
                projection_add_money_line(&g_store.billDay, event->spielerId,
                    game_credit ? game_credit->id : 0, revision,
                    game_credit ? game_credit->name : "GAME CREDIT",
                    game_credit ? game_credit->category : "GAME_CREDIT",
                    event->anzahl, unit_price);
        }
        bill->creditRemaining = bill->creditGranted - bill->creditUsed;
    }
    if (g_store.billDay.uniquePlayers < g_store.billDay.playerCount)
        g_store.billDay.uniquePlayers = g_store.billDay.playerCount;
    g_store.billDay.authoritative = g_store.billDayBaseline.authoritative;
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

// Daily ammunition counters are a cache, not the source of truth.  Rebuild
// them from events belonging to the new local date so a midnight rollover
// never discards another day's retry records or accidentally carries them in.
static void rollover_ammunition_day(const char *today)
{
    if (!today || !strcmp(g_store.verkaufDatum, today)) return;
    memset(g_store.munition, 0, sizeof(g_store.munition));
    g_store.verkaufCal12Total = g_store.verkaufCal20Total = 0;
    snprintf(g_store.verkaufDatum, sizeof(g_store.verkaufDatum), "%s", today);
    for (int i = 0; i < g_store.pendingVerkaufEventCount; ++i) {
        const VerkaufEvent *event = &g_store.pendingVerkaufEvents[i];
        if (!strcmp(event->datum, today))
            store_apply_portal_verkauf(event->spielerId, event->produktId, event->quantity);
    }
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
    MunitionStand prior_munition[MAX_PORTAL_SPIELER];
    memcpy(prior_munition, g_store.munition, sizeof(prior_munition));
    char prior_date[sizeof(g_store.verkaufDatum)];
    memcpy(prior_date, g_store.verkaufDatum, sizeof(prior_date));
    int prior_cal12_total = g_store.verkaufCal12Total;
    int prior_cal20_total = g_store.verkaufCal20Total;
    rollover_ammunition_day(today);
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
    if (quantity < 0) {
        if (!snapshot_original_sale_price(event, spieler_id, produkt->id, event->datum)) {
            // Keep product identity useful, but do not silently value an
            // unknown historic lot at today's catalog price.
            snprintf(event->produktName, sizeof(event->produktName), "%s", produkt->name);
            snprintf(event->category, sizeof(event->category), "%s", produkt->category);
            event->unitPriceCent = VERKAUF_UNIT_PRICE_UNKNOWN;
        }
    } else {
        snapshot_sale_product(event, produkt);
    }
    store_apply_portal_verkauf(spieler_id, produkt->id, quantity);
    esp_err_t persist = save_sale_state_unlocked();
    if (persist != ESP_OK) {
        memcpy(g_store.munition, prior_munition, sizeof(prior_munition));
        memcpy(g_store.verkaufDatum, prior_date, sizeof(prior_date));
        g_store.verkaufCal12Total = prior_cal12_total;
        g_store.verkaufCal20Total = prior_cal20_total;
        memset(event, 0, sizeof(*event));
        g_store.pendingVerkaufEventCount--;
    }
    xSemaphoreGive(s_verkauf_events_mutex);
    if (persist == ESP_OK) store_rebuild_bill_projection();
    return persist == ESP_OK;
}

static bool spieler_fuer_tag_aktiv_unlocked(int spieler_id, const char *today)
{
    if (spieler_id <= 0 || !today) return false;
    if (strcmp(g_store.kreditDatum, today) != 0) return false;
    bool exists = false;
    for (int i = 0; i < g_store.portalSpielerCount; ++i) {
        if (g_store.portalSpieler[i].id == spieler_id) {
            exists = true;
            break;
        }
    }
    if (!exists) return false;
    for (int k = 0; k < MAX_PORTAL_SPIELER; ++k)
        if (g_store.kreditPlayerIds[k] == spieler_id) return true;
    return false;
}

bool store_queue_catering_basket(int spieler_id, const int *produkt_ids,
                                 const int *quantities, int line_count)
{
    s_catering_error = "";
    if (!produkt_ids || !quantities || line_count < 1 || line_count > MAX_PRODUKTE) {
        s_catering_error = "ONGULTEGE WEENCHEN";
        return false;
    }
    xSemaphoreTake(s_verkauf_events_mutex, portMAX_DELAY);
    kredit_events_lock();
    time_t now = time(NULL); struct tm tmi; localtime_r(&now, &tmi);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &tmi);
    if (!spieler_fuer_tag_aktiv_unlocked(spieler_id, today)) {
        s_catering_error = "SPILLER NET FIR HAUT AUTORISEIERT";
        kredit_events_unlock();
        xSemaphoreGive(s_verkauf_events_mutex);
        return false;
    }
    MunitionStand prior_munition[MAX_PORTAL_SPIELER];
    memcpy(prior_munition, g_store.munition, sizeof(prior_munition));
    char prior_date[sizeof(g_store.verkaufDatum)];
    memcpy(prior_date, g_store.verkaufDatum, sizeof(prior_date));
    int prior_cal12_total = g_store.verkaufCal12Total;
    int prior_cal20_total = g_store.verkaufCal20Total;
    // Revalidate the cached catalog under the same lock as reservation.
    for (int line = 0; line < line_count; ++line) {
        const Produkt *p = NULL;
        for (int i = 0; i < g_store.produkteCount; ++i)
            if (g_store.produkte[i].id == produkt_ids[line]) { p = &g_store.produkte[i]; break; }
        if (!p || !p->active || p->preisRevisionId <= 0 || quantities[line] <= 0 ||
            (strcmp(p->category, "FOOD") && strcmp(p->category, "DRINK"))) {
            s_catering_error = !p ? "PRODUKT NET FONNT" :
                !p->active ? "PRODUKT NET AKTIV" :
                p->preisRevisionId <= 0 ? "PRODUKT HUET KEE PRAIS" :
                "ONGULTEGE PRODUKT-DATEN";
            kredit_events_unlock();
            xSemaphoreGive(s_verkauf_events_mutex);
            return false;
        }
    }
    // Reserve all slots before writing any event: a full outbox is all-or-nothing.
    if (g_store.pendingVerkaufEventCount + line_count > MAX_PENDING_VERKAEUFE) {
        s_catering_error = "VERKAAF-QUEUE ASS VOLL - EISCHT SYNC";
        kredit_events_unlock();
        xSemaphoreGive(s_verkauf_events_mutex);
        return false;
    }
    bool daily_reset = strcmp(g_store.verkaufDatum, today) != 0;
    rollover_ammunition_day(today);
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
        snapshot_sale_product(event, p);
        snprintf(event->datum, sizeof(event->datum), "%s", today);
    }
    // Commit while the outbox remains locked. The NVS commit is atomic; on a
    // failure remove the entire just-created basket from RAM as well, so a
    // caller can never observe a partially accepted basket.
    int first_event = g_store.pendingVerkaufEventCount - line_count;
    esp_err_t persist = save_catering_state_unlocked(daily_reset);
    if (persist != ESP_OK) {
        s_catering_error = persist == ESP_ERR_NVS_NOT_ENOUGH_SPACE
            ? "NVS ASS VOLL" : "NVS SCHREIF-FEELER";
        memset(&g_store.pendingVerkaufEvents[first_event], 0,
               (size_t)line_count * sizeof(VerkaufEvent));
        g_store.pendingVerkaufEventCount = first_event;
        memcpy(g_store.munition, prior_munition, sizeof(prior_munition));
        memcpy(g_store.verkaufDatum, prior_date, sizeof(prior_date));
        g_store.verkaufCal12Total = prior_cal12_total;
        g_store.verkaufCal20Total = prior_cal20_total;
    }
    kredit_events_unlock();
    xSemaphoreGive(s_verkauf_events_mutex);
    if (persist == ESP_OK) store_rebuild_bill_projection();
    return persist == ESP_OK;
}

const char *store_last_catering_error(void)
{
    return s_catering_error;
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
    store_rebuild_bill_projection();
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

bool store_spieler_fuer_tag_aktiv(int spieler_id)
{
    kredit_events_lock();
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &tm);
    bool active = spieler_fuer_tag_aktiv_unlocked(spieler_id, today);
    kredit_events_unlock();
    return active;
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

void store_reconcile_lineup_after_cache_load(void)
{
    int before[MAX_SPIELER];
    memcpy(before, g_store.lineupIds, sizeof(before));
    reconcile_lineup_with_roster();
    if (memcmp(before, g_store.lineupIds, sizeof(before)) != 0) {
        // Persist only the derived lineup repair. Calling game_store_save()
        // here would rewrite unrelated cache sections while cache loading is
        // still deciding which envelopes are valid.
        nvs_lock();
        esp_err_t err = nvs_set_blob(s_nvs, "lineup_ids", g_store.lineupIds,
                                     sizeof(g_store.lineupIds));
        if (err == ESP_OK) err = nvs_commit(s_nvs);
        nvs_unlock();
        if (err != ESP_OK)
            ESP_LOGE(TAG, "Could not persist cached-roster lineup repair: %s",
                     esp_err_to_name(err));
    }
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
    (void)offline_cache_save(OFFLINE_CACHE_ROSTER);
    (void)offline_cache_save(OFFLINE_CACHE_HISTORY);
    (void)offline_cache_save(OFFLINE_CACHE_CREDITS);
    (void)offline_cache_save(OFFLINE_CACHE_SALES);
    (void)offline_cache_save(OFFLINE_CACHE_BILLS);
}

void store_register_spieler_fuer_tag(int spieler_id)
{
    kredit_events_lock();
    int prior_ids[MAX_PORTAL_SPIELER];
    KreditStand prior_credits[MAX_PORTAL_SPIELER];
    char prior_date[sizeof(g_store.kreditDatum)];
    memcpy(prior_ids, g_store.kreditPlayerIds, sizeof(prior_ids));
    memcpy(prior_credits, g_store.kredite, sizeof(prior_credits));
    memcpy(prior_date, g_store.kreditDatum, sizeof(prior_date));
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &tm);
    if (strcmp(g_store.kreditDatum, today) != 0) {
        memset(g_store.kreditPlayerIds, 0, sizeof(g_store.kreditPlayerIds));
        memset(g_store.kredite, 0, sizeof(g_store.kredite));
        snprintf(g_store.kreditDatum, sizeof(g_store.kreditDatum), "%s", today);
    }
    for (int i = 0; i < MAX_PORTAL_SPIELER; i++) {
        if (g_store.kreditPlayerIds[i] == spieler_id) {
            kredit_events_unlock();
            return;
        }
        if (g_store.kreditPlayerIds[i] == 0) {
            g_store.kreditPlayerIds[i] = spieler_id;
            g_store.kredite[i] = (KreditStand){0, 0};
            if (!save_kredit_state_unlocked()) {
                memcpy(g_store.kreditPlayerIds, prior_ids, sizeof(prior_ids));
                memcpy(g_store.kredite, prior_credits, sizeof(prior_credits));
                memcpy(g_store.kreditDatum, prior_date, sizeof(prior_date));
            }
            kredit_events_unlock();
            return;
        }
    }
    kredit_events_unlock();
}

bool store_remove_spieler_fuer_tag(int spieler_id, char *reason, size_t reason_len)
{
    if (reason && reason_len) reason[0] = '\0';
    if (spieler_id <= 0) {
        if (reason && reason_len) snprintf(reason, reason_len, "ONGULTEGE SPILLER");
        return false;
    }
    // Match Catering's lock order. This makes the complete decision and roster
    // change indivisible with sale and credit/payment sync state transitions.
    xSemaphoreTake(s_verkauf_events_mutex, portMAX_DELAY);
    kredit_events_lock();
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &tm);
    int slot = find_kredit_slot(&g_store, spieler_id);
    const char *unsettled = NULL;
    if (slot < 0 || strcmp(g_store.kreditDatum, today))
        unsettled = "SPILLER NET AKTIV";
    else if (g_store.kredite[slot].gewaehrt || g_store.kredite[slot].verbraucht)
        unsettled = "KREDIT";
    else {
        for (int i = 0; i < MAX_PORTAL_SPIELER; ++i)
            if (g_store.munition[i].spielerId == spieler_id &&
                (g_store.munition[i].cal12 || g_store.munition[i].cal20)) {
                unsettled = "MUNITION"; break;
            }
        for (int i = 0; !unsettled && i < g_store.pendingVerkaufEventCount; ++i)
            if (g_store.pendingVerkaufEvents[i].spielerId == spieler_id &&
                !strcmp(g_store.pendingVerkaufEvents[i].datum, today)) {
                unsettled = "PENDING VERKAAF"; break;
            }
        for (int i = 0; !unsettled && i < g_store.pendingKreditEventCount; ++i)
            if (g_store.pendingKreditEvents[i].spielerId == spieler_id &&
                !strcmp(g_store.pendingKreditEvents[i].datum, today)) {
                unsettled = "PENDING KREDITT"; break;
            }
        for (int i = 0; !unsettled && i < g_store.pendingPaymentEventCount; ++i)
            if (g_store.pendingPaymentEvents[i].spielerId == spieler_id &&
                !strcmp(g_store.pendingPaymentEvents[i].datum, today)) {
                unsettled = "PENDING BEZUELUNG"; break;
            }
    }
    if (unsettled) {
        if (reason && reason_len) snprintf(reason, reason_len, "%s", unsettled);
        kredit_events_unlock();
        xSemaphoreGive(s_verkauf_events_mutex);
        return false;
    }
    KreditStand previous = g_store.kredite[slot];
    g_store.kreditPlayerIds[slot] = 0;
    g_store.kredite[slot] = (KreditStand){};
    bool persisted = save_kredit_state_unlocked();
    if (!persisted) {
        g_store.kreditPlayerIds[slot] = spieler_id;
        g_store.kredite[slot] = previous;
        if (reason && reason_len) snprintf(reason, reason_len, "NVS SCHREIF-FEELER");
    }
    kredit_events_unlock();
    xSemaphoreGive(s_verkauf_events_mutex);
    return persisted;
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
            store_rebuild_bill_projection();
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
            store_rebuild_bill_projection();
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
        size_t name_len = strnlen(s->portalSpieler[pidx].name, sizeof(sp->name) - 1);
        memmove(sp->name, s->portalSpieler[pidx].name, name_len);
        sp->name[name_len] = '\0';
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

    // A USE is a billable receipt, not merely a balance decrement. Refuse
    // before taking the mutation lock (and before changing any credit) unless
    // its immutable GAME_CREDIT price can be captured.
    if (!store_game_credit_price_valid()) {
        snprintf(s->lineupWarning, sizeof(s->lineupWarning),
                 "SPIELKREDITT-PRAIS NET GUELTEG - EISCHT SYNC.");
        return false;
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
    store_rebuild_bill_projection();
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
        size_t finished_at_len = strnlen(fg.finishedAt, sizeof(fg.base.finishedAt) - 1);
        memmove(fg.base.finishedAt, fg.finishedAt, finished_at_len);
        fg.base.finishedAt[finished_at_len] = '\0';
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
static QueueHandle_t s_sync_queue         = NULL;
static portMUX_TYPE s_sync_request_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_boot_sync_requested = false;
static bool s_boot_sync_consumed = false;
static bool s_sync_pending = false;
static bool s_sync_ui_ready = false;
static bool s_sync_ui_paused = false;
static uint32_t s_sync_publication_generation = 0;
static TickType_t s_next_auto_sync;

typedef enum {
    SYNC_REQUEST_ACCEPTED,
    SYNC_REQUEST_COALESCED,
    SYNC_REQUEST_FAILED,
} SyncRequestResult;

static void queue_boot_sync_if_pending(void);
static SyncRequestResult store_sync_request(SyncRequestSource source);

static const char *sync_source_label(SyncRequestSource source)
{
    switch (source) {
    case SYNC_REQUEST_BOOT: return "boot";
    case SYNC_REQUEST_AUTO: return "auto";
    default: return "manual";
    }
}

static void publish_sync_failure(const char *error)
{
    portENTER_CRITICAL(&s_sync_request_lock);
    g_store.syncStatus = SYNC_ERROR;
    snprintf(g_store.syncError, sizeof(g_store.syncError), "%s",
             error && error[0] ? error : "Sync failed");
    ++s_sync_publication_generation;
    portEXIT_CRITICAL(&s_sync_request_lock);
}

void store_create_workers(void)
{
    // Sync worker — stack MUST be in internal RAM.
    // NVS (called from game_store_save inside the sync path) writes to SPI
    // flash, which requires disabling the cache.  If the task stack is in
    // PSRAM the cache-disable assert fires immediately (esp_task_stack_is_sane_
    // cache_disabled).  Use MALLOC_CAP_INTERNAL so the stack survives the
    // cache-off window.
    QueueHandle_t sync_queue = xQueueCreate(1, sizeof(uint32_t));
    if (!sync_queue) {
        ESP_LOGE(TAG, "Sync worker queue allocation failed");
        publish_sync_failure("Sync worker unavailable");
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
                bool ui_ready;
                portENTER_CRITICAL(&s_sync_request_lock);
                ui_ready = s_sync_ui_ready;
                portEXIT_CRITICAL(&s_sync_request_lock);
                bool due = g_store.autoSyncEnabled &&
                           (int32_t)(xTaskGetTickCount() - s_next_auto_sync) >= 0;
                if (ui_ready && due && cop_wifi_is_connected() &&
                    !store_sync_is_queued_or_running()) {
                    store_sync_request(SYNC_REQUEST_AUTO);
                }
                continue;
            }
            SyncRequestSource source = (SyncRequestSource)dummy;
            ESP_LOGI(TAG, "Sync worker started source=%s interval=%us",
                     sync_source_label(source),
                     (unsigned)g_store.autoSyncSeconds);
            char sync_error[sizeof(g_store.syncError)] = {};
            bool ui_paused = false;
            for (int attempt = 0; attempt < 200; ++attempt) {
                portENTER_CRITICAL(&s_sync_request_lock);
                ui_paused = s_sync_ui_paused;
                portEXIT_CRITICAL(&s_sync_request_lock);
                if (ui_paused) break;
                vTaskDelay(pdMS_TO_TICKS(5));
            }

            esp_err_t err;
            if (!ui_paused) {
                err = ESP_ERR_TIMEOUT;
                snprintf(sync_error, sizeof(sync_error),
                         "UI pause acknowledgement timed out");
                ESP_LOGE(TAG, "Sync aborted before mutation: %s",
                         sync_error);
            } else {
                ESP_LOGI(TAG, "UI paused; sync mutation phase starting");
                err = http_sync_all();
            }
            if (err != ESP_OK && !sync_error[0]) {
                http_sync_copy_last_error(sync_error, sizeof(sync_error));
                if (!sync_error[0])
                    snprintf(sync_error, sizeof(sync_error),
                             "HTTP sync failed: %s", esp_err_to_name(err));
            }
            portENTER_CRITICAL(&s_sync_request_lock);
            g_store.syncStatus = (err == ESP_OK) ? SYNC_SUCCESS : SYNC_ERROR;
            snprintf(g_store.syncError, sizeof(g_store.syncError), "%s",
                     sync_error);
            s_sync_pending = false;
            s_sync_ui_paused = false;
            s_next_auto_sync = xTaskGetTickCount() +
                pdMS_TO_TICKS(g_store.autoSyncSeconds * 1000u);
            ++s_sync_publication_generation;
            uint32_t generation = s_sync_publication_generation;
            portEXIT_CRITICAL(&s_sync_request_lock);
            ESP_LOGI(TAG,
                     "Sync worker finished source=%s result=%s publication=%u",
                     sync_source_label(source),
                     err == ESP_OK ? "success" : "error",
                     (unsigned)generation);
            queue_boot_sync_if_pending();
        }
    }, "sync_w", 16384, sync_queue, 5, NULL,
       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (task_created != pdPASS) {
        vQueueDelete(sync_queue);
        ESP_LOGE(TAG, "Sync worker task creation failed");
        publish_sync_failure("Sync worker unavailable");
        return;
    }

    portENTER_CRITICAL(&s_sync_request_lock);
    s_sync_queue = sync_queue;
    s_next_auto_sync = xTaskGetTickCount() +
        pdMS_TO_TICKS(g_store.autoSyncSeconds * 1000u);
    portEXIT_CRITICAL(&s_sync_request_lock);

    ESP_LOGI(TAG, "Store workers created. Internal RAM remaining: %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

// ── Portal players ───────────────────────────────────────────
void store_load_portal_spieler(void)
{
    // A roster-only worker used to bypass the coherent sync publication
    // boundary. Route this legacy API through the same coalesced full sync.
    (void)store_sync_request(SYNC_REQUEST_MANUAL);
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
    (void)offline_cache_save(OFFLINE_CACHE_ROSTER);
}

// ── Player update queue ──────────────────────────────────────
static bool queue_kredit_event_unlocked(int spieler_id, const char *typ, int anzahl)
{
    if (anzahl == 0) return true;
    // New USE records must always carry a valid immutable bill receipt.
    // Zero revision remains reserved for loading legacy persisted events.
    if (!strcmp(typ, "USE") && !store_game_credit_price_valid())
        return false;
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
    if (!strcmp(typ, "USE")) {
        // A USE must remain priced at the moment it was committed locally.
        // A missing catalog leaves zeros for the legacy-compatible projection
        // fallback; it must never be silently represented as a money amount.
        const Produkt *credit = store_produkt("GAME_CREDIT");
        if (credit && credit->preisRevisionId > 0) {
            ev->preisRevisionId = credit->preisRevisionId;
            ev->unitPriceCent = credit->preisCent;
        }
        struct tm utc; gmtime_r(&now, &utc);
        int millis = (int)(tv.tv_usec / 1000);
        snprintf(ev->occurredAt, sizeof(ev->occurredAt),
                 "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                 utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                 utc.tm_hour, utc.tm_min, utc.tm_sec, millis);
    }
    return true;
}

bool store_queue_kredit_event(int spieler_id, const char *typ, int anzahl)
{
    kredit_events_lock();
    bool queued = queue_kredit_event_unlocked(spieler_id, typ, anzahl);
    if (queued && anzahl != 0 && !save_kredit_state_unlocked()) {
        memset(&g_store.pendingKreditEvents[--g_store.pendingKreditEventCount], 0,
               sizeof(g_store.pendingKreditEvents[0]));
        queued = false;
    }
    kredit_events_unlock();
    if (queued) store_rebuild_bill_projection();
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
    store_rebuild_bill_projection();
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

int store_pending_action_count(void)
{
    return g_store.pendingGamesCount + g_store.spielerUpdateCount +
           g_store.pendingKreditEventCount + g_store.pendingVerkaufEventCount +
           g_store.pendingPaymentEventCount;
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
static SyncRequestResult store_sync_request(SyncRequestSource source)
{
    // Just trigger the persistent worker — costs no RAM at call time.
    QueueHandle_t sync_queue = NULL;
    portENTER_CRITICAL(&s_sync_request_lock);
    sync_queue = s_sync_queue;
    portEXIT_CRITICAL(&s_sync_request_lock);
    if (!sync_queue) {
        publish_sync_failure("Sync worker unavailable");
        return SYNC_REQUEST_FAILED;
    }

    portENTER_CRITICAL(&s_sync_request_lock);
    if (s_sync_pending) {
        portEXIT_CRITICAL(&s_sync_request_lock);
        ESP_LOGI(TAG, "Sync request coalesced source=%s interval=%us",
                 sync_source_label(source), (unsigned)g_store.autoSyncSeconds);
        return SYNC_REQUEST_COALESCED;
    }
    s_sync_pending = true;
    s_sync_ui_paused = false;
    g_store.syncStatus = SYNC_RUNNING;
    g_store.syncError[0] = '\0';
    portEXIT_CRITICAL(&s_sync_request_lock);

    uint32_t trigger = (uint32_t)source;
    if (xQueueSend(sync_queue, &trigger, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_sync_request_lock);
        s_sync_pending = false;
        s_sync_ui_paused = false;
        g_store.syncStatus = SYNC_ERROR;
        snprintf(g_store.syncError, sizeof(g_store.syncError),
                 "Sync request queue failed");
        ++s_sync_publication_generation;
        portEXIT_CRITICAL(&s_sync_request_lock);
        ESP_LOGE(TAG, "Sync request queue failed source=%s",
                 sync_source_label(source));
        return SYNC_REQUEST_FAILED;
    }
    ESP_LOGI(TAG, "Sync request accepted source=%s interval=%us",
             sync_source_label(source), (unsigned)g_store.autoSyncSeconds);
    return SYNC_REQUEST_ACCEPTED;
}

bool store_sync(void)
{
    return store_sync_request(SYNC_REQUEST_MANUAL) == SYNC_REQUEST_ACCEPTED;
}

bool store_sync_is_queued_or_running(void)
{
    portENTER_CRITICAL(&s_sync_request_lock);
    bool pending = s_sync_pending;
    portEXIT_CRITICAL(&s_sync_request_lock);
    return pending;
}

void store_get_sync_ui_state(SyncUiState *state)
{
    if (!state) return;
    portENTER_CRITICAL(&s_sync_request_lock);
    state->status = g_store.syncStatus;
    state->publicationGeneration = s_sync_publication_generation;
    memcpy(state->error, g_store.syncError, sizeof(state->error));
    portEXIT_CRITICAL(&s_sync_request_lock);
}

void store_sync_set_ui_ready(void)
{
    portENTER_CRITICAL(&s_sync_request_lock);
    s_sync_ui_ready = true;
    portEXIT_CRITICAL(&s_sync_request_lock);
    ESP_LOGI(TAG, "UI publication boundary ready; boot sync may start");
    queue_boot_sync_if_pending();
}

void store_sync_ack_ui_paused(void)
{
    portENTER_CRITICAL(&s_sync_request_lock);
    if (s_sync_pending && g_store.syncStatus == SYNC_RUNNING)
        s_sync_ui_paused = true;
    portEXIT_CRITICAL(&s_sync_request_lock);
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
    bool coalesced = false;
    portENTER_CRITICAL(&s_sync_request_lock);
    // This is deliberately one-shot per boot. A connection flap before the
    // initial request is accepted can retry queueing; once accepted, normal
    // scheduler/manual paths handle all later syncs.
    if (!s_boot_sync_consumed) {
        if (s_sync_pending) {
            s_boot_sync_requested = false;
            s_boot_sync_consumed = true;
            coalesced = true;
        } else {
            s_boot_sync_requested = true;
        }
    }
    portEXIT_CRITICAL(&s_sync_request_lock);

    if (coalesced)
        ESP_LOGI(TAG, "Boot sync coalesced into active sync generation");
    queue_boot_sync_if_pending();
}

static void queue_boot_sync_if_pending(void)
{
    bool queue_sync_now = false;
    bool coalesced = false;
    portENTER_CRITICAL(&s_sync_request_lock);
    if (s_sync_ui_ready && s_boot_sync_requested && s_sync_queue) {
        // A boot request arriving during another source joins that active
        // generation instead of forcing a second full sync immediately after.
        if (s_sync_pending) {
            s_boot_sync_requested = false;
            s_boot_sync_consumed = true;
            coalesced = true;
        } else {
            // Consume before leaving the lock. If queueing fails it is
            // restored below so a later connection callback can retry.
            s_boot_sync_requested = false;
            s_boot_sync_consumed = true;
            queue_sync_now = true;
        }
    }
    portEXIT_CRITICAL(&s_sync_request_lock);

    if (coalesced)
        ESP_LOGI(TAG, "Boot sync coalesced into active sync generation");
    SyncRequestResult result = queue_sync_now
        ? store_sync_request(SYNC_REQUEST_BOOT)
        : SYNC_REQUEST_COALESCED;
    if (queue_sync_now && result == SYNC_REQUEST_FAILED) {
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
    nvs_lock();
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
    if (set_counted_blob("sales", g_store.pendingVerkaufEvents,
                         g_store.pendingVerkaufEventCount,
                         sizeof(g_store.pendingVerkaufEvents[0])) == ESP_OK)
        nvs_set_i32(s_nvs, "sale_count", g_store.pendingVerkaufEventCount);
    (void)set_ammo_blob_unlocked();
    nvs_set_str(s_nvs, "sale_date", g_store.verkaufDatum);
    nvs_set_i32(s_nvs, "sale_12", g_store.verkaufCal12Total);
    nvs_set_i32(s_nvs, "sale_20", g_store.verkaufCal20Total);
    if (set_counted_blob("payments", g_store.pendingPaymentEvents,
                         g_store.pendingPaymentEventCount,
                         sizeof(g_store.pendingPaymentEvents[0])) == ESP_OK)
        nvs_set_i32(s_nvs, "payment_cnt", g_store.pendingPaymentEventCount);
    nvs_set_blob(s_nvs, "lineup_ids", g_store.lineupIds, sizeof(g_store.lineupIds));
    nvs_set_str(s_nvs, "credit_date", g_store.kreditDatum);
    nvs_set_blob(s_nvs, "credit_ids", g_store.kreditPlayerIds, sizeof(g_store.kreditPlayerIds));
    nvs_set_blob(s_nvs, "credits", g_store.kredite, sizeof(g_store.kredite));
    if (set_credit_events_blob_unlocked() == ESP_OK)
        nvs_set_i32(s_nvs, "credit_evt_cnt", g_store.pendingKreditEventCount);
    if (set_counted_blob("sp_updates", g_store.spielerUpdates,
                         g_store.spielerUpdateCount,
                         sizeof(g_store.spielerUpdates[0])) == ESP_OK)
        nvs_set_i32(s_nvs, "sp_up_cnt", g_store.spielerUpdateCount);
    nvs_commit(s_nvs);
    nvs_unlock();
}

// ── Init ─────────────────────────────────────────────────────
void game_store_init(void)
{
    memset(&g_store, 0, sizeof(g_store));
    s_nvs_mutex = xSemaphoreCreateMutex();
    configASSERT(s_nvs_mutex);
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
    int32_t credit_event_count = 0;
    if (nvs_get_i32(s_nvs, "credit_evt_cnt", &credit_event_count) == ESP_OK &&
        credit_event_count >= 0 && credit_event_count <= MAX_KREDIT_EVENTS) {
        size_t credit_events_size = 0;
        bool current_format = false, legacy_format = false;
        if (credit_event_count &&
            nvs_get_blob(s_nvs, "credit_events", NULL, &credit_events_size) == ESP_OK) {
            current_format = credit_events_size ==
                (size_t)credit_event_count * sizeof(KreditEvent) ||
                credit_events_size == sizeof(g_store.pendingKreditEvents);
            legacy_format = credit_events_size ==
                (size_t)credit_event_count * sizeof(LegacyKreditEvent) ||
                credit_events_size == (size_t)MAX_KREDIT_EVENTS * sizeof(LegacyKreditEvent);
        }
        bool loaded = credit_event_count == 0;
        if (!loaded && current_format) {
            credit_events_size = sizeof(g_store.pendingKreditEvents);
            loaded = nvs_get_blob(s_nvs, "credit_events", g_store.pendingKreditEvents,
                                  &credit_events_size) == ESP_OK;
        } else if (!loaded && legacy_format) {
            LegacyKreditEvent legacy[MAX_KREDIT_EVENTS] = {};
            credit_events_size = sizeof(legacy);
            if (nvs_get_blob(s_nvs, "credit_events", legacy, &credit_events_size) == ESP_OK) {
                for (int i = 0; i < credit_event_count; ++i) {
                    KreditEvent *out = &g_store.pendingKreditEvents[i];
                    memcpy(out->externalId, legacy[i].externalId, sizeof(out->externalId));
                    out->spielerId = legacy[i].spielerId;
                    memcpy(out->datum, legacy[i].datum, sizeof(out->datum));
                    memcpy(out->typ, legacy[i].typ, sizeof(out->typ));
                    out->anzahl = legacy[i].anzahl;
                    out->inFlight = legacy[i].inFlight;
                }
                loaded = true;
            }
        }
        bool valid = true;
        for (int i = 0; loaded && i < credit_event_count; ++i) {
            const KreditEvent *event = &g_store.pendingKreditEvents[i];
            if (event->spielerId == 0 || event->anzahl == 0 ||
                !memchr(event->externalId, '\0', sizeof(event->externalId)) ||
                (strcmp(event->typ, "GRANT") != 0 && strcmp(event->typ, "USE") != 0)) {
                valid = false;
                break;
            }
        }
        if (loaded && valid) g_store.pendingKreditEventCount = credit_event_count;
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
    // Read legacy snapshots once so the migration below can atomically copy
    // them to FAT before freeing scarce NVS space.
    if (!load_counted_blob_compat("plrs", "plr_cnt", g_store.portalSpieler,
                                  MAX_PORTAL_SPIELER, sizeof(g_store.portalSpieler[0]),
                                  &g_store.portalSpielerCount)) {
        memset(g_store.portalSpieler, 0, sizeof(g_store.portalSpieler));
        g_store.portalSpielerCount = 0;
    }
    if (!load_counted_blob_compat("sp_updates", "sp_up_cnt",
                                  g_store.spielerUpdates, MAX_SPIELER_UPDATES,
                                  sizeof(g_store.spielerUpdates[0]),
                                  &g_store.spielerUpdateCount)) {
        memset(g_store.spielerUpdates, 0, sizeof(g_store.spielerUpdates));
        g_store.spielerUpdateCount = 0;
    }
    // Reconcile after the FAT roster cache is loaded; doing it here would
    // erase a valid persisted lineup merely because NVS no longer stores the
    // portal snapshot.

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
    if (!load_counted_blob_compat("products", "prod_count", g_store.produkte,
                                  MAX_PRODUKTE, sizeof(g_store.produkte[0]),
                                  &g_store.produkteCount))
        g_store.produkteCount = 0;
    if (!load_sales_blob_compat())
        g_store.pendingVerkaufEventCount = 0;
    int ammo_count = 0;
    int32_t stored_ammo_count = 0;
    if (nvs_get_i32(s_nvs, "ammo_count", &stored_ammo_count) == ESP_OK) {
        if (!load_counted_blob_compat("ammo", "ammo_count", g_store.munition,
                                      MAX_PORTAL_SPIELER,
                                      sizeof(g_store.munition[0]), &ammo_count))
            memset(g_store.munition, 0, sizeof(g_store.munition));
    } else {
        size_t legacy_ammo_size = sizeof(g_store.munition);
        if (nvs_get_blob(s_nvs, "ammo", g_store.munition,
                         &legacy_ammo_size) != ESP_OK ||
            legacy_ammo_size != sizeof(g_store.munition))
            memset(g_store.munition, 0, sizeof(g_store.munition));
    }
    nvs_load_str("sale_date", g_store.verkaufDatum, sizeof(g_store.verkaufDatum));
    nvs_get_i32(s_nvs, "sale_12", &g_store.verkaufCal12Total);
    nvs_get_i32(s_nvs, "sale_20", &g_store.verkaufCal20Total);
    if (!load_counted_blob_compat("payments", "payment_cnt",
                                  g_store.pendingPaymentEvents,
                                  MAX_PENDING_PAYMENTS,
                                  sizeof(g_store.pendingPaymentEvents[0]),
                                  &g_store.pendingPaymentEventCount))
        g_store.pendingPaymentEventCount = 0;
    // A power loss can occur after inFlight was committed but before an HTTP
    // response reached us. Reset it at boot and retry its stable externalId.
    bool reset_payment_flights = false;
    for (int i = 0; i < g_store.pendingPaymentEventCount; ++i) {
        reset_payment_flights |= g_store.pendingPaymentEvents[i].inFlight;
        g_store.pendingPaymentEvents[i].inFlight = false;
    }
    if (reset_payment_flights) {
        kredit_events_lock();
        if (!save_payment_state_unlocked())
            ESP_LOGE(TAG, "Could not durably reset payment in-flight markers");
        kredit_events_unlock();
    }
    // One-time migration: load legacy maximum-capacity blobs first, then free
    // the two large rebuildable caches and rewrite all counted records compactly.
    int32_t nvs_layout = 0;
    nvs_get_i32(s_nvs, "nvs_layout", &nvs_layout);
    if (nvs_layout < NVS_LAYOUT_VERSION) {
        // Never erase a legacy rebuildable cache until its FAT replacement is
        // durable. A failed migration intentionally leaves the old blobs for
        // the next boot rather than sacrificing offline operation.
        bool roster_ok = g_store.portalSpielerCount == 0 ||
                         offline_cache_save(OFFLINE_CACHE_ROSTER);
        bool products_ok = g_store.produkteCount == 0 ||
                           offline_cache_save(OFFLINE_CACHE_PRODUCTS);
        if (!roster_ok || !products_ok) {
            ESP_LOGE(TAG, "Legacy portal cache migration to FAT failed; retaining NVS");
        } else {
            // FAT snapshots are durable; the legacy copies can now be retired.
            nvs_erase_key(s_nvs, "bill_day");
            nvs_erase_key(s_nvs, "plrs");
            nvs_erase_key(s_nvs, "products");
            nvs_commit(s_nvs);

            // Recreate only durable outboxes. Rebuildable portal snapshots moved
            // to FAT and must not be written back into NVS.
            esp_err_t migration = set_counted_blob("sales", g_store.pendingVerkaufEvents,
                                             g_store.pendingVerkaufEventCount,
                                             sizeof(g_store.pendingVerkaufEvents[0]));
            if (migration == ESP_OK)
                migration = set_counted_blob("payments", g_store.pendingPaymentEvents,
                                             g_store.pendingPaymentEventCount,
                                             sizeof(g_store.pendingPaymentEvents[0]));
            if (migration == ESP_OK)
                migration = set_counted_blob("sp_updates", g_store.spielerUpdates,
                                             g_store.spielerUpdateCount,
                                             sizeof(g_store.spielerUpdates[0]));
            if (migration == ESP_OK) migration = set_credit_events_blob_unlocked();
            if (migration == ESP_OK) migration = set_ammo_blob_unlocked();
            if (migration == ESP_OK)
                migration = nvs_set_i32(s_nvs, "nvs_layout", NVS_LAYOUT_VERSION);
            if (migration == ESP_OK) migration = nvs_commit(s_nvs);
            if (migration == ESP_OK)
                ESP_LOGI(TAG, "Migrated NVS counted blobs to compact layout v%d",
                         NVS_LAYOUT_VERSION);
            else
                ESP_LOGE(TAG, "NVS compact-layout migration failed: %s",
                         esp_err_to_name(migration));
        }
    }
    time_t today_now = time(NULL); struct tm today_tm; localtime_r(&today_now, &today_tm);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &today_tm);
    rollover_ammunition_day(today);

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
