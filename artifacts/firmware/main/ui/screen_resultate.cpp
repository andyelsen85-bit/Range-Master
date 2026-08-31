// ============================================================
// Resultater screen - post-game score summary + winner
// Mirrors ResultateScreen.tsx
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_fonts.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_resultate.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_lbl_winner;
static lv_obj_t *s_lbl_meta;
static lv_obj_t *s_table;
static lv_obj_t *s_lbl_max;

lv_obj_t *screen_resultate_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H);
    screen_base_init(s_scr);   // dark bg, opaque, non-scrollable

    // Header
    lv_obj_t *hdr = lv_obj_create(s_scr);
    lv_obj_set_size(hdr, DISPLAY_LOGICAL_W, 80);
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
    lv_label_set_text(title, LV_SYMBOL_CHARGE "  ERGEBNISSE");
    lv_obj_set_style_text_font(title, UI_FONT_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_PRIMARY), 0);

    s_lbl_meta = lv_label_create(hdr);
    lv_label_set_text(s_lbl_meta, "");
    lv_obj_set_style_text_font(s_lbl_meta, UI_FONT_14, 0);
    lv_obj_set_style_text_color(s_lbl_meta, lv_color_hex(CLR_MUTED), 0);

    lv_obj_t *done_btn = lv_btn_create(hdr);
    lv_obj_add_style(done_btn, &g_style_btn_primary, 0);
    lv_obj_add_event_cb(done_btn, [](lv_event_t *e) {
        store_dismiss_resultate();
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *dl = lv_label_create(done_btn);
    lv_label_set_text(dl, LV_SYMBOL_HOME "  FERTIG");
    lv_obj_set_style_text_color(dl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(dl);

    // Winner banner
    lv_obj_t *win_card = lv_obj_create(s_scr);
    lv_obj_set_size(win_card, DISPLAY_LOGICAL_W - 40, 90);
    lv_obj_align(win_card, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_add_style(win_card, &g_style_card, 0);
    lv_obj_set_style_bg_color(win_card, lv_color_hex(0x78350F), 0); // amber dark
    lv_obj_set_style_border_color(win_card, lv_color_hex(CLR_WARN), 0);
    lv_obj_set_style_border_width(win_card, 2, 0);
    lv_obj_set_style_radius(win_card, 12, 0);
    lv_obj_clear_flag(win_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(win_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(win_card, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(win_card, 12, 0);

    lv_obj_t *trophy = lv_label_create(win_card);
    lv_label_set_text(trophy, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_font(trophy, UI_FONT_28, 0);
    lv_obj_set_style_text_color(trophy, lv_color_hex(CLR_WARN), 0);

    s_lbl_winner = lv_label_create(win_card);
    lv_label_set_text(s_lbl_winner, "SIEGER: -");
    lv_obj_set_style_text_font(s_lbl_winner, UI_FONT_24, 0);
    lv_obj_set_style_text_color(s_lbl_winner, lv_color_hex(CLR_WARN), 0);
    lv_label_set_long_mode(s_lbl_winner, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_lbl_winner, 0);
    lv_obj_set_flex_grow(s_lbl_winner, 1);

    s_lbl_max = lv_label_create(win_card);
    lv_label_set_text(s_lbl_max, "");
    lv_obj_set_style_text_font(s_lbl_max, UI_FONT_16, 0);
    lv_obj_set_style_text_color(s_lbl_max, lv_color_hex(CLR_MUTED), 0);

    // Ranking table
    s_table = lv_table_create(s_scr);
    lv_obj_set_size(s_table, DISPLAY_LOGICAL_W - 40,
                    DISPLAY_LOGICAL_H - 80 - 106 - 20);
    lv_obj_align(s_table, LV_ALIGN_TOP_MID, 0, 80 + 106);
    lv_table_set_col_cnt(s_table, 5);
    lv_table_set_col_width(s_table, 0, 50);  // Rank
    lv_table_set_col_width(s_table, 1, 360); // Name
    lv_table_set_col_width(s_table, 2, 110); // LAUF 1
    lv_table_set_col_width(s_table, 3, 110); // LAUF 2
    lv_table_set_col_width(s_table, 4, 110); // TOTAL
    lv_obj_set_style_text_font(s_table, UI_FONT_16, 0);
    lv_obj_set_style_text_color(s_table, lv_color_hex(CLR_TEXT), 0);
    lv_obj_set_style_bg_color(s_table, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_border_color(s_table, lv_color_hex(CLR_BORDER), 0);

    lv_table_set_cell_value(s_table, 0, 0, "#");
    lv_table_set_cell_value(s_table, 0, 1, "SPIELER");
    lv_table_set_cell_value(s_table, 0, 2, "LAUF 1");
    lv_table_set_cell_value(s_table, 0, 3, "LAUF 2");
    lv_table_set_cell_value(s_table, 0, 4, "GESAMT");

    return s_scr;
}

void screen_resultate_refresh(void)
{
    if (!s_lbl_winner) return;
    if (!g_store.hasLastFinished) return;

    const FinishedGame *fg = &g_store.lastFinished;
    int n = fg->spieler_count;

    // Meta line
    char buf[80];
    snprintf(buf, sizeof(buf), "%s   |   %s   |   %d TAUBEN/LAUF",
             fg->finishedAt,
             modus_label(fg->base.modus),
             fg->base.taubenProLauf);
    lv_label_set_text(s_lbl_meta, buf);

    // Compute per-player lauf1/lauf2 totals (sort descending by total)
    typedef struct { int id; const char *name; int lauf1; int lauf2; int total; } Row;
    Row rows[MAX_SPIELER] = {};
    for (int i = 0; i < n; i++) {
        rows[i].id   = fg->spielerIds[i];
        rows[i].name = fg->spielerNamen[i];
        for (int j = 0; j < fg->base.ergebnisse_count; j++) {
            const Ergebnis *e = &fg->base.ergebnisse[j];
            if (e->spielerId != rows[i].id) continue;
            if (e->lauf == 1) rows[i].lauf1 += e->punkte;
            else              rows[i].lauf2 += e->punkte;
        }
        rows[i].total = rows[i].lauf1 + rows[i].lauf2;
    }
    // Bubble sort descending
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (rows[j].total > rows[i].total) { Row t=rows[i]; rows[i]=rows[j]; rows[j]=t; }

    // Winner banner
    if (n > 0) {
        snprintf(buf, sizeof(buf), "SIEGER: %s  (%d PUNKTE)",
                 rows[0].name, rows[0].total);
        lv_label_set_text(s_lbl_winner, buf);
        int maxScore = fg->base.taubenProLauf * 2 * 2;
        snprintf(buf, sizeof(buf), "MAX %d PKT", maxScore);
        lv_label_set_text(s_lbl_max, buf);
    }

    // Table rows
    lv_table_set_row_cnt(s_table, n + 1);
    for (int i = 0; i < n; i++) {
        char cell[16];
        snprintf(cell, sizeof(cell), "%d", i + 1);
        lv_table_set_cell_value(s_table, i+1, 0, cell);
        lv_table_set_cell_value(s_table, i+1, 1, rows[i].name);
        lv_table_set_cell_ctrl(s_table, i+1, 1, LV_TABLE_CELL_CTRL_TEXT_CROP);
        snprintf(cell, sizeof(cell), "%d", rows[i].lauf1);
        lv_table_set_cell_value(s_table, i+1, 2, cell);
        snprintf(cell, sizeof(cell), "%d", rows[i].lauf2);
        lv_table_set_cell_value(s_table, i+1, 3, cell);
        snprintf(cell, sizeof(cell), "%d", rows[i].total);
        lv_table_set_cell_value(s_table, i+1, 4, cell);
    }
}
