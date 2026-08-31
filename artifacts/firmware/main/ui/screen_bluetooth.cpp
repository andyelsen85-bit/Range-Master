// ============================================================
// Bluetooth screen - BLE HID keyboard pairing
// Mirrors BluetoothScreen.tsx
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_fonts.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "ui_manager.h"
#include "game_store.h"
#include "coprocessor.h"
#include "screen_bluetooth.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_lbl_state;
static lv_obj_t *s_lbl_device;
static lv_obj_t *s_btn_pair;
static lv_obj_t *s_btn_unpair;
static lv_obj_t *s_list_log;

static bool s_advertising = false;

// ── Persistent pairing worker — created once at boot while internal RAM
// is healthy.  xTaskCreate() at tap time fails silently once the screens
// have drained internal RAM (TCBs need internal RAM regardless of stack
// placement).  Same pattern as screen_wifi_create_workers().
static QueueHandle_t s_pair_queue = NULL;

void screen_bluetooth_create_workers(void)
{
    s_pair_queue = xQueueCreate(1, sizeof(uint32_t));
    xTaskCreateWithCaps([](void *arg) {
        uint32_t dummy;
        for (;;) {
            xQueueReceive(s_pair_queue, &dummy, portMAX_DELAY);
            cop_ble_start();
            vTaskDelay(pdMS_TO_TICKS(30000)); // 30s advertising window
            // Check if connected
            lv_async_call([](void *arg2) {
                if (cop_ble_is_connected()) {
                    lv_label_set_text(s_lbl_state, LV_SYMBOL_OK "  TASTATUR VERBUNDEN!");
                    lv_obj_set_style_text_color(s_lbl_state,
                        lv_color_hex(CLR_SUCCESS), 0);
                    lv_label_set_text(s_lbl_device, "BLE HID Keyboard");
                    lv_list_add_text(s_list_log, LV_SYMBOL_OK " TASTATUR VERBUNDEN");
                } else {
                    s_advertising = false;
                    lv_label_set_text(s_lbl_state, "KEINE VERBINDUNG - ERNEUT VERSUCHEN");
                    lv_obj_set_style_text_color(s_lbl_state,
                        lv_color_hex(CLR_MUTED), 0);
                    lv_obj_clear_flag(s_btn_pair, LV_OBJ_FLAG_HIDDEN);
                    lv_obj_add_flag(s_btn_unpair, LV_OBJ_FLAG_HIDDEN);
                }
            }, NULL);
        }
    }, "ble_pair_w", 4096, NULL, 5, NULL,
       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// ── Start advertising ─────────────────────────────────────────
static void pair_cb(lv_event_t *e)
{
    if (!s_pair_queue || s_advertising) return;
    s_advertising = true;
    lv_label_set_text(s_lbl_state, LV_SYMBOL_BLUETOOTH "  SICHTBAR... (AM HANDY KOPPELN)");
    lv_obj_set_style_text_color(s_lbl_state, lv_color_hex(CLR_PRIMARY), 0);
    lv_obj_add_flag(s_btn_pair, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_btn_unpair, LV_OBJ_FLAG_HIDDEN);

    uint32_t trigger = 1;
    xQueueSend(s_pair_queue, &trigger, 0);   // costs no RAM at call time
}

// ── Disconnect ────────────────────────────────────────────────
static void unpair_cb(lv_event_t *e)
{
    cop_ble_stop();
    s_advertising = false;
    lv_label_set_text(s_lbl_state, "NICHT VERBUNDEN");
    lv_obj_set_style_text_color(s_lbl_state, lv_color_hex(CLR_MUTED), 0);
    lv_label_set_text(s_lbl_device, "-");
    lv_obj_clear_flag(s_btn_pair, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_btn_unpair, LV_OBJ_FLAG_HIDDEN);
    lv_list_add_text(s_list_log, LV_SYMBOL_CLOSE " TASTATUR GETRENNT");
}

lv_obj_t *screen_bluetooth_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H);
    screen_base_init(s_scr);   // dark bg, opaque, non-scrollable

    // Header
    lv_obj_t *hdr = lv_obj_create(s_scr);
    lv_obj_set_size(hdr, DISPLAY_LOGICAL_W, 70);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(hdr, 2, 0);
    lv_obj_set_style_border_color(hdr, lv_color_hex(CLR_BORDER), 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(hdr, 20, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_BLUETOOTH "  BLUETOOTH-TASTATUR");
    lv_obj_set_style_text_font(title, UI_FONT_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_PRIMARY), 0);

    lv_obj_t *back = lv_btn_create(hdr);
    lv_obj_add_style(back, &g_style_btn_secondary, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *e) {
        ui_manager_show(SCREEN_EINSTELLUNGEN);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl2 = lv_label_create(back);
    lv_label_set_text(bl2, LV_SYMBOL_LEFT "  ZURÜCK");
    lv_obj_set_style_text_color(bl2, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(bl2);

    // Simulation banner
    lv_obj_t *sim_warn = lv_obj_create(s_scr);
    lv_obj_set_size(sim_warn, DISPLAY_LOGICAL_W - 40, 50);
    lv_obj_align(sim_warn, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_style_bg_color(sim_warn, lv_color_hex(0x78350F), 0);
    lv_obj_set_style_bg_opa(sim_warn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sim_warn, lv_color_hex(CLR_WARN), 0);
    lv_obj_set_style_border_width(sim_warn, 2, 0);
    lv_obj_set_style_radius(sim_warn, 8, 0);
    lv_obj_clear_flag(sim_warn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(sim_warn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sim_warn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sim_warn, 8, 0);

    lv_obj_t *warn_lbl = lv_label_create(sim_warn);
    lv_label_set_text(warn_lbl, LV_SYMBOL_WARNING
        "  BLUETOOTH NUR AM PHYSISCHEN TERMINAL VERFÜGBAR");
    lv_obj_set_style_text_font(warn_lbl, UI_FONT_14, 0);
    lv_obj_set_style_text_color(warn_lbl, lv_color_hex(CLR_WARN), 0);

    // Content
    lv_obj_t *content = lv_obj_create(s_scr);
    lv_obj_set_size(content, DISPLAY_LOGICAL_W - 40,
                    DISPLAY_LOGICAL_H - 70 - 60 - 20);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_set_style_bg_opa(content, LV_OPA_0, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 16, 0);

    // Status card
    lv_obj_t *status_card = lv_obj_create(content);
    lv_obj_add_style(status_card, &g_style_card, 0);
    lv_obj_set_size(status_card, LV_PCT(100), 90);
    lv_obj_set_flex_flow(status_card, LV_FLEX_FLOW_COLUMN);

    s_lbl_state = lv_label_create(status_card);
    lv_label_set_text(s_lbl_state, "NICHT VERBUNDEN");
    lv_obj_set_style_text_font(s_lbl_state, UI_FONT_18, 0);
    lv_obj_set_style_text_color(s_lbl_state, lv_color_hex(CLR_MUTED), 0);

    s_lbl_device = lv_label_create(status_card);
    lv_label_set_text(s_lbl_device, "-");
    lv_obj_set_style_text_font(s_lbl_device, UI_FONT_14, 0);
    lv_obj_set_style_text_color(s_lbl_device, lv_color_hex(CLR_MUTED), 0);

    // Buttons
    lv_obj_t *btn_row = lv_obj_create(content);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_row, 12, 0);

    s_btn_pair = lv_btn_create(btn_row);
    lv_obj_add_style(s_btn_pair, &g_style_btn_primary, 0);
    lv_obj_set_size(s_btn_pair, 200, 52);
    lv_obj_add_event_cb(s_btn_pair, pair_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pl = lv_label_create(s_btn_pair);
    lv_label_set_text(pl, LV_SYMBOL_BLUETOOTH "  KOPPLUNG STARTEN");
    lv_obj_set_style_text_font(pl, UI_FONT_16, 0);
    lv_obj_set_style_text_color(pl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(pl);

    s_btn_unpair = lv_btn_create(btn_row);
    lv_obj_add_style(s_btn_unpair, &g_style_btn_danger, 0);
    lv_obj_set_size(s_btn_unpair, 200, 52);
    lv_obj_add_event_cb(s_btn_unpair, unpair_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_btn_unpair, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *upl = lv_label_create(s_btn_unpair);
    lv_label_set_text(upl, LV_SYMBOL_CLOSE "  TRENNEN");
    lv_obj_set_style_text_font(upl, UI_FONT_16, 0);
    lv_obj_set_style_text_color(upl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(upl);

    // Event log
    lv_obj_t *log_hdr = lv_label_create(content);
    lv_label_set_text(log_hdr, "EREIGNISSE");
    lv_obj_set_style_text_font(log_hdr, UI_FONT_14, 0);
    lv_obj_set_style_text_color(log_hdr, lv_color_hex(CLR_MUTED), 0);

    s_list_log = lv_list_create(content);
    lv_obj_set_size(s_list_log, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_list_log, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_border_color(s_list_log, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_width(s_list_log, 1, 0);
    lv_obj_set_style_radius(s_list_log, 8, 0);
    lv_obj_set_style_text_font(s_list_log, UI_FONT_14, 0);
    lv_list_add_text(s_list_log, "KEINE EREIGNISSE");

    return s_scr;
}

void screen_bluetooth_refresh(void)
{
    if (!s_lbl_state) return;
    if (cop_ble_is_connected()) {
        lv_label_set_text(s_lbl_state, LV_SYMBOL_OK "  TASTATUR VERBUNDEN");
        lv_obj_set_style_text_color(s_lbl_state, lv_color_hex(CLR_SUCCESS), 0);
        lv_obj_add_flag(s_btn_pair, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_btn_unpair, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (!s_advertising) {
            lv_label_set_text(s_lbl_state, "NICHT VERBUNDEN");
            lv_obj_set_style_text_color(s_lbl_state, lv_color_hex(CLR_MUTED), 0);
            lv_obj_clear_flag(s_btn_pair, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_btn_unpair, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
