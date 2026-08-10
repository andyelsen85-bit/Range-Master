// ============================================================
// WiFi screen - scan, connect, show IP
// Mirrors WifiScreen.tsx
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
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

// ── Persistent worker tasks — created once at boot while RAM is healthy ───
// Internal RAM is exhausted (~52 bytes) by the time the WiFi screen is
// first shown (SDIO + LVGL 10 screens consume the entire ~93 KB budget).
// Any xTaskCreate() call at tap time fails because FreeRTOS TCBs need
// internal RAM regardless of where the stack lives.
// Pattern: create ONE long-lived task per operation at boot; scan_cb /
// connect_cb just queue-send (queue send costs no allocations).
static QueueHandle_t     s_scan_queue = NULL;
static QueueHandle_t     s_conn_queue = NULL;
static volatile bool     s_scan_busy  = false;

// Shared scan result buffers (written by worker, read by lv_async_call).
static char      s_scan_names[20][33];
static int       s_scan_count = 0;
static esp_err_t s_scan_err   = ESP_OK;

void screen_wifi_create_workers(void)
{
    // Scan worker — blocks on queue, runs cop_wifi_scan, notifies LVGL.
    s_scan_queue = xQueueCreate(1, sizeof(uint32_t));
    xTaskCreateWithCaps([](void *arg) {
        uint32_t dummy;
        for (;;) {
            xQueueReceive(s_scan_queue, &dummy, portMAX_DELAY);
            char names[20][33];
            int count = 0;
            s_scan_err = cop_wifi_scan(names, 20, &count);
            memcpy(s_scan_names, names, sizeof(s_scan_names));
            s_scan_count = count;
            lv_async_call([](void *) {
                if (!s_list_networks) return;
                lv_obj_clean(s_list_networks);
                if (s_scan_err == ESP_ERR_TIMEOUT) {
                    lv_list_add_text(s_list_networks,
                                     LV_SYMBOL_WARNING " SCAN TIMEOUT");
                    lv_label_set_text(s_lbl_status,
                        LV_SYMBOL_WARNING " TIMEOUT — SDIO Verbindung pruefen");
                    lv_obj_set_style_text_color(s_lbl_status,
                        lv_color_hex(CLR_DANGER), 0);
                    s_scan_busy = false;
                    return;
                }
                for (int i = 0; i < s_scan_count; i++) {
                    lv_obj_t *btn = lv_list_add_btn(s_list_networks,
                                                    LV_SYMBOL_WIFI, s_scan_names[i]);
                    // Dark background so text is visible regardless of theme
                    lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_CARD), 0);
                    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
                    lv_obj_set_style_text_font(btn, &lv_font_montserrat_14, 0);
                    lv_obj_set_style_text_color(btn, lv_color_hex(CLR_TEXT), 0);
                    // Pressed highlight
                    lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_PRIMARY),
                                              LV_STATE_PRESSED);
                    lv_obj_add_event_cb(btn, [](lv_event_t *ev) {
                        lv_obj_t *b = lv_event_get_target_obj(ev);
                        const char *net = lv_list_get_btn_text(s_list_networks, b);
                        if (s_ta_ssid) lv_textarea_set_text(s_ta_ssid, net);
                    }, LV_EVENT_CLICKED, NULL);
                }
                if (s_scan_count == 0)
                    lv_list_add_text(s_list_networks, "KENG NETZWIERKER FONNT");
                if (s_lbl_status) {
                    lv_label_set_text(s_lbl_status, "SCAN FAERDEG");
                    lv_obj_set_style_text_color(s_lbl_status,
                        lv_color_hex(CLR_SUCCESS), 0);
                }
                s_scan_busy = false;   // allow next tap
            }, NULL);
        }
    }, "wifi_scan_w", 8192, NULL, 5, NULL, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    // Connect worker — blocks on queue, runs cop_wifi_connect, notifies LVGL.
    s_conn_queue = xQueueCreate(1, sizeof(uint32_t));
    xTaskCreateWithCaps([](void *arg) {
        uint32_t dummy;
        for (;;) {
            xQueueReceive(s_conn_queue, &dummy, portMAX_DELAY);
            cop_wifi_connect(g_store.wifiSsid, g_store.wifiPass);
            lv_async_call([](void *) {
                bool ok = cop_wifi_is_connected();
                g_store.wifiConnected = ok;
                if (ok) {
                    char ip[16];
                    cop_wifi_get_ip(ip, sizeof(ip));
                    snprintf(g_store.wifiIp, sizeof(g_store.wifiIp), "%s", ip);
                    char buf[64];
                    snprintf(buf, sizeof(buf), "VERBONNEN! IP: %s", ip);
                    if (s_lbl_status) {
                        lv_label_set_text(s_lbl_status, buf);
                        lv_obj_set_style_text_color(s_lbl_status,
                            lv_color_hex(CLR_SUCCESS), 0);
                    }
                    // Show config URL so operator knows where to go
                    char url_buf[48];
                    snprintf(url_buf, sizeof(url_buf), "http://%s/", ip);
                    if (s_lbl_ip) {
                        lv_label_set_text(s_lbl_ip, url_buf);
                        lv_obj_set_style_text_color(s_lbl_ip,
                            lv_color_hex(CLR_PRIMARY), 0);
                    }
                } else {
                    if (s_lbl_status) {
                        lv_label_set_text(s_lbl_status,
                            LV_SYMBOL_WARNING " VERBINDUNG FEHLGESCHLOEN");
                        lv_obj_set_style_text_color(s_lbl_status,
                            lv_color_hex(CLR_DANGER), 0);
                    }
                }
            }, NULL);
        }
    }, "wifi_conn_w", 8192, NULL, 5, NULL, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    ESP_LOGI("wifi_screen", "Workers created. Internal RAM remaining: %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

// ── Scan button — just triggers the persistent worker ─────────
static void scan_cb(lv_event_t *e)
{
    if (!s_scan_queue) return;
    if (s_scan_busy) return;   // ignore taps while scan is in progress
    s_scan_busy = true;
    lv_label_set_text(s_lbl_status, LV_SYMBOL_REFRESH " SCANNT...");
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_MUTED), 0);
    lv_obj_clean(s_list_networks);
    uint32_t trigger = 1;
    xQueueSend(s_scan_queue, &trigger, 0);   // costs no RAM at call time
}

// ── Connect button — just triggers the persistent worker ──────
static void connect_cb(lv_event_t *e)
{
    const char *ssid = lv_textarea_get_text(s_ta_ssid);
    const char *pass = lv_textarea_get_text(s_ta_pass);
    if (!ssid || strlen(ssid) == 0) {
        lv_label_set_text(s_lbl_status, "SSID ASS EIDEL");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_DANGER), 0);
        return;
    }
    strncpy(g_store.wifiSsid, ssid, MAX_SSID_LEN - 1);
    g_store.wifiSsid[MAX_SSID_LEN - 1] = '\0';
    strncpy(g_store.wifiPass, pass, MAX_PASS_LEN - 1);
    g_store.wifiPass[MAX_PASS_LEN - 1] = '\0';
    game_store_save();
    lv_label_set_text(s_lbl_status, LV_SYMBOL_REFRESH " VERBANNE...");
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_MUTED), 0);
    if (s_conn_queue) {
        uint32_t trigger = 1;
        xQueueSend(s_conn_queue, &trigger, 0);
    }
}

lv_obj_t *screen_wifi_create(void)
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
    // LV_SIZE_CONTENT computes an oversized height on this 1280×800 display,
    // pushing all but the top row off-screen.  Fix with an explicit height:
    // 320 px ≈ 4 key-rows × 80 px, leaving ~410 px of form area above.
    lv_obj_set_size(s_kb, DISPLAY_LOGICAL_W, 320);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

    // Explicit dark-theme styling — without this the default LVGL theme
    // renders white keys on white background (invisible on this display).
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_kb, 0, 0);
    // Key buttons
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(CLR_BORDER), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_kb, lv_color_hex(CLR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_kb, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_kb, lv_color_hex(CLR_BG), LV_PART_ITEMS);
    // Pressed state
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(CLR_PRIMARY),
                              (lv_style_selector_t)((uint32_t)LV_PART_ITEMS | (uint32_t)LV_STATE_PRESSED));
    lv_obj_set_style_text_color(s_kb, lv_color_hex(0xFFFFFF),
                                (lv_style_selector_t)((uint32_t)LV_PART_ITEMS | (uint32_t)LV_STATE_PRESSED));

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
        char buf[48];
        snprintf(buf, sizeof(buf), "http://%s/", g_store.wifiIp);
        lv_label_set_text(s_lbl_ip, buf);
        lv_obj_set_style_text_color(s_lbl_ip, lv_color_hex(CLR_PRIMARY), 0);
    } else {
        lv_label_set_text(s_lbl_ip, "NET VERBONNEN");
        lv_obj_set_style_text_color(s_lbl_ip, lv_color_hex(CLR_MUTED), 0);
    }
}
