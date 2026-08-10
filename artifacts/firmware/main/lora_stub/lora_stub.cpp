// ============================================================
// LoRa UART stub — Phase 3 placeholder
//
// GPIO17 (LORA_UART_TX) and GPIO18 (LORA_UART_RX) are currently
// used by the ESP32-C6 SDIO interface (D3 and CMD respectively).
// The UART cannot be initialised until phase 3 when the LoRa
// module gets dedicated pins or a separate header board.
// ============================================================
#include "esp_log.h"
#include "lora_stub.h"
#include "app_config.h"

static const char *TAG = "lora_stub";

void lora_stub_init(void)
{
    // GPIO17/18 belong to SDIO — do NOT configure UART here.
    ESP_LOGI(TAG, "LoRa stub: GPIO%d/GPIO%d reserved for SDIO — UART deferred to phase 3",
             LORA_UART_TX, LORA_UART_RX);
}

void lora_fire_machine(Maschine m)
{
    // Phase 1/2: log only
    ESP_LOGI(TAG, "[LORA-STUB] FIRE machine %s (phase 3 not implemented)",
             maschine_label(m));
}
