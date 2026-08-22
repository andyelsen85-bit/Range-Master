// ============================================================
// Spiel screen - active game scoring
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
static lv_obj_t *s_player_grid;       // 5-post grid, rebuilt each refresh
static lv_obj_t *s_lbl_active_name;   // active shooter: name
static lv_obj_t *s_lbl_active_post;   // active shooter: current post  "P3"
static lv_obj_t *s_lbl_active_pts;    // active shooter: points
static lv_obj_t *s_btn_wiederhole;
static lv_obj_t *s_lbl_modus;
static lv_obj_t *s_lbl_fire_status;
static lv_obj_t *s_score_table;

// ── Score button callbacks ────────────────────────────────────
static void score_cb(lv_event_t *e)
{
    int pts = (int)(intptr_t)lv_event_get_user_data(e);
    if (pts > 0 && g_store.taubeIndex < g_store.sequenzLen)
        lora_fire_machine(g_store.sequenz[g_store.taubeIndex].maschine);
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
    screen_base_init(s_scr);   // dark bg, opaque, non-scrollable

    // ── Top status bar (70px) ─────────────────────────────
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
    lv_label_set_text(s_lbl_lauf, "LAUF 1");
    lv_obj_set_style_text_font(s_lbl_lauf, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_lbl_lauf, lv_color_hex(CLR_TEXT), 0);

    s_lbl_taube = lv_label_create(topbar);
    lv_label_set_text(s_lbl_taube, "TAUBE 1 / 8");
    lv_obj_set_style_text_font(s_lbl_taube, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_lbl_taube, lv_color_hex(CLR_MUTED), 0);

    // ── Left panel: Machine + score buttons (280px) ───────
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
    lv_obj_set_style_text_font(s_lbl_maschine, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_lbl_maschine, lv_color_hex(CLR_PRIMARY), 0);

    // Score buttons: 2, 1, 0
    static const char *sc_labels[] = {"2", "1", "0"};
    static int sc_pts[]            = {2, 1, 0};
    static uint32_t sc_colors[]    = {CLR_SUCCESS, CLR_WARN, CLR_DANGER};

    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(left);
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

    // Wiederhole + SKIP row
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
    lv_label_set_text(wl, LV_SYMBOL_REFRESH " WDH");
    lv_obj_set_style_text_color(wl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(wl);

    lv_obj_t *skip_btn = lv_btn_create(ctrl_row);
    lv_obj_add_style(skip_btn, &g_style_btn_secondary, 0);
    lv_obj_set_size(skip_btn, LV_PCT(50), 44);
    lv_obj_add_event_cb(skip_btn, skip_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(skip_btn);
    lv_label_set_text(sl, LV_SYMBOL_NEXT " SKIP");
    lv_obj_set_style_text_color(sl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(sl);

    // ── Right panel ───────────────────────────────────────
    lv_obj_t *right = lv_obj_create(s_scr);
    lv_obj_set_size(right, DISPLAY_LOGICAL_W - 280, DISPLAY_LOGICAL_H - 70);
    lv_obj_align(right, LV_ALIGN_TOP_LEFT, 280, 70);
    lv_obj_set_style_bg_color(right, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 12, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 10, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);

    // ── 1. Post grid header
    lv_obj_t *pg_hdr = lv_label_create(right);
    lv_label_set_text(pg_hdr, "POSTEN");
    lv_obj_set_style_text_font(pg_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pg_hdr, lv_color_hex(CLR_MUTED), 0);

    // ── 2. Five-post grid (children rebuilt in refresh)
    s_player_grid = lv_obj_create(right);
    lv_obj_set_size(s_player_grid, LV_PCT(100), 128);
    lv_obj_set_style_bg_opa(s_player_grid, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_player_grid, 0, 0);
    lv_obj_set_style_pad_all(s_player_grid, 0, 0);
    lv_obj_set_flex_flow(s_player_grid, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_player_grid, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(s_player_grid, LV_OBJ_FLAG_SCROLLABLE);

    // ── 3. Active shooter banner
    lv_obj_t *shooter_bar = lv_obj_create(right);
    lv_obj_set_size(shooter_bar, LV_PCT(100), 68);
    lv_obj_add_style(shooter_bar, &g_style_card, 0);
    lv_obj_set_style_pad_hor(shooter_bar, 20, 0);
    lv_obj_set_style_pad_ver(shooter_bar, 0, 0);
    lv_obj_set_flex_flow(shooter_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(shooter_bar, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(shooter_bar, LV_OBJ_FLAG_SCROLLABLE);

    // Name section
    lv_obj_t *name_col = lv_obj_create(shooter_bar);
    lv_obj_set_size(name_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(name_col, LV_OPA_0, 0);
    lv_obj_set_style_border_width(name_col, 0, 0);
    lv_obj_set_style_pad_all(name_col, 0, 0);
    lv_obj_set_flex_flow(name_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(name_col, 2, 0);

    lv_obj_t *shooter_hdr_lbl = lv_label_create(name_col);
    lv_label_set_text(shooter_hdr_lbl, "AKTUELLEN SCHUTZ");
    lv_obj_set_style_text_font(shooter_hdr_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(shooter_hdr_lbl, lv_color_hex(CLR_PRIMARY), 0);

    s_lbl_active_name = lv_label_create(name_col);
    lv_label_set_text(s_lbl_active_name, "---");
    lv_obj_set_style_text_font(s_lbl_active_name, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_lbl_active_name, lv_color_hex(CLR_TEXT), 0);

    // Post + Points
    s_lbl_active_post = lv_label_create(shooter_bar);
    lv_label_set_text(s_lbl_active_post, "P-");
    lv_obj_set_style_text_font(s_lbl_active_post, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(s_lbl_active_post, lv_color_hex(CLR_PRIMARY), 0);

    s_lbl_active_pts = lv_label_create(shooter_bar);
    lv_label_set_text(s_lbl_active_pts, "0 PKT");
    lv_obj_set_style_text_font(s_lbl_active_pts, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(s_lbl_active_pts, lv_color_hex(CLR_PRIMARY), 0);

    s_lbl_fire_status = lv_label_create(right);
    lv_label_set_text(s_lbl_fire_status, "Gateway: -");
    lv_obj_set_style_text_font(s_lbl_fire_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_fire_status, lv_color_hex(CLR_MUTED), 0);

    // ── 4. Score table header
    lv_obj_t *sc_hdr = lv_label_create(right);
    lv_label_set_text(sc_hdr, "PUNKTESTAND");
    lv_obj_set_style_text_font(sc_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sc_hdr, lv_color_hex(CLR_TEXT), 0);

    // ── 5. Score table
    s_score_table = lv_table_create(right);
    lv_obj_set_width(s_score_table, LV_PCT(100));
    lv_table_set_col_cnt(s_score_table, 3);
    lv_table_set_col_width(s_score_table, 0, 220);
    lv_table_set_col_width(s_score_table, 1, 90);
    lv_table_set_col_width(s_score_table, 2, 90);
    lv_table_set_cell_value(s_score_table, 0, 0, "SPILLER");
    lv_table_set_cell_value(s_score_table, 0, 1, "POSTEN");
    lv_table_set_cell_value(s_score_table, 0, 2, "PUNKTE");
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
    if (s_lbl_fire_status) {
        char fire_status[112];
        snprintf(fire_status, sizeof(fire_status), "Gateway: %s", lora_status_text());
        lv_label_set_text(s_lbl_fire_status, fire_status);
    }

    // ── Compute current sequenz entry state ───────────────
    bool isHMaschine = false;
    bool isH2        = false;   // true = 2nd doublette shot
    if (s->taubeIndex < s->sequenzLen) {
        SequenzEintrag *se = &s->sequenz[s->taubeIndex];
        isHMaschine = (se->maschine == MASCHINE_H);
        isH2        = isHMaschine && se->isDoublette;
    }
    // H2 players stay at the same post as H1: use taubeIndex-1 for rotation
    int effIdx = (isH2 && s->taubeIndex > 0) ? s->taubeIndex - 1 : s->taubeIndex;

    // Inline post formula: ((startPosten-1 + effIdx) % 5) + 1
    // (mirrors getCurrentPosten in gameStore.ts)
    auto pos_of = [&](int startPosten) -> int {
        return ((startPosten - 1 + effIdx) % 5) + 1;
    };

    // ── Top bar ───────────────────────────────────────────
    char buf[64];
    lv_label_set_text(s_lbl_modus, modus_label(s->modus));
    snprintf(buf, sizeof(buf), "LAUF %d", s->lauf);
    lv_label_set_text(s_lbl_lauf, buf);
    snprintf(buf, sizeof(buf), "TAUBE %d / %d", s->taubeIndex + 1, s->sequenzLen);
    lv_label_set_text(s_lbl_taube, buf);

    // ── Machine label: A-G / H1 / H2 ─────────────────────
    if (s->taubeIndex < s->sequenzLen) {
        SequenzEintrag *se = &s->sequenz[s->taubeIndex];
        char ml[8];
        if (isHMaschine) {
            snprintf(ml, sizeof(ml), "H%d", isH2 ? 2 : 1);
            lv_obj_set_style_text_color(s_lbl_maschine, lv_color_hex(CLR_WARN), 0);
        } else {
            snprintf(ml, sizeof(ml), "%s", maschine_label(se->maschine));
            lv_obj_set_style_text_color(s_lbl_maschine, lv_color_hex(CLR_PRIMARY), 0);
        }
        lv_label_set_text(s_lbl_maschine, ml);
    }

    // ── Active shooter's current post ─────────────────────
    int active_post = 1;
    if (s->spielerCount > 0 && s->spielerIndex < s->spielerCount)
        active_post = pos_of(s->spieler[s->spielerIndex].startPosten);

    // ── 5-post grid ───────────────────────────────────────
    // Column width: right panel usable width ≈ 976px. 5 cols × 186 + 4 × 6 gap ≈ 954.
    static const int POST_W = 186;
    static const int POST_H = 128;

    lv_obj_clean(s_player_grid);
    for (int post = 1; post <= 5; post++) {
        bool isActivePost = (post == active_post);

        lv_obj_t *col = lv_obj_create(s_player_grid);
        lv_obj_set_size(col, POST_W, POST_H);
        lv_obj_set_style_bg_color(col, lv_color_hex(CLR_CARD), 0);
        lv_obj_set_style_bg_opa(col, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(col,
            isActivePost ? lv_color_hex(CLR_PRIMARY) : lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_border_width(col, isActivePost ? 3 : 1, 0);
        lv_obj_set_style_radius(col, 8, 0);
        lv_obj_set_style_pad_all(col, 8, 0);
        lv_obj_set_style_pad_row(col, 4, 0);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        // Post header
        lv_obj_t *post_hdr = lv_label_create(col);
        char phdr[16];
        snprintf(phdr, sizeof(phdr), "POSTEN %d", post);
        lv_label_set_text(post_hdr, phdr);
        lv_obj_set_style_text_font(post_hdr, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(post_hdr,
            isActivePost ? lv_color_hex(CLR_PRIMARY) : lv_color_hex(CLR_MUTED), 0);

        // Players at this post
        bool anyPlayer = false;
        for (int i = 0; i < s->spielerCount; i++) {
            if (pos_of(s->spieler[i].startPosten) != post) continue;
            anyPlayer = true;
            bool isActive = (i == s->spielerIndex);

            lv_obj_t *prow = lv_obj_create(col);
            lv_obj_set_size(prow, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(prow,
                isActive ? lv_color_hex(CLR_PRIMARY) : lv_color_hex(CLR_BG), 0);
            lv_obj_set_style_bg_opa(prow, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(prow, 0, 0);
            lv_obj_set_style_radius(prow, 6, 0);
            lv_obj_set_style_pad_all(prow, 5, 0);
            lv_obj_set_flex_flow(prow, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(prow, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_clear_flag(prow, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *name_lbl = lv_label_create(prow);
            lv_label_set_text(name_lbl, s->spieler[i].name);
            lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(name_lbl,
                isActive ? lv_color_hex(0x000000) : lv_color_hex(CLR_TEXT), 0);

            char pts_buf[10];
            snprintf(pts_buf, sizeof(pts_buf), "%dp", s->spieler[i].punkte);
            lv_obj_t *pts_lbl = lv_label_create(prow);
            lv_label_set_text(pts_lbl, pts_buf);
            lv_obj_set_style_text_font(pts_lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(pts_lbl,
                isActive ? lv_color_hex(0x000000) : lv_color_hex(CLR_PRIMARY), 0);
        }

        if (!anyPlayer) {
            lv_obj_t *dash = lv_label_create(col);
            lv_label_set_text(dash, "-");
            lv_obj_set_style_text_font(dash, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(dash, lv_color_hex(CLR_BORDER), 0);
        }
    }

    // ── Active shooter banner ─────────────────────────────
    if (s->spielerCount > 0 && s->spielerIndex < s->spielerCount) {
        Spieler *sp = &s->spieler[s->spielerIndex];
        lv_label_set_text(s_lbl_active_name, sp->name);
        char pb[8];
        snprintf(pb, sizeof(pb), "P%d", active_post);
        lv_label_set_text(s_lbl_active_post, pb);
        char ptb[16];
        snprintf(ptb, sizeof(ptb), "%d PKT", sp->punkte);
        lv_label_set_text(s_lbl_active_pts, ptb);
    }

    // ── Score table: show current (rotated) position ──────
    lv_table_set_row_cnt(s_score_table, s->spielerCount + 1);
    for (int i = 0; i < s->spielerCount; i++) {
        int cur_post = pos_of(s->spieler[i].startPosten);
        char rb[16];
        lv_table_set_cell_value(s_score_table, i + 1, 0, s->spieler[i].name);
        snprintf(rb, sizeof(rb), "P%d", cur_post);
        lv_table_set_cell_value(s_score_table, i + 1, 1, rb);
        snprintf(rb, sizeof(rb), "%d", s->spieler[i].punkte);
        lv_table_set_cell_value(s_score_table, i + 1, 2, rb);
    }
}

void screen_spiel_tick(void)
{
    // The gateway worker completes asynchronously. Refresh periodically so the
    // operator sees the final success/failure rather than only "Sending...".
    static uint32_t last = 0;
    if (lv_tick_get() - last > 250) {
        last = lv_tick_get();
        screen_spiel_refresh();
    }
}
