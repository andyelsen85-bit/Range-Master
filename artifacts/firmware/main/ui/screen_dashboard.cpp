// ============================================================
// Dashboard screen — home screen with nav sidebar + sync panel
// Mirrors DashboardScreen.tsx
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_log.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_dashboard.h"

static const char *TAG = "scr_dashboard";

// Persistent widget handles for refresh
static lv_obj_t *s_scr;
static lv_obj_t *s_lbl_sync_status;
static lv_obj_t *s_lbl_pending;
static lv_obj_t *s_lbl_wifi;
static lv_obj_t *s_history_list;

// ── Sidebar navigation button helper ─────────────────────────
static lv_obj_t *sidebar_btn(lv_obj_t *parent, const char *label,
                              Screen target)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_add_style(btn, &g_style_btn_secondary, 0);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, 56);
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        Screen s = (Screen)(intptr_t)lv_event_get_user_data(e);
        ui_manager_show(s);
    }, LV_EVENT_CLICKED, (void *)(intptr_t)target);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(lbl);
    return btn;
}

// ── Sync button callback ───────────────────────────────────────
static void sync_cb(lv_event_t *e)
{
    store_sync();
    screen_dashboard_refresh();
}

// ── screen_dashboard_create ───────────────────────────────────
lv_obj_t *screen_dashboard_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Header (80px tall)
    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_size(header, DISPLAY_LOGICAL_W, 80);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 2, LV_PART_MAIN);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *club_lbl = lv_label_create(header);
    lv_label_set_text(club_lbl, CLUB_NAME);
    lv_obj_set_style_text_font(club_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(club_lbl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_align(club_lbl, LV_ALIGN_LEFT_MID, 20, 0);

    lv_obj_t *ver_lbl = lv_label_create(header);
    lv_label_set_text(ver_lbl, APP_VERSION);
    lv_obj_set_style_text_font(ver_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ver_lbl, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(ver_lbl, LV_ALIGN_RIGHT_MID, -20, 0);

    // ── Sidebar (240px wide, below header)
    lv_obj_t *sidebar = lv_obj_create(s_scr);
    lv_obj_set_size(sidebar, 240, DISPLAY_LOGICAL_H - 80);
    lv_obj_align(sidebar, LV_ALIGN_TOP_LEFT, 0, 80);
    lv_obj_add_style(sidebar, &g_style_sidebar, 0);
    lv_obj_set_style_border_side(sidebar, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(sidebar, 2, 0);
    lv_obj_set_style_border_color(sidebar, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sidebar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(sidebar, 8, 0);

    sidebar_btn(sidebar, LV_SYMBOL_PLAY "  SPILL STARTEN",   SCREEN_START);
    sidebar_btn(sidebar, LV_SYMBOL_CHARGE " KREDITTER",      SCREEN_KREDITE);
    sidebar_btn(sidebar, LV_SYMBOL_LIST "  SPILLERLËSCHT",   SCREEN_SPILLER);
    sidebar_btn(sidebar, LV_SYMBOL_SETTINGS " ASTELLUNGEN",  SCREEN_EINSTELLUNGEN);
    sidebar_btn(sidebar, LV_SYMBOL_LIST "  SPILLGESCHICHT", SCREEN_GESCHICHTE);

    // ── Main content area
    lv_obj_t *content = lv_obj_create(s_scr);
    lv_obj_set_size(content, DISPLAY_LOGICAL_W - 240, DISPLAY_LOGICAL_H - 80);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 240, 80);
    lv_obj_set_style_bg_color(content, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 20, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 16, 0);

    // Sync status card
    lv_obj_t *sync_card = lv_obj_create(content);
    lv_obj_add_style(sync_card, &g_style_card, 0);
    lv_obj_set_width(sync_card, LV_PCT(100));
    lv_obj_set_height(sync_card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sync_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sync_card, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *sync_col = lv_obj_create(sync_card);
    lv_obj_set_size(sync_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(sync_col, LV_OPA_0, 0);
    lv_obj_set_style_border_width(sync_col, 0, 0);
    lv_obj_set_flex_flow(sync_col, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *sync_title = lv_label_create(sync_col);
    lv_label_set_text(sync_title, "Offline Queue");
    lv_obj_set_style_text_font(sync_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sync_title, lv_color_hex(CLR_TEXT), 0);

    s_lbl_pending = lv_label_create(sync_col);
    lv_label_set_text(s_lbl_pending, "0 Spiller pending");
    lv_obj_set_style_text_font(s_lbl_pending, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_pending, lv_color_hex(CLR_MUTED), 0);

    s_lbl_sync_status = lv_label_create(sync_col);
    lv_label_set_text(s_lbl_sync_status, "—");
    lv_obj_set_style_text_font(s_lbl_sync_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_sync_status, lv_color_hex(CLR_MUTED), 0);

    lv_obj_t *sync_btn = lv_btn_create(sync_card);
    lv_obj_add_style(sync_btn, &g_style_btn_primary, 0);
    lv_obj_add_event_cb(sync_btn, sync_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sync_lbl = lv_label_create(sync_btn);
    lv_label_set_text(sync_lbl, LV_SYMBOL_REFRESH "  Sync");
    lv_obj_set_style_text_color(sync_lbl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(sync_lbl);

    // WiFi status
    s_lbl_wifi = lv_label_create(content);
    lv_label_set_text(s_lbl_wifi, LV_SYMBOL_WIFI "  WiFi: net verbonnen");
    lv_obj_set_style_text_font(s_lbl_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(CLR_MUTED), 0);

    // Recent games list header
    lv_obj_t *hist_hdr = lv_label_create(content);
    lv_label_set_text(hist_hdr, "Rezent Spiller");
    lv_obj_set_style_text_font(hist_hdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hist_hdr, lv_color_hex(CLR_TEXT), 0);

    s_history_list = lv_list_create(content);
    lv_obj_set_width(s_history_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_history_list, 1);
    lv_obj_set_style_bg_color(s_history_list, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_border_width(s_history_list, 0, 0);
    lv_obj_set_style_pad_all(s_history_list, 0, 0);

    return s_scr;
}

// ── screen_dashboard_refresh ──────────────────────────────────
void screen_dashboard_refresh(void)
{
    if (!s_lbl_sync_status) return;

    // Sync status
    char buf[64];
    switch (g_store.syncStatus) {
        case SYNC_IDLE:    lv_label_set_text(s_lbl_sync_status, "Bereet"); break;
        case SYNC_RUNNING: lv_label_set_text(s_lbl_sync_status, LV_SYMBOL_REFRESH " Synciséiert..."); break;
        case SYNC_SUCCESS: lv_label_set_text(s_lbl_sync_status, LV_SYMBOL_OK " Synciséiert"); break;
        case SYNC_ERROR:
            snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " Feeler: %.40s", g_store.syncError);
            lv_label_set_text(s_lbl_sync_status, buf);
            break;
    }

    // Pending games
    snprintf(buf, sizeof(buf), "%d Spiller am Waardraum", g_store.pendingGamesCount);
    lv_label_set_text(s_lbl_pending, buf);

    // WiFi
    if (g_store.wifiConnected) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  WiFi: %s", g_store.wifiIp);
        lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(CLR_SUCCESS), 0);
    } else {
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  WiFi: net verbonnen");
        lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(CLR_MUTED), 0);
    }
    lv_label_set_text(s_lbl_wifi, buf);

    // History list (last 5)
    lv_obj_clean(s_history_list);
    int start = g_store.historyCount - 5;
    if (start < 0) start = 0;
    for (int i = g_store.historyCount - 1; i >= start; i--) {
        const FinishedGame *fg = &g_store.history[i];
        char item[160];
        snprintf(item, sizeof(item), "%s  ·  %s  ·  %s",
                 fg->finishedAt,
                 modus_label(fg->base.modus),
                 fg->spieler_count > 0 ? fg->spielerNamen[0] : "");
        lv_list_add_text(s_history_list, item);
    }
    if (g_store.historyCount == 0) {
        lv_list_add_text(s_history_list, "Keng Spiller nach");
    }
}

void screen_dashboard_tick(void)
{
    // Update WiFi status periodically
    static uint32_t last = 0;
    if (lv_tick_get() - last > 2000) {
        last = lv_tick_get();
        bool was = g_store.wifiConnected;
        // coprocessor will update g_store.wifiConnected via events
        if (was != g_store.wifiConnected) screen_dashboard_refresh();
    }
}
