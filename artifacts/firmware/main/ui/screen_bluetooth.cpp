// ============================================================
// Bluetooth screen - BLE HID keyboard pairing
// Mirrors BluetoothScreen.tsx
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

// ── Start advertising ─────────────────────────────────────────
static void pair_cb(lv_event_t *e)
{
    s_advertising = true;
    lv_label_set_text(s_lbl_state, LV_SYMBOL_BLUETOOTH "  ANNONCIERT... (KOPPEL AM HANDY)");
    lv_obj_set_style_text_color(s_lbl_state, lv_color_hex(CLR_PRIMARY), 0);
    lv_obj_add_flag(s_btn_pair, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_btn_unpair, LV_OBJ_FLAG_HIDDEN);

    xTaskCreate([](void *arg) {
        cop_ble_start();
        vTaskDelay(pdMS_TO_TICKS(30000)); // 30s advertising window
        // Check if connected
        lv_async_call([](void *arg2) {
            if (cop_ble_is_connected()) {
                lv_label_set_text(s_lbl_state, LV_SYMBOL_OK "  KEYBOARD VERBONNEN!");
                lv_obj_set_style_text_color(s_lbl_state,
                    lv_color_hex(CLR_SUCCESS), 0);
                lv_label_set_text(s_lbl_device, "BLE HID Keyboard");
                lv_list_add_text(s_list_log, LV_SYMBOL_OK " KEYBOARD VERBONNEN");
            } else {
                s_advertising = false;
                lv_label_set_text(s_lbl_state, "KENG VERBINDUNG - NEI PROBIEREN");
                lv_obj_set_style_text_color(s_lbl_state,
                    lv_color_hex(CLR_MUTED), 0);
                lv_obj_clear_flag(s_btn_pair, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(s_btn_unpair, LV_OBJ_FLAG_HIDDEN);
            }
        }, NULL);
        vTaskDelete(NULL);
    }, "ble_pair", 4096, NULL, 5, NULL);
}

// ── Disconnect ────────────────────────────────────────────────
static void unpair_cb(lv_event_t *e)
{
    cop_ble_stop();
    s_advertising = false;
    lv_label_set_text(s_lbl_state, "NET VERBONNEN");
    lv_obj_set_style_text_color(s_lbl_state, lv_color_hex(CLR_MUTED), 0);
    lv_label_set_text(s_lbl_device, "-");
    lv_obj_clear_flag(s_btn_pair, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_btn_unpair, LV_OBJ_FLAG_HIDDEN);
    lv_list_add_text(s_list_log, LV_SYMBOL_CLOSE " Keyboard getrennt");
}

lv_obj_t *screen_bluetooth_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

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
    lv_label_set_text(title, LV_SYMBOL_BLUETOOTH "  BLUETOOTH KEYBOARD");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_PRIMARY), 0);

    lv_obj_t *back = lv_btn_create(hdr);
    lv_obj_add_style(back, &g_style_btn_secondary, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *e) {
        ui_manager_show(SCREEN_EINSTELLUNGEN);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl2 = lv_label_create(back);
    lv_label_set_text(bl2, LV_SYMBOL_LEFT "  ZURUCK");
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
        "  BLUETOOTH ASS NEMMEN UM PHYSESCHE TERMINAL VERFUGBAR");
    lv_obj_set_style_text_font(warn_lbl, &lv_font_montserrat_14, 0);
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
    lv_label_set_text(s_lbl_state, "NET VERBONNEN");
    lv_obj_set_style_text_font(s_lbl_state, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_lbl_state, lv_color_hex(CLR_MUTED), 0);

    s_lbl_device = lv_label_create(status_card);
    lv_label_set_text(s_lbl_device, "-");
    lv_obj_set_style_text_font(s_lbl_device, &lv_font_montserrat_14, 0);
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
    lv_label_set_text(pl, LV_SYMBOL_BLUETOOTH "  KOPPELE STARTEN");
    lv_obj_set_style_text_font(pl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(pl);

    s_btn_unpair = lv_btn_create(btn_row);
    lv_obj_add_style(s_btn_unpair, &g_style_btn_danger, 0);
    lv_obj_set_size(s_btn_unpair, 200, 52);
    lv_obj_add_event_cb(s_btn_unpair, unpair_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_btn_unpair, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *upl = lv_label_create(s_btn_unpair);
    lv_label_set_text(upl, LV_SYMBOL_CLOSE "  Trennen");
    lv_obj_set_style_text_font(upl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(upl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(upl);

    // Event log
    lv_obj_t *log_hdr = lv_label_create(content);
    lv_label_set_text(log_hdr, "EVENEMENTER");
    lv_obj_set_style_text_font(log_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(log_hdr, lv_color_hex(CLR_MUTED), 0);

    s_list_log = lv_list_create(content);
    lv_obj_set_size(s_list_log, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_list_log, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_border_color(s_list_log, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_width(s_list_log, 1, 0);
    lv_obj_set_style_radius(s_list_log, 8, 0);
    lv_obj_set_style_text_font(s_list_log, &lv_font_montserrat_14, 0);
    lv_list_add_text(s_list_log, "Keng EVENEMENTER");

    return s_scr;
}

void screen_bluetooth_refresh(void)
{
    if (!s_lbl_state) return;
    if (cop_ble_is_connected()) {
        lv_label_set_text(s_lbl_state, LV_SYMBOL_OK "  KEYBOARD VERBONNEN");
        lv_obj_set_style_text_color(s_lbl_state, lv_color_hex(CLR_SUCCESS), 0);
        lv_obj_add_flag(s_btn_pair, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_btn_unpair, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (!s_advertising) {
            lv_label_set_text(s_lbl_state, "NET VERBONNEN");
            lv_obj_set_style_text_color(s_lbl_state, lv_color_hex(CLR_MUTED), 0);
            lv_obj_clear_flag(s_btn_pair, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_btn_unpair, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
