// ============================================================
// LoRa UART stub — Phase 2 placeholder
// ============================================================
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "lora_stub.h"
#include "app_config.h"

static const char *TAG = "lora_stub";

void lora_stub_init(void)
{
    ESP_LOGI(TAG, "LoRa UART%d stub init (TX=%d RX=%d @%d baud) — phase 2",
             LORA_UART_PORT, LORA_UART_TX, LORA_UART_RX, LORA_UART_BAUD);

    // Reserve the UART pins so phase-2 code just enables the driver.
    uart_config_t cfg = {
        .baud_rate  = LORA_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // Install driver (small buffers; no real traffic yet)
    uart_driver_install(LORA_UART_PORT, 256, 256, 0, NULL, 0);
    uart_param_config(LORA_UART_PORT, &cfg);
    uart_set_pin(LORA_UART_PORT, LORA_UART_TX, LORA_UART_RX,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    ESP_LOGI(TAG, "LoRa UART reserved — no traffic until phase 2");
}

void lora_fire_machine(Maschine m)
{
    // Phase 1: log only
    ESP_LOGI(TAG, "[LORA-STUB] FIRE machine %s (phase 2 not implemented)",
             maschine_label(m));
    // Phase 2: transmit a LoRa packet over LORA_UART_PORT:
    //   char pkt[8];
    //   snprintf(pkt, sizeof(pkt), "F%c\r\n", 'A' + m);
    //   uart_write_bytes(LORA_UART_PORT, pkt, strlen(pkt));
}
