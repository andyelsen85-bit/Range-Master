// ============================================================
// Spiel screen — active game scoring
// Mirrors SpielScreen.tsx
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_spiel.h"
#include "lora_stub.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_lbl_maschine;
static lv_obj_t *s_lbl_lauf;
static lv_obj_t *s_lbl_taube;
static lv_obj_t *s_player_grid;
static lv_obj_t *s_btn_2;
static lv_obj_t *s_btn_1;
static lv_obj_t *s_btn_0;
static lv_obj_t *s_btn_wiederhole;
static lv_obj_t *s_lbl_modus;
static lv_obj_t *s_score_table;

// ── Score button callbacks ────────────────────────────────────
static void score_cb(lv_event_t *e)
{
    int pts = (int)(intptr_t)lv_event_get_user_data(e);
    // Fire LoRa stub for physical machine trigger
    if (pts > 0 && g_store.taubeIndex < g_store.sequenzLen) {
        lora_fire_machine(g_store.sequenz[g_store.taubeIndex].maschine);
    }
    store_eintragen(pts);
    screen_spiel_refresh();
}

static void wiederhole_cb(lv_event_t *e)
{
    store_wiederholen();
    screen_spiel_refresh();
}

static void skip_cb(lv_event_t *e)
{
    store_skip_taube();
    screen_spiel_refresh();
}

// ── screen_spiel_create ───────────────────────────────────────
lv_obj_t *screen_spiel_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Top status bar (70px)
    lv_obj_t *topbar = lv_obj_create(s_scr);
    lv_obj_set_size(topbar, DISPLAY_LOGICAL_W, 70);
    lv_obj_align(topbar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(topbar, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_bg_opa(topbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(topbar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(topbar, 2, 0);
    lv_obj_set_style_border_color(topbar, lv_color_hex(CLR_BORDER), 0);
    lv_obj_clear_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(topbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(topbar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(topbar, 20, 0);

    s_lbl_modus = lv_label_create(topbar);
    lv_label_set_text(s_lbl_modus, "NORMAL");
    lv_obj_set_style_text_font(s_lbl_modus, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_lbl_modus, lv_color_hex(CLR_PRIMARY), 0);

    s_lbl_lauf = lv_label_create(topbar);
    lv_label_set_text(s_lbl_lauf, "Lauf 1");
    lv_obj_set_style_text_font(s_lbl_lauf, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_lbl_lauf, lv_color_hex(CLR_TEXT), 0);

    s_lbl_taube = lv_label_create(topbar);
    lv_label_set_text(s_lbl_taube, "Taube 1 / 8");
    lv_obj_set_style_text_font(s_lbl_taube, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_lbl_taube, lv_color_hex(CLR_MUTED), 0);

    // ── Left panel: Machine + fire buttons (280px)
    lv_obj_t *left = lv_obj_create(s_scr);
    lv_obj_set_size(left, 280, DISPLAY_LOGICAL_H - 70);
    lv_obj_align(left, LV_ALIGN_TOP_LEFT, 0, 70);
    lv_obj_set_style_bg_color(left, lv_color_hex(CLR_SIDEBAR), 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(left, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(left, 2, 0);
    lv_obj_set_style_border_color(left, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(left, 20, 0);
    lv_obj_set_style_pad_row(left, 16, 0);

    lv_obj_t *mach_hdr = lv_label_create(left);
    lv_label_set_text(mach_hdr, "WERFEN");
    lv_obj_set_style_text_font(mach_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mach_hdr, lv_color_hex(CLR_MUTED), 0);

    s_lbl_maschine = lv_label_create(left);
    lv_label_set_text(s_lbl_maschine, "A");
    lv_obj_set_style_text_font(s_lbl_maschine, &lv_font_montserrat_72, 0);
    lv_obj_set_style_text_color(s_lbl_maschine, lv_color_hex(CLR_PRIMARY), 0);

    // Score buttons: 2, 1, 0
    static const char *sc_labels[] = {"2", "1", "0"};
    static int sc_pts[]            = {2, 1, 0};
    static lv_obj_t **sc_refs[]    = {&s_btn_2, &s_btn_1, &s_btn_0};
    static uint32_t sc_colors[]    = {CLR_SUCCESS, CLR_WARN, CLR_DANGER};

    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(left);
        *sc_refs[i] = btn;
        lv_obj_set_size(btn, LV_PCT(100), 72);
        lv_obj_set_style_bg_color(btn, lv_color_hex(sc_colors[i]), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, score_cb, LV_EVENT_CLICKED,
                            (void*)(intptr_t)sc_pts[i]);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, sc_labels[i]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_36, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(lbl);
    }

    // Wiederhole + Skip row
    lv_obj_t *ctrl_row = lv_obj_create(left);
    lv_obj_set_size(ctrl_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ctrl_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(ctrl_row, 0, 0);
    lv_obj_set_style_pad_all(ctrl_row, 0, 0);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(ctrl_row, 8, 0);

    s_btn_wiederhole = lv_btn_create(ctrl_row);
    lv_obj_add_style(s_btn_wiederhole, &g_style_btn_secondary, 0);
    lv_obj_set_size(s_btn_wiederhole, LV_PCT(50), 44);
    lv_obj_add_event_cb(s_btn_wiederhole, wiederhole_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wl = lv_label_create(s_btn_wiederhole);
    lv_label_set_text(wl, LV_SYMBOL_REFRESH " Wdh");
    lv_obj_set_style_text_color(wl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(wl);

    lv_obj_t *skip_btn = lv_btn_create(ctrl_row);
    lv_obj_add_style(skip_btn, &g_style_btn_secondary, 0);
    lv_obj_set_size(skip_btn, LV_PCT(50), 44);
    lv_obj_add_event_cb(skip_btn, skip_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(skip_btn);
    lv_label_set_text(sl, LV_SYMBOL_NEXT " Skip");
    lv_obj_set_style_text_color(sl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(sl);

    // ── Right panel: player grid + scores
    lv_obj_t *right = lv_obj_create(s_scr);
    lv_obj_set_size(right, DISPLAY_LOGICAL_W - 280, DISPLAY_LOGICAL_H - 70);
    lv_obj_align(right, LV_ALIGN_TOP_LEFT, 280, 70);
    lv_obj_set_style_bg_color(right, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 16, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 12, 0);

    lv_obj_t *pg_hdr = lv_label_create(right);
    lv_label_set_text(pg_hdr, "Spiller am aktuelle Posten");
    lv_obj_set_style_text_font(pg_hdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(pg_hdr, lv_color_hex(CLR_MUTED), 0);

    s_player_grid = lv_obj_create(right);
    lv_obj_set_size(s_player_grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_player_grid, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_player_grid, 0, 0);
    lv_obj_set_style_pad_all(s_player_grid, 0, 0);
    lv_obj_set_flex_flow(s_player_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(s_player_grid, 8, 0);
    lv_obj_set_style_pad_column(s_player_grid, 8, 0);

    lv_obj_t *sc_hdr = lv_label_create(right);
    lv_label_set_text(sc_hdr, "Punktestand");
    lv_obj_set_style_text_font(sc_hdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sc_hdr, lv_color_hex(CLR_TEXT), 0);

    s_score_table = lv_table_create(right);
    lv_obj_set_width(s_score_table, LV_PCT(100));
    lv_table_set_col_cnt(s_score_table, 3);
    lv_table_set_col_width(s_score_table, 0, 200);
    lv_table_set_col_width(s_score_table, 1, 80);
    lv_table_set_col_width(s_score_table, 2, 80);
    lv_table_set_cell_value(s_score_table, 0, 0, "Spiller");
    lv_table_set_cell_value(s_score_table, 0, 1, "Posten");
    lv_table_set_cell_value(s_score_table, 0, 2, "Punkte");
    lv_obj_set_style_text_font(s_score_table, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_score_table, lv_color_hex(CLR_TEXT), 0);
    lv_obj_set_style_bg_color(s_score_table, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_border_color(s_score_table, lv_color_hex(CLR_BORDER), 0);

    return s_scr;
}

// ── screen_spiel_refresh ──────────────────────────────────────
void screen_spiel_refresh(void)
{
    if (!s_lbl_maschine) return;
    GameStore *s = &g_store;

    // Top bar
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", modus_label(s->modus));
    lv_label_set_text(s_lbl_modus, buf);
    snprintf(buf, sizeof(buf), "Lauf %d", s->lauf);
    lv_label_set_text(s_lbl_lauf, buf);
    snprintf(buf, sizeof(buf), "Taube %d / %d",
             s->taubeIndex + 1, s->sequenzLen);
    lv_label_set_text(s_lbl_taube, buf);

    // Current machine
    if (s->taubeIndex < s->sequenzLen) {
        const char *ml = maschine_label(s->sequenz[s->taubeIndex].maschine);
        lv_label_set_text(s_lbl_maschine, ml);
        if (s->sequenz[s->taubeIndex].maschine == MASCHINE_H) {
            lv_obj_set_style_text_color(s_lbl_maschine,
                lv_color_hex(CLR_WARN), 0);
        } else {
            lv_obj_set_style_text_color(s_lbl_maschine,
                lv_color_hex(CLR_PRIMARY), 0);
        }
    }

    // Player grid (who's at each post)
    lv_obj_clean(s_player_grid);
    for (int i = 0; i < s->spielerCount; i++) {
        bool active = (i == s->spielerIndex);
        lv_obj_t *card = lv_obj_create(s_player_grid);
        lv_obj_set_size(card, 160, 60);
        lv_obj_add_style(card, &g_style_card, 0);
        if (active) {
            lv_obj_set_style_border_color(card, lv_color_hex(CLR_PRIMARY), 0);
            lv_obj_set_style_border_width(card, 2, 0);
        }
        lv_obj_t *name_lbl = lv_label_create(card);
        lv_label_set_text(name_lbl, s->spieler[i].name);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name_lbl,
            active ? lv_color_hex(CLR_PRIMARY) : lv_color_hex(CLR_TEXT), 0);
        lv_obj_align(name_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

        char pts[16];
        snprintf(pts, sizeof(pts), "%d Pkt", s->spieler[i].punkte);
        lv_obj_t *pts_lbl = lv_label_create(card);
        lv_label_set_text(pts_lbl, pts);
        lv_obj_set_style_text_font(pts_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(pts_lbl, lv_color_hex(CLR_MUTED), 0);
        lv_obj_align(pts_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    }

    // Score table
    for (int i = 0; i < s->spielerCount; i++) {
        char row_buf[32];
        lv_table_set_cell_value(s_score_table, i + 1, 0, s->spieler[i].name);
        snprintf(row_buf, sizeof(row_buf), "P%d", s->spieler[i].startPosten);
        lv_table_set_cell_value(s_score_table, i + 1, 1, row_buf);
        snprintf(row_buf, sizeof(row_buf), "%d", s->spieler[i].punkte);
        lv_table_set_cell_value(s_score_table, i + 1, 2, row_buf);
    }
    lv_table_set_row_cnt(s_score_table, s->spielerCount + 1);
}

void screen_spiel_tick(void)
{
    // Nothing needed — score buttons call refresh directly
}
