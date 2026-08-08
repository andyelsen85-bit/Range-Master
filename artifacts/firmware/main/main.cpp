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
#include "esp_cache.h"         // esp_cache_msync — flush CPU cache → PSRAM for DMA2D
#include "esp_lcd_mipi_dsi.h"  // esp_lcd_dpi_panel_get_frame_buffer

#include "app_config.h"
#include "jd9365_panel.h"
#include "coprocessor.h"
#include "lora_stub.h"

static const char *TAG = "main";

// ── Backlight ─────────────────────────────────────────────────
static void backlight_init(void)
{
    // Force GPIO high first — rules out LEDC as the failure point.
    // If the screen lights up here, LEDC config is the issue.
    gpio_config_t bl_cfg = {};
    bl_cfg.pin_bit_mask = (1ULL << LCD_BL_PIN);
    bl_cfg.mode         = GPIO_MODE_OUTPUT;
    bl_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    bl_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    bl_cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&bl_cfg);
    gpio_set_level(LCD_BL_PIN, 1);
    ESP_LOGI(TAG, "Backlight GPIO%d forced HIGH", (int)LCD_BL_PIN);

    // Now hand control to LEDC for PWM dimming.
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
    ESP_LOGI(TAG, "Backlight LEDC on (GPIO%d, duty=%d)", (int)LCD_BL_PIN, LCD_BL_DUTY_MAX);
}

// ── Fill the single DPI frame buffer with a solid RGB565 colour ──
static void *s_fb = NULL;   // cached on first call

static void fill_framebuffer(esp_lcd_panel_handle_t panel, uint16_t rgb565)
{
    if (!s_fb) {
        esp_err_t err = esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &s_fb);
        if (err != ESP_OK || !s_fb) {
            ESP_LOGE(TAG, "get_frame_buffer failed (%s)", esp_err_to_name(err));
            return;
        }
        ESP_LOGI(TAG, "Framebuffer @ %p  size=%u bytes  in PSRAM=%s",
                 s_fb,
                 (unsigned)(DISPLAY_H_RES * DISPLAY_V_RES * 2),
                 ((uintptr_t)s_fb >= 0x48000000u) ? "YES" : "NO — check PSRAM init");
    }

    const size_t total_pixels = DISPLAY_H_RES * DISPLAY_V_RES;
    uint32_t word = ((uint32_t)rgb565 << 16) | rgb565;
    uint32_t *p   = (uint32_t *)s_fb;
    for (size_t i = 0; i < total_pixels / 2; i++) p[i] = word;

    // Flush CPU L2 cache → physical PSRAM so DMA2D sees the written pixels.
    // Without this the DPI controller reads stale zeros from PSRAM.
    const size_t fb_bytes = DISPLAY_H_RES * DISPLAY_V_RES * 2;
    esp_cache_msync(s_fb, fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
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
        fill_framebuffer(panel, COLOURS[idx].colour);
        idx = (idx + 1) % NUM_COLOURS;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
