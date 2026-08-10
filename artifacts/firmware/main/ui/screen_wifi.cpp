// ============================================================
// WiFi screen - scan, connect, show IP
// Mirrors WifiScreen.tsx
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_manager.h"
#include "game_store.h"
#include "coprocessor.h"
#include "screen_wifi.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_lbl_status;
static lv_obj_t *s_list_networks;
static lv_obj_t *s_ta_ssid;
static lv_obj_t *s_ta_pass;
static lv_obj_t *s_lbl_ip;
static lv_obj_t *s_btn_connect;
static lv_obj_t *s_kb;

// File-scope scan result buffer - shared between the scan task and the
// lv_async_call lambda that renders the list (static locals inside lambdas
// have no external linkage so extern references to them fail to link).
static char s_scan_names[20][33];
static int  s_scan_count = 0;

// ── Scan networks ─────────────────────────────────────────────
static void scan_cb(lv_event_t *e)
{
    lv_label_set_text(s_lbl_status, LV_SYMBOL_REFRESH " SCANNT...");
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_MUTED), 0);
    lv_obj_clean(s_list_networks);

    // Run scan in background task (cop_wifi_scan blocks up to 30 s then times out)
    ESP_LOGI("wifi_screen", "scan_cb: creating wifi_scan task");
    BaseType_t task_ok = xTaskCreate([](void *arg) {
        ESP_LOGI("wifi_screen", "wifi_scan task: started, calling cop_wifi_scan");
        char names[20][33];
        int count = 0;
        esp_err_t err = cop_wifi_scan(names, 20, &count);
        ESP_LOGI("wifi_screen", "wifi_scan task: cop_wifi_scan returned %s count=%d",
                 esp_err_to_name(err), count);
        // LVGL update must happen in LVGL context; use lv_async_call.
        // Stash results in static file-scope buffers first.
        memcpy(s_scan_names, names, sizeof(s_scan_names));
        s_scan_count = count;

        // Stash the error so the lambda can read it without a capture.
        static esp_err_t s_scan_err;
        s_scan_err = err;

        lv_async_call([](void *arg2) {
            lv_obj_clean(s_list_networks);
            if (s_scan_err == ESP_ERR_TIMEOUT) {
                // Scan timed out — C6 slave not yet flashed or SDIO link issue.
                lv_list_add_text(s_list_networks, LV_SYMBOL_WARNING " SCAN TIMEOUT");
                lv_label_set_text(s_lbl_status,
                    LV_SYMBOL_WARNING " TIMEOUT — C6 net geflasht? tools/flash_c6_slave.sh lufen");
                lv_obj_set_style_text_color(s_lbl_status,
                    lv_color_hex(CLR_DANGER), 0);
                return;
            }
            for (int i = 0; i < s_scan_count; i++) {
                lv_obj_t *btn = lv_list_add_btn(s_list_networks,
                                                LV_SYMBOL_WIFI, s_scan_names[i]);
                lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_color(btn, lv_color_hex(CLR_TEXT), 0);
                lv_obj_add_event_cb(btn, [](lv_event_t *ev) {
                    lv_obj_t *b = lv_event_get_target_obj(ev);
                    const char *net = lv_list_get_btn_text(s_list_networks, b);
                    if (s_ta_ssid) lv_textarea_set_text(s_ta_ssid, net);
                }, LV_EVENT_CLICKED, NULL);
            }
            if (s_scan_count == 0) {
                lv_list_add_text(s_list_networks, "KENG NETZWIERKER FONNT");
            }
            lv_label_set_text(s_lbl_status, "SCAN FAERDEG");
            lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_SUCCESS), 0);
        }, NULL);

        vTaskDelete(NULL);
    }, "wifi_scan", 8192, NULL, 5, NULL);

    if (task_ok != pdPASS) {
        ESP_LOGE("wifi_screen", "scan_cb: xTaskCreate FAILED (pdFAIL) — heap full?");
        lv_label_set_text(s_lbl_status,
            LV_SYMBOL_WARNING " INTERN FEELER: Task erstellen gescheitert");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_DANGER), 0);
    } else {
        ESP_LOGI("wifi_screen", "scan_cb: wifi_scan task created OK");
    }
}

// ── Connect ───────────────────────────────────────────────────
static void connect_cb(lv_event_t *e)
{
    const char *ssid = lv_textarea_get_text(s_ta_ssid);
    const char *pass = lv_textarea_get_text(s_ta_pass);
    if (!ssid || strlen(ssid) == 0) {
        lv_label_set_text(s_lbl_status, "SSID ASS EIDEL");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_DANGER), 0);
        return;
    }

    // Save to store
    strncpy(g_store.wifiSsid, ssid, MAX_SSID_LEN - 1); g_store.wifiSsid[MAX_SSID_LEN - 1] = '\0';
    strncpy(g_store.wifiPass, pass, MAX_PASS_LEN - 1); g_store.wifiPass[MAX_PASS_LEN - 1] = '\0';
    game_store_save();

    lv_label_set_text(s_lbl_status, LV_SYMBOL_REFRESH " VERBANNE...");
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_MUTED), 0);

    // Connect in background
    xTaskCreate([](void *arg) {
        cop_wifi_connect(g_store.wifiSsid, g_store.wifiPass);
        lv_async_call([](void *arg2) {
            bool ok = cop_wifi_is_connected();
            g_store.wifiConnected = ok;
            if (ok) {
                char ip[16];
                cop_wifi_get_ip(ip, sizeof(ip));
                snprintf(g_store.wifiIp, sizeof(g_store.wifiIp), "%s", ip);
                char buf[48];
                snprintf(buf, sizeof(buf), "VERBONNEN! IP: %s", ip);
                lv_label_set_text(s_lbl_status, buf);
                lv_obj_set_style_text_color(s_lbl_status,
                    lv_color_hex(CLR_SUCCESS), 0);
                lv_label_set_text(s_lbl_ip, buf);
            } else {
                lv_label_set_text(s_lbl_status, LV_SYMBOL_WARNING " VERBINDUNG FEHLGESCHLOEN");
                lv_obj_set_style_text_color(s_lbl_status,
                    lv_color_hex(CLR_DANGER), 0);
            }
        }, NULL);
        vTaskDelete(NULL);
    }, "wifi_conn", 8192, NULL, 5, NULL);
}

lv_obj_t *screen_wifi_create(void)
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
    lv_label_set_text(title, LV_SYMBOL_WIFI "  WIFI ASTELLUNGEN");
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

    // Content with flex row
    lv_obj_t *content = lv_obj_create(s_scr);
    lv_obj_set_size(content, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H - 70);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, 70);
    lv_obj_set_style_bg_color(content, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(content, 16, 0);

    // Left: network list + scan
    lv_obj_t *left = lv_obj_create(content);
    lv_obj_set_size(left, 440, LV_PCT(100));
    lv_obj_set_style_bg_opa(left, LV_OPA_0, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 10, 0);

    lv_obj_t *net_hdr = lv_label_create(left);
    lv_label_set_text(net_hdr, "VERFUGBAR NETZWIERKER");
    lv_obj_set_style_text_font(net_hdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(net_hdr, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *scan_btn = lv_btn_create(left);
    lv_obj_add_style(scan_btn, &g_style_btn_secondary, 0);
    lv_obj_set_size(scan_btn, LV_PCT(100), 44);
    lv_obj_add_event_cb(scan_btn, scan_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl2 = lv_label_create(scan_btn);
    lv_label_set_text(sl2, LV_SYMBOL_REFRESH "  Scan");
    lv_obj_set_style_text_color(sl2, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(sl2);

    s_list_networks = lv_list_create(left);
    lv_obj_set_size(s_list_networks, LV_PCT(100), LV_PCT(80));
    lv_obj_set_style_bg_color(s_list_networks, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_bg_opa(s_list_networks, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_list_networks, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_width(s_list_networks, 1, 0);
    lv_obj_set_style_radius(s_list_networks, 8, 0);
    lv_obj_set_style_text_font(s_list_networks, &lv_font_montserrat_14, 0);
    lv_list_add_text(s_list_networks, "SCAN DRECKEN FIR NETZWIERKER ZE SICHEN");

    // Right: SSID/pass/connect
    lv_obj_t *right = lv_obj_create(content);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_style_bg_opa(right, LV_OPA_0, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 12, 0);

    lv_obj_t *ssid_lbl = lv_label_create(right);
    lv_label_set_text(ssid_lbl, "SSID");
    lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(CLR_TEXT), 0);

    s_ta_ssid = lv_textarea_create(right);
    lv_obj_set_size(s_ta_ssid, LV_PCT(100), 50);
    lv_textarea_set_text(s_ta_ssid, g_store.wifiSsid);
    lv_textarea_set_one_line(s_ta_ssid, true);
    lv_obj_set_style_text_font(s_ta_ssid, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(s_ta_ssid, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_text_color(s_ta_ssid, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *pass_lbl = lv_label_create(right);
    lv_label_set_text(pass_lbl, "PASSWUERT");
    lv_obj_set_style_text_font(pass_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pass_lbl, lv_color_hex(CLR_TEXT), 0);

    s_ta_pass = lv_textarea_create(right);
    lv_obj_set_size(s_ta_pass, LV_PCT(100), 50);
    lv_textarea_set_text(s_ta_pass, g_store.wifiPass);
    lv_textarea_set_one_line(s_ta_pass, true);
    lv_textarea_set_password_mode(s_ta_pass, true);
    lv_obj_set_style_text_font(s_ta_pass, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(s_ta_pass, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_text_color(s_ta_pass, lv_color_hex(CLR_TEXT), 0);

    s_btn_connect = lv_btn_create(right);
    lv_obj_add_style(s_btn_connect, &g_style_btn_primary, 0);
    lv_obj_set_size(s_btn_connect, LV_PCT(100), 56);
    lv_obj_add_event_cb(s_btn_connect, connect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(s_btn_connect);
    lv_label_set_text(cl, LV_SYMBOL_WIFI "  VERBANNE");
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(cl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(cl);

    s_lbl_status = lv_label_create(right);
    lv_label_set_text(s_lbl_status, "");
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_14, 0);

    s_lbl_ip = lv_label_create(right);
    if (g_store.wifiConnected) {
        char buf[32];
        snprintf(buf, sizeof(buf), "IP: %s", g_store.wifiIp);
        lv_label_set_text(s_lbl_ip, buf);
        lv_obj_set_style_text_color(s_lbl_ip, lv_color_hex(CLR_SUCCESS), 0);
    } else {
        lv_label_set_text(s_lbl_ip, "NET VERBONNEN");
        lv_obj_set_style_text_color(s_lbl_ip, lv_color_hex(CLR_MUTED), 0);
    }
    lv_obj_set_style_text_font(s_lbl_ip, &lv_font_montserrat_14, 0);

    // ── On-screen keyboard ────────────────────────────────────────
    // Created last so it renders on top of all other content.
    s_kb = lv_keyboard_create(s_scr);
    lv_obj_set_size(s_kb, DISPLAY_LOGICAL_W, LV_SIZE_CONTENT);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

    // "OK" or "Close" key on the keyboard hides it
    lv_obj_add_event_cb(s_kb, [](lv_event_t *e) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_kb, NULL);
    }, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_kb, [](lv_event_t *e) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_kb, NULL);
    }, LV_EVENT_CANCEL, NULL);

    // Tapping a textarea links it and shows the keyboard
    lv_obj_add_event_cb(s_ta_ssid, [](lv_event_t *e) {
        lv_keyboard_set_textarea(s_kb, s_ta_ssid);
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_pass, [](lv_event_t *e) {
        lv_keyboard_set_textarea(s_kb, s_ta_pass);
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, NULL);

    return s_scr;
}

void screen_wifi_refresh(void)
{
    if (!s_ta_ssid) return;
    lv_textarea_set_text(s_ta_ssid, g_store.wifiSsid);
    lv_textarea_set_text(s_ta_pass, g_store.wifiPass);
    if (g_store.wifiConnected) {
        char buf[32];
        snprintf(buf, sizeof(buf), "IP: %s", g_store.wifiIp);
        lv_label_set_text(s_lbl_ip, buf);
        lv_obj_set_style_text_color(s_lbl_ip, lv_color_hex(CLR_SUCCESS), 0);
    } else {
        lv_label_set_text(s_lbl_ip, "NET VERBONNEN");
        lv_obj_set_style_text_color(s_lbl_ip, lv_color_hex(CLR_MUTED), 0);
    }
}
