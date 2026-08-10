// ============================================================
// Dashboard screen - home screen with game history + nav sidebar
// Mirrors DashboardScreen.tsx layout:
//   LEFT  960px - "Leschte SPILLER" game history card
//   RIGHT 320px - navigation buttons + offline queue panel
//   TOP    80px - header with club name + WiFi
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "esp_log.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_dashboard.h"


// Persistent widget handles for screen_dashboard_refresh()
static lv_obj_t *s_scr;
static lv_obj_t *s_lbl_sync_status;
static lv_obj_t *s_lbl_pending;
static lv_obj_t *s_lbl_wifi;
static lv_obj_t *s_history_list;

#define SIDEBAR_W   320
#define HEADER_H     80
#define CONTENT_W   (DISPLAY_LOGICAL_W - SIDEBAR_W)
#define CONTENT_H   (DISPLAY_LOGICAL_H - HEADER_H)

// ── Large primary nav button (full sidebar width) ─────────────
static lv_obj_t *big_btn(lv_obj_t *parent, const char *sym,
                          const char *label, int h, Screen target)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_add_style(btn, &g_style_btn_primary, 0);
    lv_obj_set_size(btn, LV_PCT(100), h);
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        Screen s = (Screen)(intptr_t)lv_event_get_user_data(e);
        ui_manager_show(s);
    }, LV_EVENT_CLICKED, (void *)(intptr_t)target);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn, 6, 0);

    lv_obj_t *ic = lv_label_create(btn);
    lv_label_set_text(ic, sym);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(ic, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_TEXT), 0);
    return btn;
}

// ── Secondary nav button (fixed pixel width) ──────────────────
static lv_obj_t *sec_btn(lv_obj_t *parent, const char *sym,
                          const char *label, int w, int h, Screen target)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_add_style(btn, &g_style_btn_secondary, 0);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        Screen s = (Screen)(intptr_t)lv_event_get_user_data(e);
        ui_manager_show(s);
    }, LV_EVENT_CLICKED, (void *)(intptr_t)target);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn, 4, 0);

    lv_obj_t *ic = lv_label_create(btn);
    lv_label_set_text(ic, sym);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(ic, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_TEXT), 0);
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

    // ── Header bar (full width, 80 px tall) ───────────────────
    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_size(header, DISPLAY_LOGICAL_W, HEADER_H);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(header, lv_color_hex(CLR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_pad_left(header, 32, 0);
    lv_obj_set_style_pad_right(header, 32, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *club_lbl = lv_label_create(header);
    lv_label_set_text(club_lbl, CLUB_NAME);
    lv_obj_set_style_text_font(club_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(club_lbl, lv_color_hex(CLR_PRIMARY), 0);
    lv_obj_align(club_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // WiFi status (right side of header)
    s_lbl_wifi = lv_label_create(header);
    lv_label_set_text(s_lbl_wifi, LV_SYMBOL_WIFI "  NET VERBONNEN");
    lv_obj_set_style_text_font(s_lbl_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(s_lbl_wifi, LV_ALIGN_RIGHT_MID, 0, 0);

    // ── Left content area (game history) ──────────────────────
    lv_obj_t *content = lv_obj_create(s_scr);
    lv_obj_set_size(content, CONTENT_W, CONTENT_H);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, HEADER_H);
    lv_obj_set_style_bg_color(content, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 24, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Card inside the content area
    lv_obj_t *hist_card = lv_obj_create(content);
    lv_obj_set_size(hist_card, LV_PCT(100), LV_PCT(100));
    lv_obj_add_style(hist_card, &g_style_card, 0);
    lv_obj_set_style_border_width(hist_card, 2, 0);
    lv_obj_set_style_pad_all(hist_card, 24, 0);
    lv_obj_set_style_pad_row(hist_card, 12, 0);
    lv_obj_set_flex_flow(hist_card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *hist_hdr = lv_label_create(hist_card);
    lv_label_set_text(hist_hdr, "LESCHTE SPILLER");
    lv_obj_set_style_text_font(hist_hdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(hist_hdr, lv_color_hex(CLR_MUTED), 0);
    lv_obj_set_style_text_letter_space(hist_hdr, 2, 0);

    s_history_list = lv_list_create(hist_card);
    lv_obj_set_size(s_history_list, LV_PCT(100), 0);
    lv_obj_set_flex_grow(s_history_list, 1);
    lv_obj_set_style_bg_opa(s_history_list, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_history_list, 0, 0);
    lv_obj_set_style_pad_all(s_history_list, 0, 0);

    // ── Right sidebar ─────────────────────────────────────────
    lv_obj_t *sidebar = lv_obj_create(s_scr);
    lv_obj_set_size(sidebar, SIDEBAR_W, CONTENT_H);
    lv_obj_align(sidebar, LV_ALIGN_TOP_RIGHT, 0, HEADER_H);
    lv_obj_add_style(sidebar, &g_style_sidebar, 0);
    lv_obj_set_style_border_side(sidebar, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(sidebar, 2, 0);
    lv_obj_set_style_border_color(sidebar, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_pad_all(sidebar, 16, 0);
    lv_obj_set_style_pad_row(sidebar, 10, 0);
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sidebar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Large primary "SPILL START" button (140 px tall)
    big_btn(sidebar, LV_SYMBOL_PLAY, "SPILL START", 140, SCREEN_START);

    // 2. "SPILLER VUM DAG" (credits) - full width, 88 px
    sec_btn(sidebar, LV_SYMBOL_CHARGE, "SPILLER VUM DAG",
            SIDEBAR_W - 32, 88, SCREEN_KREDITE);

    // 3. Row: "SPILLER" | "ASTELLUNGEN" side-by-side
    // Inner width = SIDEBAR_W - 2*pad(16) = 288. Half = (288-8)/2 = 140 px each.
    lv_obj_t *row = lv_obj_create(sidebar);
    lv_obj_set_size(row, SIDEBAR_W - 32, 88);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    sec_btn(row, LV_SYMBOL_LIST,     "SPILLER",    140, 88, SCREEN_SPILLER);
    sec_btn(row, LV_SYMBOL_SETTINGS, "ASTELLUNGEN",140, 88, SCREEN_EINSTELLUNGEN);

    // 4. "SPILLGESCHICHT" - muted ghost style
    lv_obj_t *hist_btn = lv_btn_create(sidebar);
    lv_obj_set_size(hist_btn, SIDEBAR_W - 32, 68);
    lv_obj_set_style_bg_color(hist_btn, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_bg_opa(hist_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hist_btn, 0, 0);
    lv_obj_set_style_radius(hist_btn, 8, 0);
    lv_obj_set_flex_flow(hist_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hist_btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hist_btn, 10, 0);
    lv_obj_add_event_cb(hist_btn, [](lv_event_t *) {
        ui_manager_show(SCREEN_GESCHICHTE);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hist_ic = lv_label_create(hist_btn);
    lv_label_set_text(hist_ic, LV_SYMBOL_FILE);
    lv_obj_set_style_text_font(hist_ic, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(hist_ic, lv_color_hex(CLR_MUTED), 0);

    lv_obj_t *hist_lbl = lv_label_create(hist_btn);
    lv_label_set_text(hist_lbl, "SPILLGESCHICHT");
    lv_obj_set_style_text_font(hist_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hist_lbl, lv_color_hex(CLR_MUTED), 0);

    // 5. Offline Queue panel - flex-grow fills the remaining space
    lv_obj_t *queue = lv_obj_create(sidebar);
    lv_obj_set_flex_grow(queue, 1);
    lv_obj_set_width(queue, SIDEBAR_W - 32);
    lv_obj_set_style_bg_color(queue, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_bg_opa(queue, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(queue, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_width(queue, 2, 0);
    lv_obj_set_style_radius(queue, 10, 0);
    lv_obj_set_style_pad_all(queue, 14, 0);
    lv_obj_set_style_pad_row(queue, 8, 0);
    lv_obj_set_flex_flow(queue, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(queue, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(queue, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *q_hdr = lv_label_create(queue);
    lv_label_set_text(q_hdr, "OFFLINE QUEUE");
    lv_obj_set_style_text_font(q_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(q_hdr, lv_color_hex(CLR_MUTED), 0);
    lv_obj_set_style_text_letter_space(q_hdr, 1, 0);

    s_lbl_pending = lv_label_create(queue);
    lv_label_set_text(s_lbl_pending, "0 SPILLER AM WAARDRAUM");
    lv_obj_set_style_text_font(s_lbl_pending, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_pending, lv_color_hex(CLR_TEXT), 0);

    s_lbl_sync_status = lv_label_create(queue);
    lv_label_set_text(s_lbl_sync_status, "BEREET");
    lv_obj_set_style_text_font(s_lbl_sync_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_sync_status, lv_color_hex(CLR_MUTED), 0);

    lv_obj_t *sync_btn = lv_btn_create(queue);
    lv_obj_add_style(sync_btn, &g_style_btn_primary, 0);
    lv_obj_set_size(sync_btn, LV_PCT(100), 50);
    lv_obj_add_event_cb(sync_btn, sync_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sync_lbl = lv_label_create(sync_btn);
    lv_label_set_text(sync_lbl, LV_SYMBOL_REFRESH "  ALLES SYNCEN");
    lv_obj_set_style_text_font(sync_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sync_lbl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(sync_lbl);

    return s_scr;
}

// ── screen_dashboard_refresh ──────────────────────────────────
void screen_dashboard_refresh(void)
{
    if (!s_lbl_sync_status) return;

    // Sync status label
    char buf[64];
    switch (g_store.syncStatus) {
        case SYNC_IDLE:
            lv_label_set_text(s_lbl_sync_status, "BEREET");
            break;
        case SYNC_RUNNING:
            lv_label_set_text(s_lbl_sync_status,
                              LV_SYMBOL_REFRESH " SYNCISIERT...");
            break;
        case SYNC_SUCCESS:
            lv_label_set_text(s_lbl_sync_status,
                              LV_SYMBOL_OK " SYNCISIERT");
            break;
        case SYNC_ERROR:
            snprintf(buf, sizeof(buf),
                     LV_SYMBOL_WARNING " Feeler: %.40s", g_store.syncError);
            lv_label_set_text(s_lbl_sync_status, buf);
            break;
    }

    // Pending games
    snprintf(buf, sizeof(buf), "%d SPILLER AM WAARDRAUM",
             g_store.pendingGamesCount);
    lv_label_set_text(s_lbl_pending, buf);

    // WiFi
    if (g_store.wifiConnected) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  %s", g_store.wifiIp);
        lv_label_set_text(s_lbl_wifi, buf);
        lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(CLR_SUCCESS), 0);
    } else {
        lv_label_set_text(s_lbl_wifi, LV_SYMBOL_WIFI "  NET VERBONNEN");
        lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(CLR_MUTED), 0);
    }

    // History list (most-recent 5 entries) - proper-height rows
    lv_obj_clean(s_history_list);
    if (g_store.historyCount == 0) {
        lv_obj_t *empty = lv_label_create(s_history_list);
        lv_label_set_text(empty, "KENG SPILLER NACH");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(CLR_MUTED), 0);
    } else {
        int start = g_store.historyCount - 5;
        if (start < 0) start = 0;
        for (int i = g_store.historyCount - 1; i >= start; i--) {
            const FinishedGame *fg = &g_store.history[i];

            // Find winner name
            int best_pts = -1, best_id = -1;
            for (int t = 0; t < fg->base.teilnahmen_count; t++) {
                if (fg->base.teilnahmen[t].punkte > best_pts) {
                    best_pts = fg->base.teilnahmen[t].punkte;
                    best_id  = fg->base.teilnahmen[t].spielerId;
                }
            }
            const char *winner = "---";
            for (int j = 0; j < fg->spieler_count; j++) {
                if (fg->spielerIds[j] == best_id) { winner = fg->spielerNamen[j]; break; }
            }

            lv_obj_t *row = lv_obj_create(s_history_list);
            lv_obj_set_size(row, LV_PCT(100), 52);
            lv_obj_set_style_bg_color(row, lv_color_hex(CLR_CARD), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(row, lv_color_hex(CLR_BORDER), 0);
            lv_obj_set_style_border_width(row, 1, 0);
            lv_obj_set_style_radius(row, 6, 0);
            lv_obj_set_style_pad_hor(row, 14, 0);
            lv_obj_set_style_pad_ver(row, 0, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            char ts[24]; snprintf(ts, sizeof(ts), "%s", fg->finishedAt);
            lv_obj_t *ts_lbl = lv_label_create(row);
            lv_label_set_text(ts_lbl, ts);
            lv_obj_set_style_text_font(ts_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(ts_lbl, lv_color_hex(CLR_MUTED), 0);
            lv_obj_set_width(ts_lbl, 180);

            lv_obj_t *mode_lbl = lv_label_create(row);
            lv_label_set_text(mode_lbl, modus_label(fg->base.modus));
            lv_obj_set_style_text_font(mode_lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(mode_lbl, lv_color_hex(CLR_PRIMARY), 0);
            lv_obj_set_width(mode_lbl, 120);

            char w_buf[80];
            snprintf(w_buf, sizeof(w_buf), LV_SYMBOL_CHARGE " %s  %dPKT",
                     winner, best_pts);
            lv_obj_t *win_lbl = lv_label_create(row);
            lv_label_set_text(win_lbl, w_buf);
            lv_obj_set_style_text_font(win_lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(win_lbl, lv_color_hex(CLR_TEXT), 0);

            char p_buf[12];
            snprintf(p_buf, sizeof(p_buf), "%d SPILLER", fg->spieler_count);
            lv_obj_t *p_lbl = lv_label_create(row);
            lv_label_set_text(p_lbl, p_buf);
            lv_obj_set_style_text_font(p_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(p_lbl, lv_color_hex(CLR_MUTED), 0);
        }
    }
}

void screen_dashboard_tick(void)
{
    static uint32_t last = 0;
    if (lv_tick_get() - last > 2000) {
        last = lv_tick_get();
        if (g_store.wifiConnected) screen_dashboard_refresh();
    }
}
