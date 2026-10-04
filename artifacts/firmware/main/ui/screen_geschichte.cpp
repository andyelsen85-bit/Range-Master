// ============================================================
// SPILLGESCHICHT screen - local game history list + detail panel
// ============================================================
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "lvgl.h"
#include "ui_fonts.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_geschichte.h"
#include "ui_time_fmt.h"
#include "history_scorecard.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_list;
static lv_obj_t *s_lbl_empty;

// ── Detail panel widgets ──────────────────────────────────────
static lv_obj_t *s_detail_card;
static lv_obj_t *s_detail_hdr;    // "Game N - Mode"
static lv_obj_t *s_detail_date;
static lv_obj_t *s_detail_table;  // player results table
static EXT_RAM_BSS_ATTR FinishedGame s_selected_game;
static bool s_has_selected_game;
static bool s_history_built;
static uint32_t s_history_signature;
static bool s_doublette_columns[MAX_ERGEBNISSE + 2];

#define LIST_W   300
#define DETAIL_W (DISPLAY_LOGICAL_W - 40 - LIST_W - 16)

static void scorecard_draw_cb(lv_event_t *event)
{
    lv_draw_task_t *task = lv_event_get_draw_task(event);
    if (!task) return;
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(task);
    if (!base || base->part != LV_PART_ITEMS || base->id2 >= MAX_ERGEBNISSE + 2 ||
        !s_doublette_columns[base->id2]) return;
    lv_draw_fill_dsc_t *fill = lv_draw_task_get_fill_dsc(task);
    if (fill) {
        // Muted blue, only slightly lighter than the regular dark cell.
        // Tint the header and every player's result in both pair columns.
        fill->color = lv_color_hex(0x1C2B40);
        fill->opa = LV_OPA_COVER;
    }
}

// ── Populate detail panel for game at history index i ────────
static void render_selected_detail(void)
{
    if (!s_has_selected_game) return;
    const FinishedGame *fg = &s_selected_game;
    HistoryScorecardShape shape = history_scorecard_shape(fg);
    int players = fg->spieler_count;
    if (players < 0) players = 0;
    if (players > MAX_SPIELER) players = MAX_SPIELER;
    int result_columns = shape.runs * shape.clays;
    memset(s_doublette_columns, 0, sizeof(s_doublette_columns));

    char hdr[96];
    snprintf(hdr, sizeof(hdr), "%s | %d Spieler | %d %s",
             modus_label(fg->base.modus), players, shape.runs,
             shape.runs == 1 ? "Lauf" : "Läufe");
    lv_label_set_text(s_detail_hdr, hdr);
    {   // UTC → local time (CET/CEST), formatted as "DD.MM.YYYY HH:MM"
        char ts_disp[24];
        fmt_local_time(fg->finishedAt, ts_disp, sizeof(ts_disp));
        lv_label_set_text(s_detail_date, ts_disp[0] ? ts_disp : "-");
    }

    lv_table_set_col_cnt(s_detail_table, result_columns + 2);
    lv_table_set_row_cnt(s_detail_table, players + 1);
    lv_table_set_col_width(s_detail_table, 0, 150);
    lv_table_set_cell_value(s_detail_table, 0, 0, "SPIELER");
    for (int col = 0; col < result_columns; ++col) {
        char heading[32];
        history_scorecard_heading(fg, col / shape.clays + 1,
                                  col % shape.clays + 1, heading, sizeof(heading));
        s_doublette_columns[col + 1] = history_scorecard_doublette(
            fg, col / shape.clays + 1, col % shape.clays + 1);
        lv_table_set_col_width(s_detail_table, col + 1, 34);
        lv_table_set_cell_value(s_detail_table, 0, col + 1, heading);
    }
    int total_col = result_columns + 1;
    lv_table_set_col_width(s_detail_table, total_col, 76);
    lv_table_set_cell_value(s_detail_table, 0, total_col, "GESAMT");

    for (int p = 0; p < players; p++) {
        int sid = fg->spielerIds[p];
        lv_table_set_cell_value(s_detail_table, p + 1, 0, fg->spielerNamen[p]);
        lv_table_set_cell_ctrl(s_detail_table, p + 1, 0,
                               LV_TABLE_CELL_CTRL_TEXT_CROP);
        for (int col = 0; col < result_columns; ++col) {
            const Ergebnis *result = history_scorecard_result(
                fg, sid, col / shape.clays + 1, col % shape.clays + 1);
            char score[16];
            if (result) snprintf(score, sizeof(score), "%d", result->punkte);
            else snprintf(score, sizeof(score), "-");
            lv_table_set_cell_value(s_detail_table, p + 1, col + 1, score);
        }
        char total[16];
        snprintf(total, sizeof(total), "%d", history_scorecard_total(fg, sid));
        lv_table_set_cell_value(s_detail_table, p + 1, total_col, total);
    }

    lv_obj_clear_flag(s_detail_card, LV_OBJ_FLAG_HIDDEN);
}

static void show_detail(int idx)
{
    if (idx < 0 || idx >= g_store.historyCount) return;
    // Immutable, player-named snapshot: never retain an index/pointer into the
    // cache that sync can reorder, replace or evict while this card is open.
    s_selected_game = g_store.history[idx];
    s_has_selected_game = true;
    lv_obj_scroll_to_x(lv_obj_get_parent(s_detail_table), 0, LV_ANIM_OFF);
    lv_obj_scroll_to_y(lv_obj_get_parent(s_detail_table), 0, LV_ANIM_OFF);
    render_selected_detail();
}

static uint32_t history_rows_signature(void)
{
    uint32_t hash = 2166136261u;
    int count = g_store.historyCount;
    if (count < 0) count = 0;
    if (count > MAX_HISTORY) count = MAX_HISTORY;
    const unsigned char *bytes = (const unsigned char *)g_store.history;
    for (size_t i = 0; i < (size_t)count * sizeof(FinishedGame); ++i)
        hash = (hash ^ bytes[i]) * 16777619u;
    return hash ^ (uint32_t)count;
}

// ── Build list rows ───────────────────────────────────────────
static void build_history_rows(void)
{
    lv_obj_clean(s_list);
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
    if (!s_has_selected_game) lv_obj_add_flag(s_detail_card, LV_OBJ_FLAG_HIDDEN);

    if (g_store.historyCount == 0) {
        if (!s_has_selected_game) lv_obj_clear_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
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
        lv_obj_set_height(btn, 92);
        lv_obj_set_style_bg_color(btn, lv_color_hex(CLR_CARD), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_hor(btn, 14, 0);
        lv_obj_set_style_pad_ver(btn, 0, 0);

        // Remove default label LVGL adds - use our own layout
        lv_obj_clean(btn);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t *ts_lbl = lv_label_create(btn);
        {   // UTC → local time (CET/CEST), formatted as "DD.MM.YYYY HH:MM"
            char ts_disp[24];
            fmt_local_time(fg->finishedAt, ts_disp, sizeof(ts_disp));
            lv_label_set_text(ts_lbl, ts_disp[0] ? ts_disp : "-");
        }
        lv_obj_set_style_text_font(ts_lbl, UI_FONT_12, 0);
        lv_obj_set_style_text_color(ts_lbl, lv_color_hex(CLR_MUTED), 0);
        lv_obj_set_width(ts_lbl, LIST_W - 32);

        lv_obj_t *mode_lbl = lv_label_create(btn);
        char mode_text[64];
        snprintf(mode_text, sizeof(mode_text), "%s | %d Spieler",
                 modus_label(fg->base.modus), fg->spieler_count);
        lv_label_set_text(mode_lbl, mode_text);
        lv_obj_set_style_text_font(mode_lbl, UI_FONT_16, 0);
        lv_obj_set_style_text_color(mode_lbl, lv_color_hex(CLR_PRIMARY), 0);
        lv_obj_set_width(mode_lbl, LIST_W - 32);

        char w_buf[80];
        snprintf(w_buf, sizeof(w_buf), LV_SYMBOL_CHARGE " %s  %dPKT",
                 winner, best_pts);
        lv_obj_t *win_lbl = lv_label_create(btn);
        lv_label_set_text(win_lbl, w_buf);
        lv_obj_set_style_text_font(win_lbl, UI_FONT_14, 0);
        lv_obj_set_style_text_color(win_lbl, lv_color_hex(CLR_TEXT), 0);
        lv_label_set_long_mode(win_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(win_lbl, LIST_W - 32);

        // Click handler — pass absolute history index so show_detail() finds the right game
        lv_obj_add_event_cb(btn, [](lv_event_t *ev) {
            int hist_idx = (int)(intptr_t)lv_event_get_user_data(ev);
            show_detail(hist_idx);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)idx[ii]);
    }
}

lv_obj_t *screen_geschichte_create(void)
{
    s_history_built = false;
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
    lv_label_set_text(title, LV_SYMBOL_LIST "  SPIELVERLAUF");
    lv_obj_set_style_text_font(title, UI_FONT_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_PRIMARY), 0);

    lv_obj_t *back = lv_btn_create(hdr);
    lv_obj_add_style(back, &g_style_btn_secondary, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *e) {
        ui_manager_show(SCREEN_DASHBOARD);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_HOME "  ZURÜCK");
    lv_obj_set_style_text_color(bl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(bl);

    // Empty placeholder
    s_lbl_empty = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_empty, "NOCH KEINE SPIELE");
    lv_obj_set_style_text_font(s_lbl_empty, UI_FONT_18, 0);
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
    lv_label_set_text(d_title, "ERGEBNIS");
    lv_obj_set_style_text_font(d_title, UI_FONT_14, 0);
    lv_obj_set_style_text_color(d_title, lv_color_hex(CLR_MUTED), 0);

    s_detail_hdr = lv_label_create(s_detail_card);
    lv_label_set_text(s_detail_hdr, "---");
    lv_obj_set_style_text_font(s_detail_hdr, UI_FONT_18, 0);
    lv_obj_set_style_text_color(s_detail_hdr, lv_color_hex(CLR_PRIMARY), 0);
    lv_label_set_long_mode(s_detail_hdr, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_detail_hdr, DETAIL_W - 32);

    s_detail_date = lv_label_create(s_detail_card);
    lv_label_set_text(s_detail_date, "");
    lv_obj_set_style_text_font(s_detail_date, UI_FONT_12, 0);
    lv_obj_set_style_text_color(s_detail_date, lv_color_hex(CLR_MUTED), 0);

    // Divider
    lv_obj_t *div = lv_obj_create(s_detail_card);
    lv_obj_set_size(div, LV_PCT(100), 1);
    lv_obj_set_style_bg_color(div, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);

    lv_obj_t *legend = lv_label_create(s_detail_card);
    lv_label_set_text(legend, "Doubletten blau hinterlegt. '-' = nicht gespeichert.\n"
                              "Lange Folgen seitlich verschieben.");
    lv_obj_set_style_text_font(legend, UI_FONT_12, 0);
    lv_obj_set_style_text_color(legend, lv_color_hex(CLR_MUTED), 0);
    lv_obj_set_width(legend, LV_PCT(100));

    // Independent scrolling surface: sync never recreates this table or
    // changes its horizontal/vertical position.
    lv_obj_t *score_view = lv_obj_create(s_detail_card);
    lv_obj_set_width(score_view, LV_PCT(100));
    lv_obj_set_flex_grow(score_view, 1);
    lv_obj_set_style_pad_all(score_view, 0, 0);
    lv_obj_set_style_bg_color(score_view, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_border_width(score_view, 0, 0);
    lv_obj_set_scroll_dir(score_view, LV_DIR_ALL);
    s_detail_table = lv_table_create(score_view);
    lv_obj_set_width(s_detail_table, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(s_detail_table, UI_FONT_14, 0);
    lv_obj_set_style_text_color(s_detail_table, lv_color_hex(CLR_TEXT), 0);
    lv_obj_set_style_text_color(s_detail_table, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_detail_table, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(s_detail_table, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_border_color(s_detail_table, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_text_align(s_detail_table, LV_TEXT_ALIGN_CENTER, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(s_detail_table, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(s_detail_table, 10, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_detail_table, lv_color_hex(CLR_CARD), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_detail_table, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_detail_table, scorecard_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_flag(s_detail_table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);

    return s_scr;
}

static void refresh_selected_history_results(void)
{
    if (!s_has_selected_game || !s_selected_game.base.externalId[0] ||
        s_selected_game.base.ergebnisse_count != 0) return;
    for (int i = 0; i < g_store.historyCount && i < MAX_HISTORY; ++i) {
        const FinishedGame *game = &g_store.history[i];
        if (strcmp(game->base.externalId, s_selected_game.base.externalId) ||
            game->base.ergebnisse_count <= s_selected_game.base.ergebnisse_count) continue;
        // Enrich an old totals-only cache, never downgrade an open scorecard
        // or replace its player rows because the portal's name map reordered.
        memcpy(s_selected_game.base.ergebnisse, game->base.ergebnisse,
               sizeof(s_selected_game.base.ergebnisse));
        s_selected_game.base.ergebnisse_count = game->base.ergebnisse_count;
        s_selected_game.base.lauf = game->base.lauf;
        s_selected_game.base.taubenProLauf = game->base.taubenProLauf;
        s_selected_game.base.modus = game->base.modus;
        render_selected_detail();
        return;
    }
}

void screen_geschichte_refresh(void)
{
    if (!s_list) return;
    uint32_t signature = history_rows_signature();
    if (s_history_built && signature == s_history_signature) return;
    refresh_selected_history_results();
    int32_t scroll_y = lv_obj_get_scroll_y(s_list);
    if (!s_history_built && s_has_selected_game) render_selected_detail();
    build_history_rows();
    lv_obj_scroll_to_y(s_list, scroll_y, LV_ANIM_OFF);
    s_history_signature = signature;
    s_history_built = true;
}
