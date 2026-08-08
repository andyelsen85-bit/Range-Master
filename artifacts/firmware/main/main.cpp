// ============================================================
// TrapMaster Firmware — app_main  (Phase 2: LVGL UI live)
// Board: Guition JC8012P4A1C-I-W-Y
//
// Boot sequence
//   1. NVS + game store init
//   2. Backlight on
//   3. Coprocessor UART bridge
//   4. MIPI DSI display hardware init (jd9365_panel_init)
//   5. LVGL v9 init — full-screen PSRAM double-buffer
//   6. GSL3680 touch init + LVGL indev registration
//   7. UI screens built + dashboard shown
//   8. LVGL task loops forever (lv_timer_handler every 5 ms)
// ============================================================
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"   // esp_lcd_panel_draw_bitmap

#include "lvgl.h"

#include "app_config.h"
#include "jd9365_panel.h"
#include "gsl3680_touch.h"
#include "ui_manager.h"
#include "game_store.h"
#include "coprocessor.h"
#include "lora_stub.h"

static const char *TAG = "main";

// ── Backlight ─────────────────────────────────────────────────
static void backlight_init(void)
{
    // Force pin HIGH immediately so the panel isn't dark while LVGL boots.
    gpio_config_t bl_cfg = {};
    bl_cfg.pin_bit_mask  = (1ULL << LCD_BL_PIN);
    bl_cfg.mode          = GPIO_MODE_OUTPUT;
    gpio_config(&bl_cfg);
    gpio_set_level(LCD_BL_PIN, 1);
    ESP_LOGI(TAG, "Backlight GPIO%d HIGH", (int)LCD_BL_PIN);

    // Hand over to LEDC for PWM dimming capability later.
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
    ch.timer_sel  = LCD_BL_LEDC_TIMER;
    ch.duty       = LCD_BL_DUTY_MAX;
    ch.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
    ESP_LOGI(TAG, "Backlight LEDC running (duty=%d/1023)", LCD_BL_DUTY_MAX);
}

// ── LVGL flush callback ───────────────────────────────────────
// Called by LVGL when a frame is rendered.  With full-screen
// double-buffer render mode, LVGL alternates between buf1/buf2
// so the DMA2D copy from the *other* buffer is long finished
// before we touch it again — calling flush_ready() immediately
// is safe (matches the official Guition Arduino demo pattern).
static void lvgl_flush_cb(lv_display_t *disp,
                           const lv_area_t *area,
                           uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel =
        (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    esp_lcd_panel_draw_bitmap(panel,
        area->x1, area->y1,
        area->x2 + 1, area->y2 + 1,
        px_map);

    lv_display_flush_ready(disp);
}

// ── LVGL + UI task ────────────────────────────────────────────
static esp_lcd_panel_handle_t s_panel = NULL;

static void lvgl_task(void *arg)
{
    // ── 1. LVGL core init ─────────────────────────────────────
    lv_init();
    ESP_LOGI(TAG, "LVGL %d.%d.%d initialised",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

    // ── 2. Tick source — esp_timer periodic ───────────────────
    esp_timer_handle_t tick_timer;
    esp_timer_create_args_t tick_args = {};
    tick_args.callback  = [](void*) { lv_tick_inc(LVGL_TICK_PERIOD_MS); };
    tick_args.name      = "lvgl_tick";
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer,
                                             LVGL_TICK_PERIOD_MS * 1000ULL));

    // ── 3. LVGL display — physical 800×1280, SW-rotated to 1280×800 ──
    lv_display_t *disp = lv_display_create(DISPLAY_H_RES, DISPLAY_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    // Full-screen double-buffer in PSRAM.
    // At 800×1280×2 bytes = 2 048 000 bytes (~2 MB) each.
    // With 32 MB hex PSRAM at 200 MHz there is plenty of headroom.
    size_t buf_bytes = (size_t)DISPLAY_H_RES * DISPLAY_V_RES * sizeof(lv_color16_t);
    void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    void *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "LVGL buffer alloc failed (need 2×%u bytes in PSRAM)",
                 (unsigned)buf_bytes);
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "LVGL buffers: buf1=%p  buf2=%p  (%u bytes each)",
             buf1, buf2, (unsigned)buf_bytes);

    lv_display_set_buffers(disp, buf1, buf2, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_user_data(disp, s_panel);

    // 90° software rotation: portrait panel (800×1280) → landscape UI (1280×800)
    lv_display_set_rotation(disp, DISPLAY_ROTATION);
    ESP_LOGI(TAG, "LVGL display created — logical %dx%d (SW rotation 90°)",
             DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H);

    // ── 4. Touch ──────────────────────────────────────────────
    gsl3680_touch_init(disp);

    // ── 5. UI screens ─────────────────────────────────────────
    ui_manager_init();   // builds all screens, shows SCREEN_DASHBOARD

    ESP_LOGI(TAG, "UI ready — entering LVGL loop");

    // ── 6. Main LVGL loop ─────────────────────────────────────
    for (;;) {
        uint32_t delay_ms = lv_timer_handler();
        // lv_timer_handler() returns ms until the next timer fires.
        // Cap at LVGL_TASK_MAX_DELAY_MS so we don't starve other tasks.
        if (delay_ms > LVGL_TASK_MAX_DELAY_MS) delay_ms = LVGL_TASK_MAX_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms > 0 ? delay_ms : 1));
    }
}

// ── app_main ──────────────────────────────────────────────────
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "TrapMaster firmware " APP_VERSION " — Phase 2 LVGL UI");

    // ── NVS ───────────────────────────────────────────────────
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ── Game store (loads settings from NVS) ──────────────────
    game_store_init();

    // ── Backlight ─────────────────────────────────────────────
    backlight_init();

    // ── Co-processor UART bridge ──────────────────────────────
    coprocessor_init();

    // ── LoRa stub (phase 3 placeholder) ───────────────────────
    lora_stub_init();

    // ── Display hardware init ─────────────────────────────────
    s_panel = jd9365_panel_init();
    if (!s_panel) {
        ESP_LOGE(TAG, "Display init failed — halting");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "Display hardware ready");

    // ── LVGL task — pinned to core 1, away from WiFi/BT on core 0 ──
    // Stack 20 KB: LVGL widget rendering and screen builds are deep.
    xTaskCreatePinnedToCore(
        lvgl_task,
        "lvgl",
        20480,   // 20 KB stack
        NULL,
        5,       // priority 5 — above idle, below WiFi/UART tasks
        NULL,
        1        // core 1
    );

    // app_main returns — LVGL task owns the display from here on.
}
