// ============================================================
// Astellungen screen — settings: API, machines, custom seqs,
//                     WiFi tab, Bluetooth tab, system info
// Mirrors EinstellungenScreen.tsx
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_einstellungen.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_tab_view;

// ── Tab: Portal API ───────────────────────────────────────────
static lv_obj_t *s_ta_url;
static lv_obj_t *s_ta_key;
static lv_obj_t *s_lbl_api_status;

static void save_api_cb(lv_event_t *e)
{
    const char *url = lv_textarea_get_text(s_ta_url);
    const char *key = lv_textarea_get_text(s_ta_key);
    snprintf(g_store.apiUrl, MAX_URL_LEN, "%s", url);
    snprintf(g_store.apiKey, MAX_KEY_LEN, "%s", key);
    game_store_save();
    lv_label_set_text(s_lbl_api_status, LV_SYMBOL_OK " Gespäichert");
    lv_obj_set_style_text_color(s_lbl_api_status, lv_color_hex(CLR_SUCCESS), 0);
}

static lv_obj_t *build_api_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 12, 0);
    lv_obj_set_style_pad_all(parent, 16, 0);

    lv_obj_t *url_lbl = lv_label_create(parent);
    lv_label_set_text(url_lbl, "Portal API URL");
    lv_obj_set_style_text_font(url_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(url_lbl, lv_color_hex(CLR_TEXT), 0);

    s_ta_url = lv_textarea_create(parent);
    lv_obj_set_size(s_ta_url, LV_PCT(100), 50);
    lv_textarea_set_text(s_ta_url, g_store.apiUrl);
    lv_textarea_set_one_line(s_ta_url, true);
    lv_obj_set_style_text_font(s_ta_url, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(s_ta_url, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_text_color(s_ta_url, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *key_lbl = lv_label_create(parent);
    lv_label_set_text(key_lbl, "API Key");
    lv_obj_set_style_text_font(key_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(key_lbl, lv_color_hex(CLR_TEXT), 0);

    s_ta_key = lv_textarea_create(parent);
    lv_obj_set_size(s_ta_key, LV_PCT(100), 50);
    lv_textarea_set_text(s_ta_key, g_store.apiKey);
    lv_textarea_set_one_line(s_ta_key, true);
    lv_textarea_set_password_mode(s_ta_key, true);
    lv_obj_set_style_text_font(s_ta_key, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(s_ta_key, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_text_color(s_ta_key, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *save_btn = lv_btn_create(parent);
    lv_obj_add_style(save_btn, &g_style_btn_primary, 0);
    lv_obj_set_size(save_btn, 160, 44);
    lv_obj_add_event_cb(save_btn, save_api_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(save_btn);
    lv_label_set_text(sl, LV_SYMBOL_SAVE " Späicheren");
    lv_obj_set_style_text_color(sl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(sl);

    s_lbl_api_status = lv_label_create(parent);
    lv_label_set_text(s_lbl_api_status, "");
    lv_obj_set_style_text_font(s_lbl_api_status, &lv_font_montserrat_14, 0);

    return parent;
}

// ── Tab: Maschinnen ───────────────────────────────────────────
static lv_obj_t *s_mach_sw[MASCHINE_COUNT];

static void mach_sw_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *sw = lv_event_get_target_obj(e);
    g_store.maschinenAktiv[idx] = lv_obj_has_state(sw, LV_STATE_CHECKED);
    game_store_save();
}

static lv_obj_t *build_mach_tab(lv_obj_t *parent)
{
    static const char *labels[] = {
        "Maschinn A","Maschinn B","Maschinn C","Maschinn D",
        "Maschinn E","Maschinn F","Maschinn G","Maschinn H (Doublette)"
    };
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 8, 0);
    lv_obj_set_style_pad_all(parent, 16, 0);

    for (int m = 0; m < MASCHINE_COUNT; m++) {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_set_size(row, LV_PCT(100), 50);
        lv_obj_add_style(row, &g_style_card, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, labels[m]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_TEXT), 0);

        lv_obj_t *sw = lv_switch_create(row);
        s_mach_sw[m] = sw;
        if (g_store.maschinenAktiv[m]) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, mach_sw_cb, LV_EVENT_VALUE_CHANGED,
                            (void*)(intptr_t)m);
    }
    return parent;
}

// ── Tab: WiFi ─────────────────────────────────────────────────
static lv_obj_t *build_wifi_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 16, 0);

    lv_obj_t *info = lv_label_create(parent);
    lv_label_set_text(info, "WiFi Astellungen kann op der WiFi-Säit geännert ginn.");
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(CLR_MUTED), 0);

    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_add_style(btn, &g_style_btn_primary, 0);
    lv_obj_set_size(btn, 200, 44);
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        ui_manager_show(SCREEN_WIFI);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, LV_SYMBOL_WIFI "  WiFi Säit");
    lv_obj_set_style_text_color(bl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(bl);
    return parent;
}

// ── Tab: Bluetooth ────────────────────────────────────────────
static lv_obj_t *build_bt_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 16, 0);

    lv_obj_t *info = lv_label_create(parent);
    lv_label_set_text(info, "Bluetooth HID Keyboard Kopplung.");
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(CLR_MUTED), 0);

    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_add_style(btn, &g_style_btn_primary, 0);
    lv_obj_set_size(btn, 240, 44);
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        ui_manager_show(SCREEN_BLUETOOTH);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, LV_SYMBOL_BLUETOOTH "  Bluetooth Säit");
    lv_obj_set_style_text_color(bl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(bl);
    return parent;
}

// ── Tab: System ───────────────────────────────────────────────
static lv_obj_t *build_system_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 16, 0);
    lv_obj_set_style_pad_row(parent, 8, 0);

    static const char *info_rows[] = {
        "Firmware: " APP_VERSION,
        "Board: Guition JC8012P4A1C-I-W-Y",
        "SoC: ESP32-P4NRW32",
        "Koprocessor: ESP32-C6-MINI-1U-N4",
        "Display: 10.1\" MIPI-DSI JD9365 1280×800",
        "Touch: GSL3680 I²C",
        "Portal: " DEFAULT_API_URL,
    };
    for (int i = 0; i < (int)(sizeof(info_rows)/sizeof(info_rows[0])); i++) {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, info_rows[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_MUTED), 0);
    }
    return parent;
}

// ── screen_einstellungen_create ───────────────────────────────
lv_obj_t *screen_einstellungen_create(void)
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
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  ASTELLUNGEN");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_PRIMARY), 0);

    lv_obj_t *back = lv_btn_create(hdr);
    lv_obj_add_style(back, &g_style_btn_secondary, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *e) {
        ui_manager_show(SCREEN_DASHBOARD);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl2 = lv_label_create(back);
    lv_label_set_text(bl2, LV_SYMBOL_HOME "  Zréck");
    lv_obj_set_style_text_color(bl2, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(bl2);

    // Tab view
    s_tab_view = lv_tabview_create(s_scr);
    lv_obj_set_size(s_tab_view, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H - 70);
    lv_obj_align(s_tab_view, LV_ALIGN_TOP_LEFT, 0, 70);
    lv_tabview_set_tab_bar_size(s_tab_view, 48);
    lv_obj_set_style_bg_color(s_tab_view, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_text_font(s_tab_view, &lv_font_montserrat_14, 0);

    lv_obj_t *tab_api  = lv_tabview_add_tab(s_tab_view, "Portal API");
    lv_obj_t *tab_mach = lv_tabview_add_tab(s_tab_view, "Maschinnen");
    lv_obj_t *tab_wifi = lv_tabview_add_tab(s_tab_view, "WiFi");
    lv_obj_t *tab_bt   = lv_tabview_add_tab(s_tab_view, "Bluetooth");
    lv_obj_t *tab_sys  = lv_tabview_add_tab(s_tab_view, "System");

    build_api_tab(tab_api);
    build_mach_tab(tab_mach);
    build_wifi_tab(tab_wifi);
    build_bt_tab(tab_bt);
    build_system_tab(tab_sys);

    return s_scr;
}

void screen_einstellungen_refresh(void)
{
    if (s_ta_url) lv_textarea_set_text(s_ta_url, g_store.apiUrl);
    if (s_ta_key) lv_textarea_set_text(s_ta_key, g_store.apiKey);
    for (int m = 0; m < MASCHINE_COUNT; m++) {
        if (!s_mach_sw[m]) continue;
        if (g_store.maschinenAktiv[m])
            lv_obj_add_state(s_mach_sw[m], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(s_mach_sw[m], LV_STATE_CHECKED);
    }
    if (s_lbl_api_status) lv_label_set_text(s_lbl_api_status, "");
}
