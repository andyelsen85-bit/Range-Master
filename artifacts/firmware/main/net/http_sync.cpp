// ============================================================
// HTTP sync — PHASE 1 STUB (display bring-up only)
// Full networking implementation restored once display is confirmed.
// ============================================================
#include "http_sync.h"
#include "esp_log.h"

static const char *TAG = "http_sync";

esp_err_t http_push_pending_games(void)  { ESP_LOGW(TAG, "stub"); return ESP_OK; }
esp_err_t http_fetch_spieler(PortalSpieler *, int, int *count) { if (count) *count = 0; return ESP_OK; }
esp_err_t http_fetch_spielhistorie(void) { ESP_LOGW(TAG, "stub"); return ESP_OK; }
esp_err_t http_sync_all(void)            { ESP_LOGW(TAG, "stub"); return ESP_OK; }
