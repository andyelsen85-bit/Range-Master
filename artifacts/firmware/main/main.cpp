// ============================================================
// TrapMaster Firmware — app_main
// PHASE 1 RAW FRAMEBUFFER TEST (no LVGL)
// Board: Guition JC8012P4A1C-I-W-Y
//
// After display init, fills both DPI frame buffers with cycling
// solid colours (red → green → blue → white → black, 2 s each).
// A solid colour on screen proves the DSI pipeline is alive.
// ============================================================
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"  // esp_lcd_dpi_panel_get_frame_buffer

#include "app_config.h"
#include "jd9365_panel.h"
#include "coprocessor.h"
#include "lora_stub.h"

static const char *TAG = "main";

// ── Backlight ─────────────────────────────────────────────────
static void backlight_init(void)
{
    ledc_timer_config_t timer = {};
    timer.speed_mode      = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.freq_hz         = 5000;
    timer.timer_num       = LCD_BL_LEDC_TIMER;
    timer.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t ch = {};
    ch.gpio_num   = LCD_BL_PIN;
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel    = LCD_BL_LEDC_CHANNEL;
    ch.intr_type  = LEDC_INTR_DISABLE;
    ch.timer_sel  = LCD_BL_LEDC_TIMER;
    ch.duty       = LCD_BL_DUTY_MAX;
    ch.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
    ESP_LOGI(TAG, "Backlight on");
}

// ── Fill both DPI frame buffers with a solid RGB565 colour ───
// Writing both FBs avoids a flash of old content during the
// buffer swap that the DPI driver performs on each vsync.
static void fill_framebuffers(esp_lcd_panel_handle_t panel,
                              uint16_t rgb565)
{
    void *fb0 = NULL, *fb1 = NULL;
    // Retrieve both frame buffer pointers managed by the DPI driver.
    esp_err_t err = esp_lcd_dpi_panel_get_frame_buffer(panel, 2, &fb0, &fb1);
    if (err != ESP_OK || !fb0 || !fb1) {
        ESP_LOGE(TAG, "get_frame_buffer failed (%s)", esp_err_to_name(err));
        return;
    }

    const size_t total_pixels = DISPLAY_H_RES * DISPLAY_V_RES;

    // Write 2 pixels per 32-bit store for speed (PSRAM prefers wider bursts).
    uint32_t word = ((uint32_t)rgb565 << 16) | rgb565;
    uint32_t *p0  = (uint32_t *)fb0;
    uint32_t *p1  = (uint32_t *)fb1;
    for (size_t i = 0; i < total_pixels / 2; i++) {
        p0[i] = word;
        p1[i] = word;
    }
    // Odd pixel (1280×800 = 1,024,000 pixels — even, so loop covers all)
}

// ── app_main ──────────────────────────────────────────────────
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "TrapMaster firmware " APP_VERSION
             " — PHASE 1 RAW FRAMEBUFFER TEST");

    // ── NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ── Backlight
    backlight_init();

    // ── Co-processor (ESP32-C6) UART bridge
    coprocessor_init();

    // ── LoRa stub (phase 2 placeholder)
    lora_stub_init();

    // ── Display — hardware init only, no LVGL
    esp_lcd_panel_handle_t panel = jd9365_panel_init();
    if (!panel) {
        ESP_LOGE(TAG, "Display init failed — halting");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Display init OK — starting raw framebuffer colour cycle");
    ESP_LOGI(TAG, "Expected: screen changes colour every 2 s");
    ESP_LOGI(TAG, "  RED → GREEN → BLUE → WHITE → BLACK → repeat");

    // RGB565 colour table
    static const struct { uint16_t colour; const char *name; } COLOURS[] = {
        { 0xF800, "RED"   },
        { 0x07E0, "GREEN" },
        { 0x001F, "BLUE"  },
        { 0xFFFF, "WHITE" },
        { 0x0000, "BLACK" },
    };
    const int NUM_COLOURS = sizeof(COLOURS) / sizeof(COLOURS[0]);

    int idx = 0;
    for (;;) {
        ESP_LOGI(TAG, "Filling framebuffer: %s (0x%04X)",
                 COLOURS[idx].name, COLOURS[idx].colour);
        fill_framebuffers(panel, COLOURS[idx].colour);
        idx = (idx + 1) % NUM_COLOURS;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
