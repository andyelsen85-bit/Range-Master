// ============================================================
// TrapMaster Firmware — app_main
// Board: Guition JC8012P4A1C-I-W-Y
// ============================================================
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"

#include "app_config.h"
#include "jd9365_panel.h"
#include "gsl3680_touch.h"
#include "game_store.h"
#include "coprocessor.h"
#include "lora_stub.h"
#include "ui_manager.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"

static const char *TAG = "main";

// ── Backlight ────────────────────────────────────────────────
static void backlight_init(void)
{
    // Assignment style avoids C++ designated-initializer field-order errors
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

// ── LVGL tick timer ──────────────────────────────────────────
static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

// ── app_main ─────────────────────────────────────────────────
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "TrapMaster firmware " APP_VERSION " starting...");

    // ── NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ── Backlight
    backlight_init();

    // ── Co-processor (ESP32-C6) UART bridge
    coprocessor_init();

    // ── LoRa UART stub (phase 2 placeholder)
    lora_stub_init();

    // ── Display (JD9365 MIPI-DSI)
    lv_display_t *disp = jd9365_panel_init();
    if (!disp) {
        ESP_LOGE(TAG, "Display init failed — halting");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // ── Touch (GSL3680 I²C)
    gsl3680_touch_init(disp);

    // ── LVGL tick via hardware timer
    esp_timer_create_args_t tick_args = {};
    tick_args.callback              = lvgl_tick_cb;
    tick_args.dispatch_method       = ESP_TIMER_TASK;
    tick_args.name                  = "lvgl_tick";
    tick_args.skip_unhandled_events = true;
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer,
                                             LVGL_TICK_PERIOD_MS * 1000));

    // ── Game store (loads NVS state)
    game_store_init();

    // ── Build all LVGL screens
    ui_manager_init();

    ESP_LOGI(TAG, "Init complete — entering LVGL task loop");

    // ── Main LVGL handler loop (runs on this task)
    while (true) {
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms > LVGL_TASK_MAX_DELAY_MS) delay_ms = LVGL_TASK_MAX_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms > 0 ? delay_ms : 1));
    }
}
