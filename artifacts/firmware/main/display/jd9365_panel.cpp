// ============================================================
// JD9365 MIPI-DSI panel driver — hardware init only (no LVGL)
// Reference: CelliesProjects/JC8012P4A1-LVGL
//            profi-max/JC8012P4A1_BSP_ESP32P4
// ============================================================
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_ldo_regulator.h"

#include "app_config.h"
#include "jd9365_panel.h"

static const char *TAG = "jd9365";

// ── JD9365 init command sequence ─────────────────────────────
typedef struct {
    uint8_t cmd;
    uint8_t data[64];
    uint8_t data_len;
    uint8_t delay_ms;
} jd9365_cmd_t;

static const jd9365_cmd_t JD9365_INIT[] = {
    // Password / unlock
    {0xE0, {0x00}, 1, 0},
    {0xE1, {0x93}, 1, 0},
    {0xE2, {0x65}, 1, 0},
    {0xE3, {0xF8}, 1, 0},
    {0x80, {0x03}, 1, 0},
    // Page 1
    {0xE0, {0x01}, 1, 0},
    {0x00, {0x00}, 1, 0},
    {0x01, {0x6E}, 1, 0},
    {0x03, {0x00}, 1, 0},
    {0x04, {0x65}, 1, 0},
    {0x17, {0x00}, 1, 0},
    {0x18, {0xAF}, 1, 0},
    {0x19, {0x01}, 1, 0},
    {0x1A, {0x00}, 1, 0},
    {0x1B, {0xAF}, 1, 0},
    {0x1C, {0x01}, 1, 0},
    {0x1F, {0x3E}, 1, 0},
    {0x20, {0x28}, 1, 0},
    {0x21, {0x28}, 1, 0},
    {0x26, {0x76}, 1, 0},
    // Gamma
    {0xE0, {0x02}, 1, 0},
    {0x00, {0x45}, 1, 0},
    {0x01, {0x4D}, 1, 0},
    {0x02, {0x53}, 1, 0},
    {0x03, {0x5A}, 1, 0},
    {0x04, {0x60}, 1, 0},
    {0x05, {0x72}, 1, 0},
    {0x06, {0x65}, 1, 0},
    {0x07, {0x79}, 1, 0},
    {0x08, {0x82}, 1, 0},
    {0x09, {0x1A}, 1, 0},
    {0x0A, {0x23}, 1, 0},
    {0x0B, {0x23}, 1, 0},
    {0x0C, {0x1B}, 1, 0},
    {0x0D, {0x1D}, 1, 0},
    {0x0E, {0x20}, 1, 0},
    {0x0F, {0x13}, 1, 0},
    {0x10, {0x19}, 1, 0},
    {0x11, {0x25}, 1, 0},
    {0x12, {0x29}, 1, 0},
    {0x13, {0x20}, 1, 0},
    {0x14, {0x28}, 1, 0},
    {0x15, {0x6A}, 1, 0},
    {0x16, {0x78}, 1, 0},
    {0x17, {0x45}, 1, 0},
    {0x18, {0x4D}, 1, 0},
    {0x19, {0x53}, 1, 0},
    {0x1A, {0x5A}, 1, 0},
    {0x1B, {0x60}, 1, 0},
    {0x1C, {0x72}, 1, 0},
    {0x1D, {0x65}, 1, 0},
    {0x1E, {0x79}, 1, 0},
    {0x1F, {0x82}, 1, 0},
    {0x20, {0x1A}, 1, 0},
    {0x21, {0x23}, 1, 0},
    {0x22, {0x23}, 1, 0},
    {0x23, {0x1B}, 1, 0},
    {0x24, {0x1D}, 1, 0},
    {0x25, {0x20}, 1, 0},
    {0x26, {0x13}, 1, 0},
    {0x27, {0x19}, 1, 0},
    {0x28, {0x25}, 1, 0},
    {0x29, {0x29}, 1, 0},
    {0x2A, {0x20}, 1, 0},
    {0x2B, {0x28}, 1, 0},
    {0x2C, {0x6A}, 1, 0},
    {0x2D, {0x78}, 1, 0},
    // Page 4
    {0xE0, {0x04}, 1, 0},
    {0x09, {0x11}, 1, 0},
    {0x0E, {0x48}, 1, 0},
    {0x2B, {0x2B}, 1, 0},
    {0x2D, {0x03}, 1, 0},
    {0x2E, {0x44}, 1, 0},
    // Page 0
    {0xE0, {0x00}, 1, 0},
    {0xE6, {0x02}, 1, 0},
    {0xE7, {0x02}, 1, 0},
    // Sleep out — must come before video stream starts; 120 ms mandatory
    {0x11, {0x00}, 0, 120},
    // NOTE: Display ON (0x29) is sent AFTER esp_lcd_panel_init() starts the
    // DPI video stream — many MIPI panels ignore 0x29 if no sync is running.
};
#define JD9365_INIT_LEN (sizeof(JD9365_INIT) / sizeof(JD9365_INIT[0]))

// ── Public init ───────────────────────────────────────────────
esp_lcd_panel_handle_t jd9365_panel_init(void)
{
    ESP_LOGI(TAG, "Initialising JD9365 MIPI-DSI panel %dx%d (raw, no LVGL)",
             DISPLAY_H_RES, DISPLAY_V_RES);

    // Step 0 — D-PHY LDO
    ESP_LOGI(TAG, "Step 0: power up MIPI DSI D-PHY LDO (chan=3, 2500 mV)");
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
    esp_ldo_channel_config_t ldo_cfg = {};
    ldo_cfg.chan_id    = 3;
    ldo_cfg.voltage_mv = 2500;
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi_phy));
    ESP_LOGI(TAG, "Step 0 OK — D-PHY 2.5V rail on");

    // Step 1 — DSI bus
    ESP_LOGI(TAG, "Step 1: esp_lcd_new_dsi_bus (lanes=%d, rate=%d Mbps)",
             MIPI_DSI_LANE_NUM, MIPI_DSI_LANE_BIT_RATE);
    esp_lcd_dsi_bus_handle_t dsi_bus;
    esp_lcd_dsi_bus_config_t bus_cfg = {};
    bus_cfg.bus_id             = 0;
    bus_cfg.num_data_lanes     = MIPI_DSI_LANE_NUM;
    bus_cfg.phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    bus_cfg.lane_bit_rate_mbps = MIPI_DSI_LANE_BIT_RATE;
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &dsi_bus));
    ESP_LOGI(TAG, "Step 1 OK");

    // Step 2 — DBI IO
    ESP_LOGI(TAG, "Step 2: esp_lcd_new_panel_io_dbi");
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_dbi_io_config_t dbi_cfg = {};
    dbi_cfg.virtual_channel = 0;
    dbi_cfg.lcd_cmd_bits    = 8;
    dbi_cfg.lcd_param_bits  = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &io_handle));
    ESP_LOGI(TAG, "Step 2 OK");

    // Step 3 — DPI panel (2 FBs in PSRAM, DMA2D off for diagnostics)
    ESP_LOGI(TAG, "Step 3: esp_lcd_new_panel_dpi (clk=60MHz, fbs=1, dma2d=on)");
    esp_lcd_panel_handle_t panel_handle;
    esp_lcd_dpi_panel_config_t dpi_cfg = {};
    dpi_cfg.dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi_cfg.dpi_clock_freq_mhz = 60;    // 60 MHz nominal; PSRAM at 80 MHz supplies ~160 MB/s, needs ~95 MB/s
    dpi_cfg.virtual_channel    = 0;
    dpi_cfg.pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpi_cfg.num_fbs            = 1;    // single buffer — no swap needed for static test
    dpi_cfg.flags.use_dma2d    = true;   // DMA2D: ESP32-P4 PSRAM→DSI engine, eliminates underrun
    dpi_cfg.video_timing.h_size            = DISPLAY_H_RES;
    dpi_cfg.video_timing.v_size            = DISPLAY_V_RES;
    dpi_cfg.video_timing.hsync_back_porch  = 120;
    dpi_cfg.video_timing.hsync_pulse_width = 20;
    dpi_cfg.video_timing.hsync_front_porch = 40;
    dpi_cfg.video_timing.vsync_back_porch  = 12;
    dpi_cfg.video_timing.vsync_pulse_width = 4;
    dpi_cfg.video_timing.vsync_front_porch = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(dsi_bus, &dpi_cfg, &panel_handle));
    ESP_LOGI(TAG, "Step 3 OK");

    // Step 4 — GPIO hardware reset
    ESP_LOGI(TAG, "Step 4: hardware reset via GPIO%d", (int)LCD_RST_PIN);
    gpio_config_t rst_cfg = {};
    rst_cfg.pin_bit_mask = (1ULL << LCD_RST_PIN);
    rst_cfg.mode         = GPIO_MODE_OUTPUT;
    rst_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    rst_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rst_cfg.intr_type    = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&rst_cfg));
    gpio_set_level(LCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_LOGI(TAG, "Step 4 OK");

    // Step 5 — JD9365 init command sequence
    ESP_LOGI(TAG, "Step 5: sending %d JD9365 init commands", (int)JD9365_INIT_LEN);
    for (int i = 0; i < (int)JD9365_INIT_LEN; i++) {
        const jd9365_cmd_t *c = &JD9365_INIT[i];
        if (c->data_len > 0) {
            esp_lcd_panel_io_tx_param(io_handle, c->cmd, c->data, c->data_len);
        } else {
            esp_lcd_panel_io_tx_param(io_handle, c->cmd, NULL, 0);
        }
        if (c->delay_ms) vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
    }
    ESP_LOGI(TAG, "Step 5 OK");

    // Step 6 — panel init (starts DSI video stream)
    ESP_LOGI(TAG, "Step 6: esp_lcd_panel_init");
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_LOGI(TAG, "Step 6 OK — DSI video stream running");

    // Step 7 — Display ON *after* video stream is live.
    // JD9365 (and most MIPI video-mode panels) ignore 0x29 if sent before
    // the DPI pixel clock is running; the TCON won't enable the LCD cells.
    ESP_LOGI(TAG, "Step 7: Display ON (0x29) — video stream must be running first");
    esp_lcd_panel_io_tx_param(io_handle, 0x29, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "Step 7 OK");

    return panel_handle;
}
