// ============================================================
// SPILLGESCHICHT screen - local game history list + detail panel
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_geschichte.h"
#include "ui_time_fmt.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_list;
static lv_obj_t *s_lbl_empty;

// ── Detail panel widgets ──────────────────────────────────────
static lv_obj_t *s_detail_card;
static lv_obj_t *s_detail_hdr;    // "Game N - Mode"
static lv_obj_t *s_detail_date;
static lv_obj_t *s_detail_table;  // player results table

#define LIST_W   740
#define DETAIL_W (DISPLAY_LOGICAL_W - 40 - LIST_W - 16)

// ── Populate detail panel for game at history index i ────────
static void show_detail(int idx)
{
    if (idx < 0 || idx >= g_store.historyCount) return;
    const FinishedGame *fg = &g_store.history[idx];

    char hdr[64];
    snprintf(hdr, sizeof(hdr), "%s  |  %d SPILLER",
             modus_label(fg->base.modus), fg->spieler_count);
    lv_label_set_text(s_detail_hdr, hdr);
    {   // UTC → local time (CET/CEST), formatted as "DD.MM.YYYY HH:MM"
        char ts_disp[24];
        fmt_local_time(fg->finishedAt, ts_disp, sizeof(ts_disp));
        lv_label_set_text(s_detail_date, ts_disp[0] ? ts_disp : "-");
    }

    // Fill player table (row 0 = header)
    lv_table_set_row_cnt(s_detail_table, fg->spieler_count + 1);
    lv_table_set_cell_value(s_detail_table, 0, 0, "SPILLER");
    lv_table_set_cell_value(s_detail_table, 0, 1, "POSTEN");
    lv_table_set_cell_value(s_detail_table, 0, 2, "PKT");

    // Build per-player totals from teilnahmen
    for (int p = 0; p < fg->spieler_count; p++) {
        int sid = fg->spielerIds[p];
        int pts = 0, posten = 0;
        for (int t = 0; t < fg->base.teilnahmen_count; t++) {
            if (fg->base.teilnahmen[t].spielerId == sid) {
                pts    = fg->base.teilnahmen[t].punkte;
                posten = fg->base.teilnahmen[t].startPosten;
                break;
            }
        }
        char pp[8], ps[8];
        snprintf(pp, sizeof(pp), "P%d", posten);
        snprintf(ps, sizeof(ps), "%d", pts);
        lv_table_set_cell_value(s_detail_table, p + 1, 0, fg->spielerNamen[p]);
        lv_table_set_cell_value(s_detail_table, p + 1, 1, pp);
        lv_table_set_cell_value(s_detail_table, p + 1, 2, ps);
    }

    lv_obj_clear_flag(s_detail_card, LV_OBJ_FLAG_HIDDEN);
}

// ── Build list rows ───────────────────────────────────────────
static void build_history_rows(void)
{
    lv_obj_clean(s_list);
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_detail_card, LV_OBJ_FLAG_HIDDEN);

    if (g_store.historyCount == 0) {
        lv_obj_clear_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Sort indices newest-first by finishedAt (ISO strings compare chronologically)
    int idx[MAX_HISTORY];
    int n = g_store.historyCount;
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 1; i < n; i++) {
        int key = idx[i], j = i - 1;
        while (j >= 0 && strcmp(g_store.history[idx[j]].finishedAt,
                                 g_store.history[key].finishedAt) < 0) {
            idx[j + 1] = idx[j]; j--;
        }
        idx[j + 1] = key;
    }

    for (int ii = 0; ii < n; ii++) {
        const FinishedGame *fg = &g_store.history[idx[ii]];

        // Find winner
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

        // Row button — clicking opens detail panel
        lv_obj_t *btn = lv_list_add_btn(s_list, NULL, "");
        lv_obj_set_height(btn, 60);
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_CARD), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_hor(btn, 14, 0);
        lv_obj_set_style_pad_ver(btn, 0, 0);

        // Remove default label LVGL adds - use our own layout
        lv_obj_clean(btn);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *ts_lbl = lv_label_create(btn);
        {   // UTC → local time (CET/CEST), formatted as "DD.MM.YYYY HH:MM"
            char ts_disp[24];
            fmt_local_time(fg->finishedAt, ts_disp, sizeof(ts_disp));
            lv_label_set_text(ts_lbl, ts_disp[0] ? ts_disp : "-");
        }
        lv_obj_set_style_text_font(ts_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(ts_lbl, lv_color_hex(CLR_MUTED), 0);
        lv_obj_set_width(ts_lbl, 170);

        lv_obj_t *mode_lbl = lv_label_create(btn);
        lv_label_set_text(mode_lbl, modus_label(fg->base.modus));
        lv_obj_set_style_text_font(mode_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(mode_lbl, lv_color_hex(CLR_PRIMARY), 0);
        lv_obj_set_width(mode_lbl, 110);

        char w_buf[80];
        snprintf(w_buf, sizeof(w_buf), LV_SYMBOL_CHARGE " %s  %dPKT",
                 winner, best_pts);
        lv_obj_t *win_lbl = lv_label_create(btn);
        lv_label_set_text(win_lbl, w_buf);
        lv_obj_set_style_text_font(win_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(win_lbl, lv_color_hex(CLR_TEXT), 0);

        char p_buf[16];
        snprintf(p_buf, sizeof(p_buf), "%d " LV_SYMBOL_LIST, fg->spieler_count);
        lv_obj_t *p_lbl = lv_label_create(btn);
        lv_label_set_text(p_lbl, p_buf);
        lv_obj_set_style_text_font(p_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(p_lbl, lv_color_hex(CLR_MUTED), 0);

        // Click handler — pass absolute history index so show_detail() finds the right game
        lv_obj_add_event_cb(btn, [](lv_event_t *ev) {
            int hist_idx = (int)(intptr_t)lv_event_get_user_data(ev);
            show_detail(hist_idx);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)idx[ii]);
    }
}

lv_obj_t *screen_geschichte_create(void)
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
    lv_label_set_text(title, LV_SYMBOL_LIST "  SPILLGESCHICHT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_PRIMARY), 0);

    lv_obj_t *back = lv_btn_create(hdr);
    lv_obj_add_style(back, &g_style_btn_secondary, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *e) {
        ui_manager_show(SCREEN_DASHBOARD);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_HOME "  ZURUCK");
    lv_obj_set_style_text_color(bl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(bl);

    // Empty placeholder
    s_lbl_empty = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_empty, "KENG SPILLER NACH");
    lv_obj_set_style_text_font(s_lbl_empty, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_lbl_empty, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(s_lbl_empty, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    // ── Left: scrollable game list ─────────────────────────
    s_list = lv_list_create(s_scr);
    lv_obj_set_size(s_list, LIST_W, DISPLAY_LOGICAL_H - 86);
    lv_obj_align(s_list, LV_ALIGN_TOP_LEFT, 20, 78);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_style_pad_row(s_list, 6, 0);

    // ── Right: detail card (initially hidden) ──────────────
    s_detail_card = lv_obj_create(s_scr);
    lv_obj_set_size(s_detail_card, DETAIL_W, DISPLAY_LOGICAL_H - 86);
    lv_obj_align(s_detail_card, LV_ALIGN_TOP_RIGHT, -20, 78);
    lv_obj_add_style(s_detail_card, &g_style_card, 0);
    lv_obj_set_style_pad_all(s_detail_card, 16, 0);
    lv_obj_set_style_pad_row(s_detail_card, 10, 0);
    lv_obj_set_flex_flow(s_detail_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_detail_card, LV_OBJ_FLAG_HIDDEN);

    // Detail header
    lv_obj_t *d_title = lv_label_create(s_detail_card);
    lv_label_set_text(d_title, "RESULTAT");
    lv_obj_set_style_text_font(d_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(d_title, lv_color_hex(CLR_MUTED), 0);

    s_detail_hdr = lv_label_create(s_detail_card);
    lv_label_set_text(s_detail_hdr, "---");
    lv_obj_set_style_text_font(s_detail_hdr, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_detail_hdr, lv_color_hex(CLR_PRIMARY), 0);
    lv_label_set_long_mode(s_detail_hdr, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_detail_hdr, DETAIL_W - 32);

    s_detail_date = lv_label_create(s_detail_card);
    lv_label_set_text(s_detail_date, "");
    lv_obj_set_style_text_font(s_detail_date, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_detail_date, lv_color_hex(CLR_MUTED), 0);

    // Divider
    lv_obj_t *div = lv_obj_create(s_detail_card);
    lv_obj_set_size(div, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(div, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);

    // Player results table
    s_detail_table = lv_table_create(s_detail_card);
    lv_obj_set_width(s_detail_table, LV_PCT(100));
    lv_table_set_col_cnt(s_detail_table, 3);
    lv_table_set_col_width(s_detail_table, 0, DETAIL_W - 32 - 60 - 60);
    lv_table_set_col_width(s_detail_table, 1, 60);
    lv_table_set_col_width(s_detail_table, 2, 60);
    lv_obj_set_style_text_font(s_detail_table, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_detail_table, lv_color_hex(CLR_TEXT), 0);
    lv_obj_set_style_bg_color(s_detail_table, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_border_color(s_detail_table, lv_color_hex(CLR_BORDER), 0);

    return s_scr;
}

void screen_geschichte_refresh(void)
{
    if (!s_list) return;
    build_history_rows();
}
