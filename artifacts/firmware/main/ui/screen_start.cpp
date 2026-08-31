// ============================================================
// Start screen - game setup: assign players, pick mode
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
static lv_obj_t *s_modus_btns[MODUS_COUNT];
static lv_obj_t *s_lbl_error;

// option-index → portalSpieler[] index (index 0 = "Kee SPILLER" sentinel)
static int s_option_to_pidx[MAX_PORTAL_SPIELER + 1];
static int s_options_count = 0;
static bool s_refreshing_dropdowns;
static int option_for_player_id(int id);

static void refresh_lineup_selections(void)
{
    s_refreshing_dropdowns = true;
    for (int i = 0; i < MAX_SPIELER; ++i)
        if (s_player_dropdowns[i])
            lv_dropdown_set_selected(s_player_dropdowns[i],
                                     option_for_player_id(g_store.lineupIds[i]));
    s_refreshing_dropdowns = false;
    lv_label_set_text(s_lbl_error, "");
}

static int option_for_player_id(int id)
{
    for (int o = 1; o < s_options_count; ++o) {
        int pidx = s_option_to_pidx[o];
        if (pidx >= 0 && pidx < g_store.portalSpielerCount &&
            g_store.portalSpieler[pidx].id == id) return o;
    }
    return 0;
}

// ── Build player dropdown options string ──────────────────────
// Only includes players that have at least 1 day-credit remaining.
// Populates s_option_to_pidx[] so start_cb can look up the correct
// portal player without relying on a direct sel-1 index offset.
static void build_player_opts(char *buf, size_t len)
{
    strncpy(buf, "--- Kee SPILLER ---", len - 1);
    buf[len-1] = '\0';
    s_options_count = 1;  // option 0 = "Kee SPILLER"

    // ── 1. Collect eligible players (credit > 0) ──────────────
    int eligible[MAX_PORTAL_SPIELER];
    int eligible_count = 0;
    for (int i = 0; i < g_store.portalSpielerCount && eligible_count < MAX_PORTAL_SPIELER; i++) {
        if (store_kredite_verfuegbar(g_store.portalSpieler[i].id) <= 0) continue;
        eligible[eligible_count++] = i;
    }

    // ── 2. Count appearances in local history per player ──────
    // Most frequent players float to the top for faster selection.
    int game_counts[MAX_PORTAL_SPIELER] = {};
    for (int e = 0; e < eligible_count; e++) {
        int pid = g_store.portalSpieler[eligible[e]].id;
        int cnt = 0;
        for (int h = 0; h < g_store.historyCount; h++) {
            for (int s = 0; s < MAX_SPIELER; s++) {
                if (g_store.history[h].spielerIds[s] == pid) { cnt++; break; }
            }
        }
        game_counts[e] = cnt;
    }

    // ── 3. Insertion sort: descending by game count ───────────
    for (int i = 1; i < eligible_count; i++) {
        int ki = eligible[i], kc = game_counts[i], j = i - 1;
        while (j >= 0 && game_counts[j] < kc) {
            eligible[j + 1]    = eligible[j];
            game_counts[j + 1] = game_counts[j];
            j--;
        }
        eligible[j + 1]    = ki;
        game_counts[j + 1] = kc;
    }

    // ── 4. Build dropdown string in sorted order ──────────────
    for (int e = 0; e < eligible_count; e++) {
        if (s_options_count > MAX_PORTAL_SPIELER) break;
        int i = eligible[e];
        s_option_to_pidx[s_options_count] = i;
        s_options_count++;
        strncat(buf, "\n", len - strlen(buf) - 1);
        strncat(buf, g_store.portalSpieler[i].name, len - strlen(buf) - 1);
    }
}

// ── Start button callback ─────────────────────────────────────
static void start_cb(lv_event_t *e)
{
    bool any = false;
    for (int i = 0; i < MAX_SPIELER; ++i) if (g_store.lineupIds[i]) any = true;
    if (!any) {
        lv_label_set_text(s_lbl_error, "Mindestens 1 SPILLER auswiele!");
        lv_obj_set_style_text_color(s_lbl_error, lv_color_hex(CLR_DANGER), 0);
        return;
    }

    if (!store_start_spiel()) {
        lv_label_set_text(s_lbl_error, g_store.lineupWarning[0]
            ? g_store.lineupWarning : "Fehler: SPILLER hunn keng Kreditter!");
        lv_obj_set_style_text_color(s_lbl_error, lv_color_hex(CLR_DANGER), 0);
        return;
    }
    // store_start_spiel sets g_store.screen = SCREEN_SPIEL
    // ui_manager_tick will pick that up
}

static void dropdown_changed_cb(lv_event_t *e)
{
    if (s_refreshing_dropdowns) return;
    int post = (int)(intptr_t)lv_event_get_user_data(e);
    uint16_t selected = lv_dropdown_get_selected(lv_event_get_target_obj(e));
    int id = 0;
    if (selected > 0 && selected < s_options_count) {
        int pidx = s_option_to_pidx[selected];
        if (pidx >= 0 && pidx < g_store.portalSpielerCount) id = g_store.portalSpieler[pidx].id;
    }
    store_set_lineup_post(post, id);
    refresh_lineup_selections();
}

static void lineup_action_cb(lv_event_t *e)
{
    intptr_t action = (intptr_t)lv_event_get_user_data(e);
    if (action == 0) store_clear_lineup();
    else if (action == 1) store_mix_lineup();
    else {
        int post = (int)(action >> 8);
        int direction = (action & 0xff) == 1 ? -1 : 1;
        store_move_lineup(post, direction);
    }
    refresh_lineup_selections();
}

// ── Modus button callback ─────────────────────────────────────
static void update_modus_styles(void)
{
    for (int i = 0; i < MODUS_COUNT; i++) {
        if (!s_modus_btns[i]) continue;
        lv_obj_set_style_bg_color(s_modus_btns[i],
            (i == (int)g_store.modus)
                ? lv_color_hex(CLR_PRIMARY)
                : lv_color_hex(CLR_SIDEBAR), 0);
        lv_obj_set_style_bg_opa(s_modus_btns[i], LV_OPA_COVER, 0);
        lv_obj_invalidate(s_modus_btns[i]);
    }
}

static void modus_cb(lv_event_t *e)
{
    Modus m = (Modus)(intptr_t)lv_event_get_user_data(e);
    g_store.modus = m;
    update_modus_styles();
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
    if (store_prepare_current_day())
        store_reconcile_lineup_with_credits();
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
    lv_label_set_text(back_lbl, LV_SYMBOL_HOME "  ZURUCK");
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
    lv_label_set_text(players_hdr, "SPILLER zouweisen");
    lv_obj_set_style_text_font(players_hdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(players_hdr, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *tools = lv_obj_create(left);
    lv_obj_set_size(tools, LV_PCT(100), 42);
    lv_obj_set_style_bg_opa(tools, LV_OPA_0, 0);
    lv_obj_set_style_border_width(tools, 0, 0);
    lv_obj_set_style_pad_all(tools, 0, 0);
    lv_obj_set_flex_flow(tools, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tools, 8, 0);
    const char *tool_names[] = {"CLEAR ALL", "MIX"};
    for (int t = 0; t < 2; ++t) {
        lv_obj_t *btn = lv_btn_create(tools);
        lv_obj_set_size(btn, 150, 38);
        lv_obj_add_style(btn, t == 0 ? &g_style_btn_secondary : &g_style_btn_primary, 0);
        lv_obj_add_event_cb(btn, lineup_action_cb, LV_EVENT_CLICKED, (void *)(intptr_t)t);
        lv_obj_t *lbl = lv_label_create(btn); lv_label_set_text(lbl, tool_names[t]); lv_obj_center(lbl);
    }

    // Heap-allocated: ~13 KB is too large for stack and too large for static BSS
    // (static BSS lands in internal DRAM which is already scarce on ESP32-P4).
    // lv_dropdown_set_options() calls lv_strdup() internally, so free() is safe
    // immediately after the dropdown calls below.
    const size_t opts_sz = MAX_PORTAL_SPIELER * (MAX_NAME_LEN + 1) + 64;
    char *opts = (char *)malloc(opts_sz);
    if (opts) build_player_opts(opts, opts_sz);

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
        lv_dropdown_set_options(dd, opts ? opts : "-"); // safe if malloc failed
        lv_obj_set_flex_grow(dd, 1);
        lv_obj_set_height(dd, 40);
        lv_obj_set_style_text_font(dd, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_color(dd, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_text_color(dd, lv_color_hex(CLR_TEXT), 0);
        s_player_dropdowns[i] = dd;
        s_player_rows[i] = row;
        lv_obj_add_event_cb(dd, dropdown_changed_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)(i + 1));
        lv_dropdown_set_selected(dd, option_for_player_id(g_store.lineupIds[i]));

        for (int dir = 0; dir < 2; ++dir) {
            lv_obj_t *move = lv_btn_create(row);
            // Make the reorder controls easy to hit with a finger and keep
            // the existing 10px row spacing between them.
            lv_obj_set_size(move, 48, 44);
            lv_obj_add_style(move, &g_style_btn_secondary, 0);
            intptr_t action = ((intptr_t)(i + 1) << 8) | (dir == 0 ? 1 : 2);
            lv_obj_add_event_cb(move, lineup_action_cb, LV_EVENT_CLICKED, (void *)action);
            lv_obj_t *label = lv_label_create(move);
            lv_label_set_text(label, dir == 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
            lv_obj_center(label);
        }
    }
    free(opts); // lv_dropdown_set_options copies via lv_strdup - safe to free now

    s_lbl_error = lv_label_create(left);
    lv_label_set_text(s_lbl_error, g_store.lineupWarning);
    if (g_store.lineupWarning[0])
        lv_obj_set_style_text_color(s_lbl_error, lv_color_hex(CLR_DANGER), 0);
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
    lv_label_set_text(modus_hdr, "SPILLMODUS");
    lv_obj_set_style_text_font(modus_hdr, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(modus_hdr, lv_color_hex(CLR_TEXT), 0);

    static const char *MODUS_NAMES[] = {"NORMAL","HARAKIRI","CUSTOM 1","CUSTOM 2","CUSTOM 3","CUSTOM 4"};
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
        lv_obj_set_size(mb, 130, 44);
        lv_obj_set_style_bg_color(mb,
            (m == (int)g_store.modus) ? lv_color_hex(CLR_PRIMARY) : lv_color_hex(CLR_SIDEBAR), 0);
        lv_obj_set_style_bg_opa(mb, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(mb, 8, 0);
        lv_obj_set_style_border_width(mb, 0, 0);
        lv_obj_add_event_cb(mb, modus_cb, LV_EVENT_CLICKED, (void*)(intptr_t)m);
        s_modus_btns[m] = mb;
        lv_obj_t *ml = lv_label_create(mb);
        lv_label_set_text(ml, MODUS_NAMES[m]);
        lv_obj_set_style_text_font(ml, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ml, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(ml);
    }

    // Machine toggles
    lv_obj_t *mach_hdr = lv_label_create(right);
    lv_label_set_text(mach_hdr, "Aktiv MASCHINNEN");
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
    if (store_prepare_current_day())
        store_reconcile_lineup_with_credits();
    // Rebuild dropdown options when portal players change
    if (!s_player_dropdowns[0]) return;
    const size_t opts_sz = MAX_PORTAL_SPIELER * (MAX_NAME_LEN + 1) + 64;
    char *opts = (char *)malloc(opts_sz);
    if (!opts) return;
    build_player_opts(opts, opts_sz);
    s_refreshing_dropdowns = true;
    for (int i = 0; i < MAX_SPIELER; i++) {
        if (s_player_dropdowns[i])
            lv_dropdown_set_options(s_player_dropdowns[i], opts);
        if (s_player_dropdowns[i])
            lv_dropdown_set_selected(s_player_dropdowns[i],
                option_for_player_id(g_store.lineupIds[i]));
    }
    s_refreshing_dropdowns = false;
    free(opts);
    // Sync modus button states
    update_modus_styles();
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
