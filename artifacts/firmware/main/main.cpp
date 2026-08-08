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
#include "esp_cache.h"           // esp_cache_msync — write-back CPU cache before DMA reads
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
static void lvgl_flush_cb(lv_display_t *disp,
                           const lv_area_t *area,
                           uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel =
        (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    // ── Diagnostics (first 8 calls per boot) ──────────────────
    static int s_flush_count = 0;
    if (s_flush_count < 8) {
        uint16_t px0 = ((uint16_t *)px_map)[0];
        uint8_t r = (px0 >> 11) & 0x1F;
        uint8_t g = (px0 >> 5)  & 0x3F;
        uint8_t b =  px0        & 0x1F;
        int32_t log_w = lv_display_get_horizontal_resolution(disp);
        int32_t log_h = lv_display_get_vertical_resolution(disp);
        ESP_LOGI("flush", "#%d  area=(%d,%d)-(%d,%d)  px0=0x%04x (R%d G%d B%d)  logical=%dx%d",
                 s_flush_count,
                 area->x1, area->y1, area->x2, area->y2,
                 px0, r, g, b, log_w, log_h);
        s_flush_count++;
    }
    // ──────────────────────────────────────────────────────────

    // Write-back CPU cache to physical PSRAM before DMA2D reads it.
    // LVGL fills px_map via the CPU cache; DMA2D bypasses the cache and
    // reads physical PSRAM directly.  Without msync, DMA2D sees stale
    // data → garbled / striped output.
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    esp_cache_msync(px_map,
                    (size_t)w * h * sizeof(lv_color16_t),
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M);

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

    // LVGL 9 requires set_rotation() BEFORE set_buffers() so the render
    // engine knows the physical layout when it initialises its internal state.
    lv_display_set_rotation(disp, DISPLAY_ROTATION);

    // Partial render mode — the proven pattern for MIPI DSI panels in IDF.
    // RENDER_MODE_FULL with SW rotation does NOT transpose the pixel data
    // before the flush callback; it relies on hardware rotation support which
    // the JD9365 DPI path doesn't expose.  With PARTIAL mode LVGL correctly
    // rotates each dirty tile before calling flush, so draw_bitmap receives
    // already-rotated data in physical (800×1280) coordinates.
    //
    // Two buffers of 1/10 screen area (~200 KB each) allow LVGL to prepare
    // the next tile in buf2 while DMA2D is still copying buf1 to the panel FB.
    size_t buf_bytes = (size_t)DISPLAY_H_RES * (DISPLAY_V_RES / 10)
                       * sizeof(lv_color16_t);   // 800×128×2 = 204 800 B
    void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    void *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "LVGL buffer alloc failed (need 2×%u bytes in PSRAM)",
                 (unsigned)buf_bytes);
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "LVGL buffers: buf1=%p  buf2=%p  (%u bytes each, partial mode)",
             buf1, buf2, (unsigned)buf_bytes);

    lv_display_set_buffers(disp, buf1, buf2, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_user_data(disp, s_panel);

    ESP_LOGI(TAG, "LVGL display created — logical %dx%d (SW rotation 90°, partial mode)",
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
