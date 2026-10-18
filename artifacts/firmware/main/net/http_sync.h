#pragma once
#include "esp_err.h"
esp_err_t http_sync_boot(void);
// ============================================================
// Portal API HTTP sync client
// Mirrors emulator gameStore.ts sync logic
// ============================================================
#include "esp_err.h"
#include "game_store.h"
#include <stddef.h>

/** POST /api/sync/spiele — push all pending games. */
esp_err_t http_push_pending_games(void);
// Recovery callers authenticate through storage_admin and never log the PIN.
char *http_pending_games_json(); // heap JSON; caller frees
char *http_pending_game_export(const char *id,const char *pin); // full record and readable JSON
void http_pending_game_export_delivered(const char *id,uint32_t checksum);
esp_err_t http_pending_games_reconcile(const char *pin); // read-only portal query, exact ACKs
bool http_pending_game_prepare_discard(const char *id,const char *pin,char *token,size_t size);
bool http_pending_game_discard(const char *id,const char *pin,const char *token,const char *confirmation);
/** Next full sync requeues completed, archived games from the last seven days. */
void http_request_game_recovery(void);

/** GET /api/portal/spieler-vum-dag — fetch today's registered players. */
esp_err_t http_fetch_spieler(PortalSpieler *out, int max, int *count);

/** GET /api/sync/spiele — pull game history for offline display. */
esp_err_t http_fetch_spielhistorie(void);

/** POST /api/sync/spieler-updates — push queued player edits and password resets. */
esp_err_t http_push_spieler_updates(void);
esp_err_t http_push_kredit_events(void);
esp_err_t http_pull_kredite(void);
esp_err_t http_fetch_produkte(void);
esp_err_t http_push_verkauf_events(void);
esp_err_t http_pull_verkaeufe(void);
/** POST /api/sync/payments — submit durable Paid confirmations. */
esp_err_t http_push_payment_events(void);
/** GET /api/sync/bills/day-summary?datum=YYYY-MM-DD — canonical daily bill pull. */
esp_err_t http_fetch_bill_day_summary(void);

/** Run the full sync sequence (push + pull). */
esp_err_t http_sync_all(void);
/** Push billing outboxes, then refresh today's credits, sales, and bills. */
esp_err_t http_sync_billing(void);

/** Copy the last bounded, credential-free HTTP diagnostic. */
void http_sync_copy_last_error(char *out, size_t out_len);

/** Upload only the versioned operational configuration snapshot. */
esp_err_t http_backup_config(void);

/** Consume a one-time portal approval and atomically apply validated settings. */
esp_err_t http_restore_config(const char *restore_code);
bool http_terminal_id(char *out,size_t size);
bool http_optional_ready(void);
/** Independent optional-channel connection; no shared sync/FIRE lock, 2 s timeout. */
esp_err_t http_optional_json(const char *path,const char *body,char *out,size_t capacity);
