#pragma once
// ============================================================
// TrapMaster game store — C port of emulator gameStore.ts
// ============================================================
#include <stdint.h>
#include <stdbool.h>

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
#define MAX_URL_LEN         128
#define MAX_KEY_LEN         65
#define TM_MAX_SSID_LEN     33
#define MAX_PASS_LEN        64

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
    SCREEN_BLUETOOTH,
    SCREEN_COUNT
} Screen;

typedef enum {
    SYNC_IDLE = 0,
    SYNC_RUNNING,
    SYNC_SUCCESS,
    SYNC_ERROR,
} SyncStatus;

typedef struct {
    int     id;
    char    name[MAX_NAME_LEN];
    int     punkte;
    int     startPosten;   // 1..5
} Spieler;

typedef struct {
    Maschine  maschine;
    bool      isDoublette;   // true if this entry = the 2nd shot of H
} SequenzEintrag;

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
} KreditEvent;

typedef struct {
    int   gewaehrt;
    int   verbraucht;
} KreditStand;

typedef struct {
    char     externalId[40];
    char     datum[11];        // YYYY-MM-DD (local date, for terminal display)
    char     finishedAt[32];   // ISO datetime UTC "YYYY-MM-DDTHH:MM:SS.000Z" sent as API datum
    Modus    modus;
    int      lauf;
    int      taubenProLauf;
    bool     abgeschlossen;
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
    bool    maschinenAktiv[MASCHINE_COUNT];
    char    apiUrl[MAX_URL_LEN];
    char    apiKey[MAX_KEY_LEN];
    char    wifiSsid[TM_MAX_SSID_LEN];
    char    wifiPass[MAX_PASS_LEN];
    Maschine customSequenzen[4][16]; // CUSTOM_1..4 up to 16 machines each
    int      customSequenzLen[4];
    int      customLaeufe[4];        // 1 or 2

    // Active screen
    Screen  screen;

    // In-game
    Spieler spieler[MAX_SPIELER];
    int     spielerCount;
    int     lauf;
    int     taubeIndex;
    int     spielerIndex;
    SequenzEintrag sequenz[MAX_SEQUENZ];
    int     sequenzLen;
    Ergebnis ergebnisse[MAX_ERGEBNISSE];
    int      ergebnisseCount;
    char     spielId[40];

    // Portal cache
    PortalSpieler portalSpieler[MAX_PORTAL_SPIELER];
    int           portalSpielerCount;

    // Sync
    SyncStatus syncStatus;
    char       syncError[128];

    // Credits (today)
    char       kreditDatum[11];    // YYYY-MM-DD
    KreditStand kredite[MAX_PORTAL_SPIELER]; // indexed by portal player id slot
    int         kreditPlayerIds[MAX_PORTAL_SPIELER]; // parallel array of ids

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
void store_eintragen(int punkte);
void store_wiederholen(void);
void store_skip_taube(void);
void store_dismiss_resultate(void);

// ── Players ──────────────────────────────────────────────────
void store_create_workers(void);       // call ONCE at boot (before screens are built)
void store_load_portal_spieler(void);  // async via HTTP
void store_add_lokal_spieler(const char *name, int *out_id);

// ── Credits ──────────────────────────────────────────────────
void store_add_kredite(int spieler_id, int anzahl);
void store_register_spieler_fuer_tag(int spieler_id);

// ── Sync ─────────────────────────────────────────────────────
void store_sync(void);   // pushes pending games + pulls history

// ── Player updates ───────────────────────────────────────────
void store_queue_spieler_update(int spieler_id, const char *name, const char *email, bool portal_aktiv);
void store_queue_kredit_event(int spieler_id, const char *typ, int anzahl);
void store_queue_passwort_reset(int spieler_id);
int  store_pending_update_count(void);

// ── Helpers ──────────────────────────────────────────────────
const char *maschine_label(Maschine m);
const char *modus_label(Modus m);
int  store_kredite_verfuegbar(int spieler_id);
