// ============================================================
// ASTELLUNGEN screen - settings: API, machines, custom seqs,
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
static lv_obj_t *s_kb;

// ── Tab: Portal API ───────────────────────────────────────────
static lv_obj_t *s_ta_url;
static lv_obj_t *s_ta_key;
static lv_obj_t *s_lbl_api_status;

static void save_api_cb(lv_event_t *e)
{
    const char *url = lv_textarea_get_text(s_ta_url);
    const char *key = lv_textarea_get_text(s_ta_key);
    strncpy(g_store.apiUrl, url, MAX_URL_LEN - 1); g_store.apiUrl[MAX_URL_LEN - 1] = '\0';
    strncpy(g_store.apiKey, key, MAX_KEY_LEN - 1); g_store.apiKey[MAX_KEY_LEN - 1] = '\0';
    game_store_save();
    lv_label_set_text(s_lbl_api_status, LV_SYMBOL_OK " GESPEICHERT");
    lv_obj_set_style_text_color(s_lbl_api_status, lv_color_hex(CLR_SUCCESS), 0);
}

static lv_obj_t *build_api_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 12, 0);
    lv_obj_set_style_pad_all(parent, 16, 0);

    lv_obj_t *url_lbl = lv_label_create(parent);
    lv_label_set_text(url_lbl, "PORTAL API URL");
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
    lv_label_set_text(key_lbl, "API KEY");
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
    lv_label_set_text(sl, LV_SYMBOL_SAVE " SPEICHERN");
    lv_obj_set_style_text_color(sl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(sl);

    s_lbl_api_status = lv_label_create(parent);
    lv_label_set_text(s_lbl_api_status, "");
    lv_obj_set_style_text_font(s_lbl_api_status, &lv_font_montserrat_14, 0);

    return parent;
}

// ── Tab: MASCHINNEN ───────────────────────────────────────────
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
        "MASCHINN A","MASCHINN B","MASCHINN C","MASCHINN D",
        "MASCHINN E","MASCHINN F","MASCHINN G","MASCHINN H (DOUBLETTE)"
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

// ── Tab: CUSTOM SEQUENZEN ─────────────────────────────────────
// Ordered sequence builder: tap A-H to APPEND a machine to the sequence
// (same machine can appear multiple times, up to 16 total); tap any badge
// in the sequence strip to REMOVE that slot; pick 1 or 2 Läufe.
// Mirrors the emulator's CustomSequenzEditor component.
static const char *CUSTOM_NAMES[] = {"CUSTOM 1","CUSTOM 2","CUSTOM 3","CUSTOM 4"};
static const char *MACH_LBL[]     = {"A","B","C","D","E","F","G","H"};
#define CUSTOM_SEQ_MAX 16

// Per-mode widget references — rebuilt on each add/remove
static lv_obj_t *s_custom_seq_cont[4];
static lv_obj_t *s_stat_tauben[4];
static lv_obj_t *s_stat_pkt_lauf[4];
static lv_obj_t *s_stat_pkt_spiel[4];
static lv_obj_t *s_lauf_btn[4][2];

// ── Helpers ───────────────────────────────────────────────────
static void refresh_custom_stats(int ci)
{
    if (!s_stat_tauben[ci]) return;
    int tauben = 0;
    for (int i = 0; i < g_store.customSequenzLen[ci]; i++)
        tauben += (g_store.customSequenzen[ci][i] == MASCHINE_H) ? 2 : 1;
    int pktLauf  = tauben * 2;
    int pktSpiel = pktLauf * g_store.customLaeufe[ci];
    char buf[12];   // int can be up to 11 digits + NUL — was 8, causing -Werror=format-truncation
    snprintf(buf, sizeof(buf), "%d", tauben);  lv_label_set_text(s_stat_tauben[ci],    buf);
    snprintf(buf, sizeof(buf), "%d", pktLauf); lv_label_set_text(s_stat_pkt_lauf[ci],  buf);
    snprintf(buf, sizeof(buf), "%d", pktSpiel);lv_label_set_text(s_stat_pkt_spiel[ci], buf);
}

static void refresh_custom_seq(int ci)
{
    lv_obj_t *cont = s_custom_seq_cont[ci];
    if (!cont) return;
    lv_obj_clean(cont);
    int len = g_store.customSequenzLen[ci];
    if (len == 0) {
        lv_obj_t *ph = lv_label_create(cont);
        lv_label_set_text(ph, "KENG SCHANZEN");
        lv_obj_set_style_text_color(ph, lv_color_hex(CLR_MUTED), 0);
        lv_obj_set_style_text_font(ph, &lv_font_montserrat_14, 0);
        lv_obj_center(ph);
        return;
    }
    for (int i = 0; i < len; i++) {
        Maschine m  = g_store.customSequenzen[ci][i];
        bool    isH = (m == MASCHINE_H);
        lv_obj_t *badge = lv_btn_create(cont);
        lv_obj_set_size(badge, 46, 46);
        lv_obj_set_style_radius(badge, 8, 0);
        lv_obj_set_style_bg_color(badge,
            isH ? lv_color_hex(0xD97706) : lv_color_hex(CLR_PRIMARY), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        // Pack ci (3 bits) and slot index i (5 bits) into user_data
        int packed = (ci << 5) | i;
        lv_obj_add_event_cb(badge, [](lv_event_t *ev) {
            int pk  = (int)(intptr_t)lv_event_get_user_data(ev);
            int c   = (pk >> 5) & 0x7;
            int idx = pk & 0x1F;
            int ln  = g_store.customSequenzLen[c];
            if (idx >= ln) return;
            for (int j = idx; j < ln - 1; j++)
                g_store.customSequenzen[c][j] = g_store.customSequenzen[c][j + 1];
            g_store.customSequenzLen[c]--;
            game_store_save();
            refresh_custom_seq(c);
            refresh_custom_stats(c);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)packed);
        lv_obj_t *lbl = lv_label_create(badge);
        lv_label_set_text(lbl, MACH_LBL[(int)m]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(lbl);
    }
}

// ── Build ─────────────────────────────────────────────────────
static lv_obj_t *build_custom_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 14, 0);
    lv_obj_set_style_pad_row(parent, 12, 0);

    for (int ci = 0; ci < 4; ci++) {

        // ── Card ──────────────────────────────────────────────
        lv_obj_t *card = lv_obj_create(parent);
        lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_add_style(card, &g_style_card, 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_style_pad_row(card, 8, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        // ── Title + CLEAR button ───────────────────────────────
        lv_obj_t *title_row = lv_obj_create(card);
        lv_obj_set_size(title_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(title_row, LV_OPA_0, 0);
        lv_obj_set_style_border_width(title_row, 0, 0);
        lv_obj_set_style_pad_all(title_row, 0, 0);
        lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *cname = lv_label_create(title_row);
        lv_label_set_text(cname, CUSTOM_NAMES[ci]);
        lv_obj_set_style_text_font(cname, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(cname, lv_color_hex(CLR_TEXT), 0);

        lv_obj_t *clr = lv_btn_create(title_row);
        lv_obj_add_style(clr, &g_style_btn_secondary, 0);
        lv_obj_set_size(clr, 120, 34);
        lv_obj_add_event_cb(clr, [](lv_event_t *ev) {
            int c = (int)(intptr_t)lv_event_get_user_data(ev);
            g_store.customSequenzLen[c] = 0;
            game_store_save();
            refresh_custom_seq(c);
            refresh_custom_stats(c);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)ci);
        lv_obj_t *clrl = lv_label_create(clr);
        lv_label_set_text(clrl, LV_SYMBOL_CLOSE "  LOESCHEN");
        lv_obj_set_style_text_font(clrl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(clrl, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(clrl);

        // ── Section label: SEQUENZ ────────────────────────────
        lv_obj_t *seq_hdr = lv_label_create(card);
        lv_label_set_text(seq_hdr, "SEQUENZ  (tippen op eng Schanz fir ze loeschen)");
        lv_obj_set_style_text_font(seq_hdr, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(seq_hdr, lv_color_hex(CLR_MUTED), 0);

        // ── Sequence strip (horizontally scrollable) ──────────
        lv_obj_t *seq_cont = lv_obj_create(card);
        s_custom_seq_cont[ci] = seq_cont;
        lv_obj_set_size(seq_cont, LV_PCT(100), 62);
        lv_obj_set_style_bg_color(seq_cont, lv_color_hex(0x0A0A0A), 0);
        lv_obj_set_style_bg_opa(seq_cont, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(seq_cont, 1, 0);
        lv_obj_set_style_border_color(seq_cont, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_radius(seq_cont, 8, 0);
        lv_obj_set_style_pad_all(seq_cont, 6, 0);
        lv_obj_set_style_pad_column(seq_cont, 4, 0);
        lv_obj_set_flex_flow(seq_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(seq_cont, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scroll_dir(seq_cont, LV_DIR_HOR);
        lv_obj_clear_flag(seq_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);

        refresh_custom_seq(ci);

        // ── Section label: ADD ────────────────────────────────
        lv_obj_t *add_hdr = lv_label_create(card);
        lv_label_set_text(add_hdr, "SCHANZ DOBAISETZEN");
        lv_obj_set_style_text_font(add_hdr, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(add_hdr, lv_color_hex(CLR_MUTED), 0);

        // ── 8 machine add-buttons (A-H) ───────────────────────
        lv_obj_t *add_row = lv_obj_create(card);
        lv_obj_set_size(add_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(add_row, LV_OPA_0, 0);
        lv_obj_set_style_border_width(add_row, 0, 0);
        lv_obj_set_style_pad_all(add_row, 0, 0);
        lv_obj_set_style_pad_column(add_row, 6, 0);
        lv_obj_set_flex_flow(add_row, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(add_row, LV_OBJ_FLAG_SCROLLABLE);

        for (int mi = 0; mi < MASCHINE_COUNT; mi++) {
            bool isH = (mi == (int)MASCHINE_H);
            lv_obj_t *ab = lv_btn_create(add_row);
            lv_obj_set_size(ab, 52, 52);
            lv_obj_set_style_radius(ab, 8, 0);
            lv_obj_set_style_bg_color(ab,
                isH ? lv_color_hex(0x78350F) : lv_color_hex(CLR_SIDEBAR), 0);
            lv_obj_set_style_bg_opa(ab, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(ab, 2, 0);
            lv_obj_set_style_border_color(ab,
                isH ? lv_color_hex(0xD97706) : lv_color_hex(CLR_PRIMARY), 0);
            // Pack ci (4 bits) and mi (4 bits)
            int packed = (ci << 4) | mi;
            lv_obj_add_event_cb(ab, [](lv_event_t *ev) {
                int pk = (int)(intptr_t)lv_event_get_user_data(ev);
                int c  = (pk >> 4) & 0xF;
                int m  = pk & 0xF;
                if (g_store.customSequenzLen[c] >= CUSTOM_SEQ_MAX) return;
                g_store.customSequenzen[c][g_store.customSequenzLen[c]] = (Maschine)m;
                g_store.customSequenzLen[c]++;
                game_store_save();
                refresh_custom_seq(c);
                refresh_custom_stats(c);
            }, LV_EVENT_CLICKED, (void*)(intptr_t)packed);
            lv_obj_t *ml = lv_label_create(ab);
            lv_label_set_text(ml, MACH_LBL[mi]);
            lv_obj_set_style_text_font(ml, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(ml, lv_color_hex(CLR_TEXT), 0);
            lv_obj_center(ml);
        }

        // ── Läufe toggle (1 / 2) ─────────────────────────────
        lv_obj_t *lauf_hdr = lv_label_create(card);
        lv_label_set_text(lauf_hdr, "UNZUEL VUN DE LAEUF");
        lv_obj_set_style_text_font(lauf_hdr, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lauf_hdr, lv_color_hex(CLR_MUTED), 0);

        lv_obj_t *lauf_row = lv_obj_create(card);
        lv_obj_set_size(lauf_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(lauf_row, LV_OPA_0, 0);
        lv_obj_set_style_border_width(lauf_row, 0, 0);
        lv_obj_set_style_pad_all(lauf_row, 0, 0);
        lv_obj_set_style_pad_column(lauf_row, 8, 0);
        lv_obj_set_flex_flow(lauf_row, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(lauf_row, LV_OBJ_FLAG_SCROLLABLE);

        for (int li = 0; li < 2; li++) {
            bool sel = (g_store.customLaeufe[ci] == li + 1);
            lv_obj_t *lb = lv_btn_create(lauf_row);
            s_lauf_btn[ci][li] = lb;
            lv_obj_set_size(lb, 160, 44);
            lv_obj_set_style_radius(lb, 8, 0);
            lv_obj_set_style_bg_color(lb,
                sel ? lv_color_hex(CLR_PRIMARY) : lv_color_hex(CLR_SIDEBAR), 0);
            lv_obj_set_style_bg_opa(lb, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(lb, 2, 0);
            lv_obj_set_style_border_color(lb,
                sel ? lv_color_hex(CLR_PRIMARY) : lv_color_hex(CLR_BORDER), 0);
            int packed = (ci << 4) | li;
            lv_obj_add_event_cb(lb, [](lv_event_t *ev) {
                int pk = (int)(intptr_t)lv_event_get_user_data(ev);
                int c  = (pk >> 4) & 0xF;
                int l  = (pk & 0xF) + 1;   // 1 or 2
                g_store.customLaeufe[c] = l;
                game_store_save();
                for (int x = 0; x < 2; x++) {
                    bool s = (g_store.customLaeufe[c] == x + 1);
                    lv_obj_set_style_bg_color(s_lauf_btn[c][x],
                        s ? lv_color_hex(CLR_PRIMARY) : lv_color_hex(CLR_SIDEBAR), 0);
                    lv_obj_set_style_border_color(s_lauf_btn[c][x],
                        s ? lv_color_hex(CLR_PRIMARY) : lv_color_hex(CLR_BORDER), 0);
                    lv_obj_invalidate(s_lauf_btn[c][x]);
                }
                refresh_custom_stats(c);
            }, LV_EVENT_CLICKED, (void*)(intptr_t)packed);
            lv_obj_t *ll = lv_label_create(lb);
            char lbuf[16];
            snprintf(lbuf, sizeof(lbuf), "%d %s", li + 1, li == 0 ? "LAUF" : "LAEUF");
            lv_label_set_text(ll, lbuf);
            lv_obj_set_style_text_font(ll, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(ll, lv_color_hex(CLR_TEXT), 0);
            lv_obj_center(ll);
        }

        // ── Live stats (Tauben / Max Pkt Lauf / Max Pkt Spill) ─
        lv_obj_t *stats_row = lv_obj_create(card);
        lv_obj_set_size(stats_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(stats_row, LV_OPA_0, 0);
        lv_obj_set_style_border_width(stats_row, 0, 0);
        lv_obj_set_style_pad_all(stats_row, 0, 0);
        lv_obj_set_style_pad_column(stats_row, 6, 0);
        lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(stats_row, LV_OBJ_FLAG_SCROLLABLE);

        static const char *stat_lbl[] = {"TAUBEN/LAUF","MAX PKT/LAUF","MAX PKT/SPILL"};
        lv_obj_t **stat_ptrs[3] = {
            &s_stat_tauben[ci], &s_stat_pkt_lauf[ci], &s_stat_pkt_spiel[ci]
        };
        for (int si = 0; si < 3; si++) {
            lv_obj_t *sc = lv_obj_create(stats_row);
            lv_obj_set_height(sc, LV_SIZE_CONTENT);
            lv_obj_set_flex_grow(sc, 1);
            lv_obj_add_style(sc, &g_style_card, 0);
            lv_obj_set_style_pad_all(sc, 8, 0);
            lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(sc, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_clear_flag(sc, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *sv = lv_label_create(sc);
            *stat_ptrs[si] = sv;
            lv_label_set_text(sv, "0");
            lv_obj_set_style_text_font(sv, &lv_font_montserrat_22, 0);
            lv_obj_set_style_text_color(sv, lv_color_hex(CLR_PRIMARY), 0);

            lv_obj_t *sl = lv_label_create(sc);
            lv_label_set_text(sl, stat_lbl[si]);
            lv_obj_set_style_text_font(sl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(sl, lv_color_hex(CLR_MUTED), 0);
        }

        refresh_custom_stats(ci);
    }
    return parent;
}

// ── Tab: WiFi ─────────────────────────────────────────────────
static lv_obj_t *build_wifi_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 16, 0);

    lv_obj_t *info = lv_label_create(parent);
    lv_label_set_text(info, "WiFi ASTELLUNGEN kann op der WiFi-SAit geAnnert ginn.");
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(CLR_MUTED), 0);

    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_add_style(btn, &g_style_btn_primary, 0);
    lv_obj_set_size(btn, 200, 44);
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        ui_manager_show(SCREEN_WIFI);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, LV_SYMBOL_WIFI "  WIFI SAEIT");
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
    lv_label_set_text(info, "BLUETOOTH HID KEYBOARD KOPPLUNG.");
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(CLR_MUTED), 0);

    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_add_style(btn, &g_style_btn_primary, 0);
    lv_obj_set_size(btn, 240, 44);
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        ui_manager_show(SCREEN_BLUETOOTH);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, LV_SYMBOL_BLUETOOTH "  BLUETOOTH SAEIT");
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
        "FIRMWARE: " APP_VERSION,
        "BOARD: GUITION JC8012P4A1C-I-W-Y",
        "SOC: ESP32-P4NRW32",
        "KOPROZESSOR: ESP32-C6-MINI-1U-N4",
        "Display: 10.1\" MIPI-DSI JD9365 1280x800",
        "TOUCH: GSL3680 I2C",
        "PORTAL: " DEFAULT_API_URL,
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
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  ASTELLUNGEN");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_PRIMARY), 0);

    lv_obj_t *back = lv_btn_create(hdr);
    lv_obj_add_style(back, &g_style_btn_secondary, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *e) {
        ui_manager_show(SCREEN_DASHBOARD);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl2 = lv_label_create(back);
    lv_label_set_text(bl2, LV_SYMBOL_HOME "  ZURUCK");
    lv_obj_set_style_text_color(bl2, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(bl2);

    // Tab view
    s_tab_view = lv_tabview_create(s_scr);
    lv_obj_set_size(s_tab_view, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H - 70);
    lv_obj_align(s_tab_view, LV_ALIGN_TOP_LEFT, 0, 70);
    lv_tabview_set_tab_bar_size(s_tab_view, 48);
    lv_obj_set_style_bg_color(s_tab_view, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_text_font(s_tab_view, &lv_font_montserrat_14, 0);

    lv_obj_t *tab_api    = lv_tabview_add_tab(s_tab_view, "Portal API");
    lv_obj_t *tab_mach   = lv_tabview_add_tab(s_tab_view, "MASCHINNEN");
    lv_obj_t *tab_custom = lv_tabview_add_tab(s_tab_view, "CUSTOM");
    lv_obj_t *tab_wifi   = lv_tabview_add_tab(s_tab_view, "WiFi");
    lv_obj_t *tab_bt     = lv_tabview_add_tab(s_tab_view, "Bluetooth");
    lv_obj_t *tab_sys    = lv_tabview_add_tab(s_tab_view, "System");

    build_api_tab(tab_api);
    build_mach_tab(tab_mach);
    build_custom_tab(tab_custom);
    build_wifi_tab(tab_wifi);
    build_bt_tab(tab_bt);
    build_system_tab(tab_sys);

    // ── On-screen keyboard ────────────────────────────────────────
    // Created last (after all tabs) so it renders on top.
    s_kb = lv_keyboard_create(s_scr);
    // Explicit height — LV_SIZE_CONTENT computes oversized on 1280×800
    lv_obj_set_size(s_kb, DISPLAY_LOGICAL_W, 320);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

    // Dark theme styling (default theme renders white-on-white)
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_kb, 0, 0);
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(CLR_BORDER), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_kb, lv_color_hex(CLR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(s_kb, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_kb, lv_color_hex(CLR_BG), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(s_kb, lv_color_hex(CLR_PRIMARY),
                              (lv_style_selector_t)((uint32_t)LV_PART_ITEMS | (uint32_t)LV_STATE_PRESSED));
    lv_obj_set_style_text_color(s_kb, lv_color_hex(0xFFFFFF),
                                (lv_style_selector_t)((uint32_t)LV_PART_ITEMS | (uint32_t)LV_STATE_PRESSED));

    lv_obj_add_event_cb(s_kb, [](lv_event_t *e) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_kb, NULL);
    }, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_kb, [](lv_event_t *e) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_kb, NULL);
    }, LV_EVENT_CANCEL, NULL);

    // API tab textareas
    lv_obj_add_event_cb(s_ta_url, [](lv_event_t *e) {
        lv_keyboard_set_textarea(s_kb, s_ta_url);
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_key, [](lv_event_t *e) {
        lv_keyboard_set_textarea(s_kb, s_ta_key);
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, NULL);

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
