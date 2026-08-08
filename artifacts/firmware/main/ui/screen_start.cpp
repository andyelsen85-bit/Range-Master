// ============================================================
// Start screen — game setup: assign players, pick mode
// Mirrors StartScreen.tsx
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_start.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_player_rows[MAX_SPIELER];
static lv_obj_t *s_player_dropdowns[MAX_SPIELER];
static lv_obj_t *s_maschinen_btns[MASCHINE_COUNT];
static lv_obj_t *s_lbl_error;

// ── Build player dropdown options string ──────────────────────
static void build_player_opts(char *buf, size_t len)
{
    // "--- Kee Spiller ---\nName1\nName2\n..."
    strncpy(buf, "--- Kee Spiller ---", len - 1);
    buf[len-1] = '\0';
    for (int i = 0; i < g_store.portalSpielerCount; i++) {
        strncat(buf, "\n", len - strlen(buf) - 1);
        strncat(buf, g_store.portalSpieler[i].name, len - strlen(buf) - 1);
    }
}

// ── Start button callback ─────────────────────────────────────
static void start_cb(lv_event_t *e)
{
    // Collect selected players from dropdowns
    g_store.spielerCount = 0;
    for (int i = 0; i < MAX_SPIELER; i++) {
        if (!s_player_dropdowns[i]) continue;
        uint16_t sel = lv_dropdown_get_selected(s_player_dropdowns[i]);
        if (sel == 0) continue; // "--- Kee Spiller ---"
        int pidx = (int)sel - 1;
        if (pidx >= g_store.portalSpielerCount) continue;
        PortalSpieler *ps = &g_store.portalSpieler[pidx];

        Spieler *sp = &g_store.spieler[g_store.spielerCount];
        sp->id          = ps->id;
        sp->startPosten = g_store.spielerCount + 1;
        sp->punkte      = 0;
        snprintf(sp->name, MAX_NAME_LEN, "%s", ps->name);
        g_store.spielerCount++;
    }

    if (g_store.spielerCount == 0) {
        lv_label_set_text(s_lbl_error, "Mindestens 1 Spiller auswiele!");
        lv_obj_set_style_text_color(s_lbl_error, lv_color_hex(CLR_DANGER), 0);
        return;
    }

    if (!store_start_spiel()) {
        lv_label_set_text(s_lbl_error, "Fehler: Spiller hunn keng Kreditter!");
        lv_obj_set_style_text_color(s_lbl_error, lv_color_hex(CLR_DANGER), 0);
        return;
    }
    // store_start_spiel sets g_store.screen = SCREEN_SPIEL
    // ui_manager_tick will pick that up
}

// ── Modus button callback ─────────────────────────────────────
static void modus_cb(lv_event_t *e)
{
    Modus m = (Modus)(intptr_t)lv_event_get_user_data(e);
    g_store.modus = m;
}

// ── Machine toggle callback ───────────────────────────────────
static void machine_toggle_cb(lv_event_t *e)
{
    Maschine m = (Maschine)(intptr_t)lv_event_get_user_data(e);
    g_store.maschinenAktiv[m] = !g_store.maschinenAktiv[m];
    lv_obj_t *btn = lv_event_get_target_obj(e);
    if (g_store.maschinenAktiv[m]) {
        lv_obj_add_style(btn, &g_style_btn_primary, 0);
    } else {
        lv_obj_add_style(btn, &g_style_btn_secondary, 0);
    }
}

// ── screen_start_create ───────────────────────────────────────
lv_obj_t *screen_start_create(void)
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

    lv_obj_t *hdr_lbl = lv_label_create(hdr);
    lv_label_set_text(hdr_lbl, LV_SYMBOL_PLAY "  SPILL STARTEN");
    lv_obj_set_style_text_font(hdr_lbl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(hdr_lbl, lv_color_hex(CLR_PRIMARY), 0);
    lv_obj_align(hdr_lbl, LV_ALIGN_LEFT_MID, 20, 0);

    lv_obj_t *back_btn = lv_btn_create(hdr);
    lv_obj_add_style(back_btn, &g_style_btn_secondary, 0);
    lv_obj_align(back_btn, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        ui_manager_show(SCREEN_DASHBOARD);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_HOME "  Zréck");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(back_lbl);

    // Content area with flex row
    lv_obj_t *content = lv_obj_create(s_scr);
    lv_obj_set_size(content, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H - 70);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, 70);
    lv_obj_set_style_bg_color(content, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(content, 16, 0);

    // Left column: players
    lv_obj_t *left = lv_obj_create(content);
    lv_obj_set_size(left, 480, LV_PCT(100));
    lv_obj_set_style_bg_opa(left, LV_OPA_0, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(left, 10, 0);

    lv_obj_t *players_hdr = lv_label_create(left);
    lv_label_set_text(players_hdr, "Spiller zouweisen");
    lv_obj_set_style_text_font(players_hdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(players_hdr, lv_color_hex(CLR_TEXT), 0);

    // static: ~13 KB — too large for the LVGL task stack as a local variable.
    static char opts[MAX_PORTAL_SPIELER * (MAX_NAME_LEN + 1) + 32];
    build_player_opts(opts, sizeof(opts));

    for (int i = 0; i < MAX_SPIELER; i++) {
        lv_obj_t *row = lv_obj_create(left);
        lv_obj_set_size(row, LV_PCT(100), 52);
        lv_obj_set_style_bg_color(row, lv_color_hex(CLR_CARD), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_add_style(row, &g_style_card, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 10, 0);

        char pos_str[8];
        snprintf(pos_str, sizeof(pos_str), "P%d", i + 1);
        lv_obj_t *pos_lbl = lv_label_create(row);
        lv_label_set_text(pos_lbl, pos_str);
        lv_obj_set_style_text_font(pos_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(pos_lbl, lv_color_hex(CLR_PRIMARY), 0);
        lv_obj_set_width(pos_lbl, 32);

        lv_obj_t *dd = lv_dropdown_create(row);
        lv_dropdown_set_options(dd, opts);
        lv_obj_set_flex_grow(dd, 1);
        lv_obj_set_height(dd, 40);
        lv_obj_set_style_text_font(dd, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_color(dd, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_text_color(dd, lv_color_hex(CLR_TEXT), 0);
        s_player_dropdowns[i] = dd;
        s_player_rows[i] = row;
    }

    s_lbl_error = lv_label_create(left);
    lv_label_set_text(s_lbl_error, "");
    lv_obj_set_style_text_font(s_lbl_error, &lv_font_montserrat_14, 0);

    // Right column: modus + machines + start button
    lv_obj_t *right = lv_obj_create(content);
    lv_obj_set_flex_grow(right, 1);
    lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_style_bg_opa(right, LV_OPA_0, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(right, 14, 0);

    // Modus buttons
    lv_obj_t *modus_hdr = lv_label_create(right);
    lv_label_set_text(modus_hdr, "Spillmodus");
    lv_obj_set_style_text_font(modus_hdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(modus_hdr, lv_color_hex(CLR_TEXT), 0);

    static const char *MODUS_NAMES[] = {"Normal","Harakiri","Custom 1","Custom 2","Custom 3","Custom 4"};
    lv_obj_t *modus_grid = lv_obj_create(right);
    lv_obj_set_size(modus_grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(modus_grid, LV_OPA_0, 0);
    lv_obj_set_style_border_width(modus_grid, 0, 0);
    lv_obj_set_style_pad_all(modus_grid, 0, 0);
    lv_obj_set_flex_flow(modus_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(modus_grid, 8, 0);
    lv_obj_set_style_pad_column(modus_grid, 8, 0);

    for (int m = 0; m < MODUS_COUNT; m++) {
        lv_obj_t *mb = lv_btn_create(modus_grid);
        lv_obj_add_style(mb, (m == 0) ? &g_style_btn_primary : &g_style_btn_secondary, 0);
        lv_obj_set_size(mb, 130, 44);
        lv_obj_add_event_cb(mb, modus_cb, LV_EVENT_CLICKED, (void*)(intptr_t)m);
        lv_obj_t *ml = lv_label_create(mb);
        lv_label_set_text(ml, MODUS_NAMES[m]);
        lv_obj_set_style_text_font(ml, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ml, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(ml);
    }

    // Machine toggles
    lv_obj_t *mach_hdr = lv_label_create(right);
    lv_label_set_text(mach_hdr, "Aktiv Maschinnen");
    lv_obj_set_style_text_font(mach_hdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(mach_hdr, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *mach_row = lv_obj_create(right);
    lv_obj_set_size(mach_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(mach_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(mach_row, 0, 0);
    lv_obj_set_style_pad_all(mach_row, 0, 0);
    lv_obj_set_flex_flow(mach_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(mach_row, 8, 0);

    static const char *MACH_LABELS[] = {"A","B","C","D","E","F","G","H"};
    for (int m = 0; m < MASCHINE_COUNT; m++) {
        lv_obj_t *mb = lv_btn_create(mach_row);
        lv_obj_add_style(mb, &g_style_btn_primary, 0);
        lv_obj_set_size(mb, 52, 52);
        lv_obj_add_event_cb(mb, machine_toggle_cb, LV_EVENT_CLICKED, (void*)(intptr_t)m);
        s_maschinen_btns[m] = mb;
        lv_obj_t *ml = lv_label_create(mb);
        lv_label_set_text(ml, MACH_LABELS[m]);
        lv_obj_set_style_text_font(ml, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(ml, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(ml);
    }

    // START button (large, bottom)
    lv_obj_t *start_btn = lv_btn_create(right);
    lv_obj_add_style(start_btn, &g_style_btn_primary, 0);
    lv_obj_set_size(start_btn, LV_PCT(100), 70);
    lv_obj_add_event_cb(start_btn, start_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *start_lbl = lv_label_create(start_btn);
    lv_label_set_text(start_lbl, LV_SYMBOL_PLAY "  SPILL STARTEN");
    lv_obj_set_style_text_font(start_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(start_lbl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(start_lbl);

    return s_scr;
}

void screen_start_refresh(void)
{
    // Rebuild dropdown options when portal players change
    if (!s_player_dropdowns[0]) return;
    static char opts[MAX_PORTAL_SPIELER * (MAX_NAME_LEN + 1) + 32];
    build_player_opts(opts, sizeof(opts));
    for (int i = 0; i < MAX_SPIELER; i++) {
        if (s_player_dropdowns[i])
            lv_dropdown_set_options(s_player_dropdowns[i], opts);
    }
    // Sync machine button states
    for (int m = 0; m < MASCHINE_COUNT; m++) {
        if (!s_maschinen_btns[m]) continue;
        if (g_store.maschinenAktiv[m]) {
            lv_obj_add_style(s_maschinen_btns[m], &g_style_btn_primary, 0);
        } else {
            lv_obj_add_style(s_maschinen_btns[m], &g_style_btn_secondary, 0);
        }
        lv_obj_invalidate(s_maschinen_btns[m]);
    }
    lv_label_set_text(s_lbl_error, "");
}
