#pragma once
// ============================================================
// Portal API HTTP sync client
// Mirrors emulator gameStore.ts sync logic
// ============================================================
#include "esp_err.h"
#include "game_store.h"
#include <stddef.h>

/** POST /api/sync/spiele — push all pending games. */
esp_err_t http_push_pending_games(void);

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

/** Run the full sync sequence (push + pull). */
esp_err_t http_sync_all(void);

/** Copy the last bounded, credential-free HTTP diagnostic. */
void http_sync_copy_last_error(char *out, size_t out_len);
