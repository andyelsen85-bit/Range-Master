#pragma once
// ============================================================
// TrapMaster game store — C port of emulator gameStore.ts
// ============================================================
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_SPIELER         6
#define MAX_SEQUENZ         50
#define MAX_HISTORY         20   // in-RAM cache; older games live on FAT flash
#define MAX_PORTAL_SPIELER  200
#define MAX_PENDING_GAMES   30   // unsynced queue; portal sync flushes this
#define MAX_ERGEBNISSE      200
#define MAX_NAME_LEN        64
#define MAX_EMAIL_LEN       64
#define MAX_SPIELER_UPDATES 10
#define MAX_KREDIT_EVENTS   50   // pending grant/use events queued for portal sync
#define MAX_PRODUKTE        12   // deliberately small: cached catalog for offline sales
#define MAX_PENDING_VERKAEUFE 64 // durable, idempotent sale-event outbox
#define VERKAUF_PRICE_REVISION_UNALLOCATED 0 // server allocates correction to original lot
#define VERKAUF_UNIT_PRICE_UNKNOWN INT32_MIN // local-only: do not include in money totals
#define MAX_PENDING_PAYMENTS 64  // durable Paid confirmations awaiting portal acceptance
#define MAX_DAY_BILLS       24   // bounded offline cache for one authoritative day
// A bill can contain the current catalog's grouped lines plus every retained
// outbox event at a no-longer-current price revision.  This is deliberately
// derived from the two bounded sources rather than being a UI-sized limit.
#define MAX_BILL_LINES       (MAX_PRODUKTE + MAX_PENDING_VERKAEUFE)
#define MAX_BILL_CATEGORIES  MAX_PRODUKTE
#define MAX_DAY_PRODUCTS     MAX_BILL_LINES
#define MAX_URL_LEN         128
#define MAX_KEY_LEN         65
#define TM_MAX_SSID_LEN     33
#define MAX_PASS_LEN        64
#define CUSTOM_SEQ_MAX      16
#define AUTO_SYNC_MIN_SECONDS 10u
#define AUTO_SYNC_MAX_SECONDS 86400u
#define AUTO_SYNC_DEFAULT_SECONDS 300u
#define BILLING_SYNC_MIN_SECONDS 20u
#define BILLING_SYNC_MAX_SECONDS 30u
#define BILLING_SYNC_DEFAULT_SECONDS 30u
// Current API tokens are "sha256:" plus 64 hex characters. Keep headroom for
// future opaque revision-token formats and the terminating NUL.
#define CACHE_MANIFEST_TOKEN_LEN 96

// ── Types ────────────────────────────────────────────────────

typedef enum {
    MASCHINE_A = 0, MASCHINE_B, MASCHINE_C, MASCHINE_D,
    MASCHINE_E, MASCHINE_F, MASCHINE_G, MASCHINE_H,
    MASCHINE_COUNT
} Maschine;

typedef enum {
    MODUS_NORMAL = 0,
    MODUS_HARAKIRI,
    MODUS_CUSTOM_1,
    MODUS_CUSTOM_2,
    MODUS_CUSTOM_3,
    MODUS_CUSTOM_4,
    MODUS_COUNT
} Modus;

// This is deliberately independent from Modus: Catering never becomes a game
// format and therefore cannot reach the launcher/fire flow.
typedef enum {
    TERMINAL_MODE_NORMAL = 0,
    TERMINAL_MODE_CATERING,
} TerminalOperatingMode;
typedef enum {
    CATERING_PIN_OK = 0,
    CATERING_PIN_WRONG,
    CATERING_PIN_LOCKED,
    CATERING_PIN_NOT_CONFIGURED,
} CateringPinVerifyResult;

typedef enum {
    SCREEN_DASHBOARD = 0,
    SCREEN_START,
    SCREEN_SPIEL,
    SCREEN_RESULTATE,
    SCREEN_GESCHICHTE,
    SCREEN_KREDITE,
    SCREEN_SPILLER,
    SCREEN_EINSTELLUNGEN,
    SCREEN_WIFI,
    SCREEN_CATERING,
    SCREEN_COUNT
} Screen;

typedef enum {
    SYNC_IDLE = 0,
    SYNC_RUNNING,
    SYNC_SUCCESS,
    SYNC_ERROR,
} SyncStatus;

typedef enum {
    SYNC_REQUEST_MANUAL = 1,
    SYNC_REQUEST_BOOT,
    SYNC_REQUEST_AUTO,
    SYNC_REQUEST_BILLING_AUTO,
} SyncRequestSource;

typedef struct {
    SyncStatus status;
    uint32_t publicationGeneration;
    // True only for the short interval in which the sync worker is about to
    // replace a shared cache dataset. SYNC_RUNNING itself is non-blocking.
    bool commitRequested;
    char error[128];
} SyncUiState;

typedef struct {
    int     id;
    char    name[MAX_NAME_LEN];
    int     punkte;
    int     startPosten;   // 1..5
} Spieler;

typedef struct {
    Maschine  maschine;
    bool      isDoublette;   // true if this entry is the second result of a pair
    bool      isPair;        // H1/H2 or an A-G custom doublette
    Maschine  partner;       // only meaningful on the first custom-pair entry
    uint16_t  delayMs;       // only meaningful on the first custom-pair entry
} SequenzEintrag;

typedef struct {
    Maschine  maschine;
    Maschine  partner;       // required for an A-G doublette, ignored for H
    bool      isDoublette;   // H-only doublette or an ordered A-G pair
    uint16_t  delayMs;       // A-G pair delay, 0-10000 ms
} CustomSequenzEintrag;

typedef struct {
    int      spielerId;
    int      lauf;
    int      taube;
    Maschine maschine;
    int      posten;
    bool     schuss1;
    bool     schuss2;
    int      punkte;
    bool     wiederholt;
} Ergebnis;

typedef struct {
    int   id;
    char  name[MAX_NAME_LEN];
    char  mitgliedNr[32];
    char  email[MAX_EMAIL_LEN];   // populated from portal sync
    bool  lokal;                  // created on terminal, not yet pushed
    bool  portalAktiv;
} PortalSpieler;

typedef enum {
    SPIELER_UPDATE_PROFILE = 0,
    SPIELER_UPDATE_PASSWORT_RESET,
    SPIELER_CREATE,        // terminal-local player; must be synced before updates
} SpielerUpdateTyp;

typedef struct {
    bool             used;
    int              spielerId;
    SpielerUpdateTyp typ;
    char             externalId[36];
    char             name[MAX_NAME_LEN];
    char             email[MAX_EMAIL_LEN];
    bool             portalAktiv;
} SpielerUpdateEntry;

typedef struct {
    char externalId[40]; // unique event ID for idempotent portal push
    int  spielerId;
    char datum[11];      // YYYY-MM-DD
    char typ[8];         // "GRANT" or "USE"
    int  anzahl;
    // USE is a billable occurrence.  Its immutable price snapshot is retained
    // even if a later catalog pull changes GAME_CREDIT.
    char occurredAt[32]; // UTC ISO timestamp; empty on legacy records
    int  preisRevisionId;
    int  unitPriceCent;
    bool inFlight;       // included in the current portal POST; may be accepted
} KreditEvent;

typedef struct {
    int   gewaehrt;
    int   verbraucht;
} KreditStand;

typedef struct {
    int  id;                 // canonical portal product ID
    char code[24];           // optional portal code; empty when API returned null
    char category[24];       // e.g. AMMUNITION
    char name[32];
    bool active;
    int  preisCent;          // currentPrice.unitPriceCents
    int  preisRevisionId;    // currentPrice.id; immutable event reference
} Produkt;

typedef struct {
    int spielerId;
    int cal12;
    int cal20;
} MunitionStand;

typedef struct {
    char externalId[40];
    int  spielerId;
    char datum[11];
    int  produktId;
    // Positive sales carry their immutable price revision. Negative corrections
    // use VERKAUF_PRICE_REVISION_UNALLOCATED so the server reverses the
    // original lot instead of applying today's cached price.
    int  preisRevisionId;
    int  quantity;           // signed (+ sale, - correction)
    // Display/audit snapshots. These are local-only and intentionally never
    // added to the established /api/sync/sales event contract.
    char produktName[32];
    char category[24];
    int  unitPriceCent;
    bool inFlight;
} VerkaufEvent;

// A payment never mutates the active-day roster until the portal explicitly
// accepts this idempotent event.  inFlight is persisted so a reset during a
// request simply retries the same externalId.
typedef struct {
    char externalId[40];
    int  spielerId;
    char datum[11];
    bool inFlight;
    char lastError[48];
} PaymentEvent;

typedef struct {
    int  produktId;
    int  preisRevisionId;
    char produktName[32];
    char category[24];
    int  quantity;
    int  unitPriceCent;
    int  lineTotalCent;
    bool localPending; // projected unsynced event, not in portal baseline
} BillLine;

typedef struct {
    char name[24];
    int  totalCent;
} BillCategoryTotal;

typedef enum {
    BILL_OPEN = 0,
    BILL_PENDING_NEUTRAL,
    BILL_PAID,
} BillState;

typedef struct {
    int  spielerId;
    char spielerName[MAX_NAME_LEN];
    BillLine lines[MAX_BILL_LINES];
    int  lineCount;
    bool lineOverflow; // source exceeded supported bounded bill capacity
    BillCategoryTotal categories[MAX_BILL_CATEGORIES];
    int  categoryCount;
    int  totalCent;
    int  creditGranted;
    int  creditUsed;
    int  creditRemaining;
    int  games;
    int  completedGames;
    int  confirmedClays;
    BillState state;
    char paymentExternalId[40];
    char paidAt[32];
    char paymentSource[16];
} PlayerBill;

typedef struct {
    char datum[11];
    PlayerBill players[MAX_DAY_BILLS];
    int playerCount;
    BillCategoryTotal categories[MAX_BILL_CATEGORIES];
    int categoryCount;
    BillLine products[MAX_DAY_PRODUCTS];
    int productCount;
    bool productOverflow;
    int generalTotalCent;
    int uniquePlayers;
    int paidPlayers;
    int games;
    int completedGames;
    int confirmedClays;
    bool authoritative;
} BillDaySummary;

typedef struct {
    char     externalId[40];
    char     datum[11];        // YYYY-MM-DD (local date, for terminal display)
    char     finishedAt[32];   // ISO datetime UTC "YYYY-MM-DDTHH:MM:SS.000Z" sent as API datum
    Modus    modus;
    int      lauf;
    int      taubenProLauf;
    bool     abgeschlossen;
    int      confirmedLaunches; // actual gateway ACKed launches; never test/skips
    struct {
        int  spielerId;
        int  startPosten;
        int  punkte;
        int  lauf;
    } teilnahmen[MAX_SPIELER];
    int teilnahmen_count;
    Ergebnis ergebnisse[MAX_ERGEBNISSE];
    int      ergebnisse_count;
} PendingGame;

typedef struct {
    PendingGame base;
    char        finishedAt[32];  // ISO datetime "YYYY-MM-DDTHH:MM:SS.000Z\0" + headroom
    char        spielerNamen[MAX_SPIELER][MAX_NAME_LEN];
    int         spielerIds[MAX_SPIELER]; // to map Namen
    int         spieler_count;
} FinishedGame;

typedef struct {
    // Settings
    Modus   modus;
    TerminalOperatingMode operatingMode;
    // Only a random salt and derived digest are retained; never the PIN.
    uint8_t cateringPinSalt[16];
    uint8_t cateringPinHash[32];
    bool    cateringPinConfigured;
    uint8_t cateringPinFailures;
    int64_t cateringPinLockoutUntil; // Unix seconds; 0 if clock unavailable
    bool    maschinenAktiv[MASCHINE_COUNT];
    char    apiUrl[MAX_URL_LEN];
    char    apiKey[MAX_KEY_LEN];
    char    gatewayUrl[MAX_URL_LEN]; // local TrapMaster gateway, e.g. http://192.168.1.50
    char    gatewayToken[MAX_KEY_LEN]; // private HMAC key for the local gateway
    uint32_t gatewaySequence; // persisted, strictly increasing gateway command sequence
    char    wifiSsid[TM_MAX_SSID_LEN];
    char    wifiPass[MAX_PASS_LEN];
    bool    autoSyncEnabled;
    uint32_t autoSyncSeconds;
    uint32_t billingSyncSeconds;
    CustomSequenzEintrag customSequenzen[4][CUSTOM_SEQ_MAX];
    int      customSequenzLen[4];
    int      customLaeufe[4];        // 1 or 2

    // Active screen
    Screen  screen;

    // In-game
    Spieler spieler[MAX_SPIELER];
    int     spielerCount;
    // Durable setup slots.  These deliberately contain IDs only: scores and
    // rotating in-game positions never become the next game's lineup.
    int     lineupIds[MAX_SPIELER]; // index + 1 is the selected starting post
    char    lineupWarning[128];     // recoverable setup/roster warning
    int     lauf;
    int     taubeIndex;
    int     spielerIndex;
    SequenzEintrag sequenz[MAX_SEQUENZ];
    int     sequenzLen;
    Ergebnis ergebnisse[MAX_ERGEBNISSE];
    int      ergebnisseCount;
    char     spielId[40];
    bool     currentFireSent; // one physical gateway request per launch unit
    int      activeAcknowledgedClays;
    // Exact USE events charged when this game started. They let a canceled
    // game safely remove unsynced charges or compensate charges already sent.
    int      activeGameCreditPlayerIds[MAX_SPIELER];
    char     activeGameCreditUseIds[MAX_SPIELER][40];
    int      activeGameCreditCount;

    // Portal cache
    PortalSpieler portalSpieler[MAX_PORTAL_SPIELER];
    int           portalSpielerCount;

    // Sync
    SyncStatus syncStatus;
    char       syncError[128];
    int64_t    lastSuccessfulSyncAt;
    bool       offlineCacheHealthy;
    bool       offlineCacheLoaded;
    char       cacheManifestTokens[6][CACHE_MANIFEST_TOKEN_LEN];
    char       cacheManifestDailyDate[11];
    char       lastConfigBackupAt[32];
    char       configBackupStatus[128];

    // Credits (today)
    char       kreditDatum[11];    // YYYY-MM-DD
    KreditStand kredite[MAX_PORTAL_SPIELER]; // indexed by portal player id slot
    int         kreditPlayerIds[MAX_PORTAL_SPIELER]; // parallel array of ids
    MunitionStand munition[MAX_PORTAL_SPIELER]; // same daily player capacity
    char          verkaufDatum[11];             // date represented by munition[]
    int32_t       verkaufCal12Total;            // portal aggregate + local pending events
    int32_t       verkaufCal20Total;

    // Cached catalog remains usable while offline. A successful catalog pull
    // replaces it wholesale, making portal prices authoritative.
    Produkt produkte[MAX_PRODUKTE];
    int     produkteCount;
    VerkaufEvent pendingVerkaufEvents[MAX_PENDING_VERKAEUFE];
    int          pendingVerkaufEventCount;
    PaymentEvent pendingPaymentEvents[MAX_PENDING_PAYMENTS];
    int          pendingPaymentEventCount;
    // The FAT snapshot is always the portal baseline. billDay is its local
    // projection and may contain unsynced sale/USE lines.
    BillDaySummary billDayBaseline;
    BillDaySummary billDay;

    // Queued games
    PendingGame  pendingGames[MAX_PENDING_GAMES];
    int          pendingGamesCount;

    // Finished game (last result shown on SCREEN_RESULTATE)
    FinishedGame lastFinished;
    bool         hasLastFinished;

    // Local history
    FinishedGame history[MAX_HISTORY];
    int          historyCount;

    // Queued player edits (pushed on next sync)
    SpielerUpdateEntry spielerUpdates[MAX_SPIELER_UPDATES];
    int                spielerUpdateCount;

    // Queued credit events (GRANT / USE) — pushed on next sync
    KreditEvent pendingKreditEvents[MAX_KREDIT_EVENTS];
    int         pendingKreditEventCount;

    // WiFi state
    bool    wifiConnected;
    char    wifiIp[16];

    // UI preferences
    bool    clickSoundEnabled;   // touch-feedback beep via ES8311
} GameStore;

// ── Global store instance ────────────────────────────────────
extern GameStore g_store;

// ── Lifecycle ────────────────────────────────────────────────
void game_store_init(void);
void game_store_save(void);

// ── Navigation ───────────────────────────────────────────────
void store_navigate(Screen s);

// ── Game flow ────────────────────────────────────────────────
bool store_start_spiel(void);   // deducts credits, builds sequenz
bool store_cancel_spiel(void);  // refunds this game's credits; no result is saved
void store_eintragen(int punkte);
void store_wiederholen(void);
void store_skip_taube(void);
void store_account_acknowledged_clays(int count);
void store_dismiss_resultate(void);

// ── Players ──────────────────────────────────────────────────
void store_create_workers(void);       // call ONCE at boot (before screens are built)
void store_load_portal_spieler(void);  // async via HTTP
void store_apply_portal_roster(const PortalSpieler *spieler, int count);
/** Reconcile persisted lineup IDs after side-effect-free FAT cache restoration. */
void store_reconcile_lineup_after_cache_load(void);
void store_remap_spieler_id(int old_id, int new_id);
void store_add_lokal_spieler(const char *name, int *out_id);
bool store_set_lineup_post(int post, int spieler_id);
void store_clear_lineup(void);
void store_mix_lineup(void);
void store_move_lineup(int post, int direction);
void store_remap_lineup_spieler(int old_id, int new_id);

// ── Credits ──────────────────────────────────────────────────
void store_add_kredite(int spieler_id, int anzahl);
/**
 * Append a signed GRANT adjustment and update the local projection. Negative
 * adjustments are rejected when they would make available credit negative.
 */
bool store_adjust_kredite(int spieler_id, int delta);
bool store_spieler_fuer_tag_aktiv(int spieler_id);
void store_register_spieler_fuer_tag(int spieler_id);
/** Atomically rejects unsettled players or removes the active-day roster entry. */
bool store_remove_spieler_fuer_tag(int spieler_id, char *reason, size_t reason_len);

// ── Sync ─────────────────────────────────────────────────────
/**
 * Queue a full portal sync. Returns false when the persistent sync worker is
 * unavailable or its queue is busy; the caller must not treat it as a sync.
 */
bool store_sync(void);
bool store_sync_is_queued_or_running(void);
bool store_set_auto_sync(bool enabled, uint32_t seconds);
bool store_set_billing_sync(uint32_t seconds);
void store_get_sync_ui_state(SyncUiState *state);
void store_sync_set_ui_ready(void);
/** Worker-only: acquire/release a bounded UI publication window. */
bool store_sync_commit_begin(void);
void store_sync_commit_end(void);
/** UI-only acknowledgement after input has been quiesced for a commit. */
void store_sync_ack_ui_commit(void);

/**
 * Request exactly one initial full sync after this boot's stored-WiFi
 * auto-connect succeeds. Safe to call before the sync worker exists.
 */
void store_sync_after_boot_wifi_connected(void);

// ── Player updates ───────────────────────────────────────────
void store_queue_spieler_update(int spieler_id, const char *name, const char *email, bool portal_aktiv);
bool store_queue_kredit_event(int spieler_id, const char *typ, int anzahl);
int  store_begin_kredit_event_sync(KreditEvent *snapshot, int capacity);
void store_finish_kredit_event_sync(const KreditEvent *snapshot, int count,
                                    bool delivered);
/** Sync worker variant: RAM outbox mutation only; caller owns commit window. */
void store_finish_kredit_event_sync_commit(const KreditEvent *snapshot, int count,
                                           bool delivered);
void store_apply_portal_kredit(int spieler_id, int gewaehrt, int verbraucht);
void store_queue_passwort_reset(int spieler_id);
int  store_pending_update_count(void);

// ── Product sales / ammunition ────────────────────────────────
const Produkt *store_produkt(const char *produkt_code);
/** Cached GAME_CREDIT receipt data required before charging a new game. */
bool store_game_credit_price_valid(void);
bool store_queue_verkauf(int spieler_id, const char *produkt_code, int quantity);
/** Atomically validates and appends every line of a catering basket. */
bool store_queue_catering_basket(int spieler_id, const int *produkt_ids,
                                 const int *quantities, int line_count);
const char *store_last_catering_error(void);
int  store_begin_verkauf_sync(VerkaufEvent *snapshot, int capacity);
void store_finish_verkauf_sync(const VerkaufEvent *snapshot, int count, bool delivered);
/** Sync worker variant: RAM outbox mutation only; caller owns commit window. */
void store_finish_verkauf_sync_commit(const VerkaufEvent *snapshot, int count,
                                      bool delivered);
void store_apply_portal_verkauf(int spieler_id, int produkt_id, int quantity);
void store_replace_produkte(const Produkt *produkte, int count);
void store_remap_verkauf_spieler(int old_id, int new_id);
int  store_munition_cal12(int spieler_id);
int  store_munition_cal20(int spieler_id);

// ── Day settlement / payments ─────────────────────────────────
bool store_queue_payment(int spieler_id);
bool store_payment_pending(int spieler_id);
// Returns false only when marking the selected events in-flight could not be
// durably committed.  On success, count receives the number selected.
bool store_begin_payment_sync(PaymentEvent *snapshot, int capacity, int *count);
// acceptedIds contains only portal-accepted external IDs. Other events remain
// visible and queued (with inFlight cleared), including conflicts.
// Returns false when accepting/clearing the events could not be durably
// committed; in that case the pre-finish queue and roster remain in RAM.
bool store_finish_payment_sync(const PaymentEvent *snapshot, int count,
                               const char *const *acceptedIds, int acceptedCount,
                               const char *error);
/** Sync worker variant: RAM payment/roster mutation only. */
bool store_finish_payment_sync_commit(const PaymentEvent *snapshot, int count,
                                      const char *const *acceptedIds, int acceptedCount,
                                      const char *error);
void store_cache_bill_day(const BillDaySummary *summary);
/** Rebuild the display-only bill projection from the cached portal baseline. */
void store_rebuild_bill_projection(void);

// ── Terminal operating mode / Catering access ─────────────────
bool store_set_catering_pin(const char *pin);
bool store_catering_pin_configured(void);
CateringPinVerifyResult store_verify_catering_pin(const char *pin);
uint32_t store_catering_pin_lockout_remaining(void);
bool store_set_operating_mode(TerminalOperatingMode mode);

// ── Helpers ──────────────────────────────────────────────────
const char *maschine_label(Maschine m);
const char *modus_label(Modus m);
int  store_kredite_verfuegbar(int spieler_id);
/** Total durable, idempotent actions waiting for portal acceptance. */
int store_pending_action_count(void);
