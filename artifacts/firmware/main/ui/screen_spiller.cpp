// ============================================================
// Spillerverwaltung — player management terminal screen
// Two-panel layout: left = add-local + edit form
//                   right = search field + scrollable player list
// Keyboard is created LAST so it renders above the list.
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_spiller.h"

// ── Statics ───────────────────────────────────────────────────
static lv_obj_t *s_scr;
static lv_obj_t *s_list;
static lv_obj_t *s_ta_search;
static lv_obj_t *s_ta_new_name;
static lv_obj_t *s_ta_name;
static lv_obj_t *s_ta_email;
static lv_obj_t *s_placeholder;
static lv_obj_t *s_edit_form;
static lv_obj_t *s_aktiv_btn;
static lv_obj_t *s_lbl_aktiv;
static lv_obj_t *s_day_btn;
static lv_obj_t *s_lbl_day;
static lv_obj_t *s_lbl_status;
static lv_obj_t *s_lbl_edit_status;
static lv_obj_t *s_lbl_pending;
static lv_obj_t *s_kb;

static int  s_selected_pidx = -1;
static bool s_edit_aktiv    = false;
static char s_search_buf[64] = {0};

// ── Forward declarations ──────────────────────────────────────
static void build_list(void);
static void open_edit(int pidx);
static void close_edit(void);
static void update_aktiv_btn(void);
static void update_day_btn(void);
static void update_pending_badge(void);

// ── Case-insensitive substring search ────────────────────────
static bool str_icontains(const char *hay, const char *needle)
{
    if (!needle || needle[0] == '\0') return true;
    if (!hay)                         return false;
    int hlen = (int)strlen(hay);
    int nlen = (int)strlen(needle);
    if (nlen > hlen) return false;
    for (int i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (int j = 0; j < nlen; j++) {
            char h = hay[i + j];
            char n = needle[j];
            if (h >= 'A' && h <= 'Z') h = (char)(h + 32);
            if (n >= 'A' && n <= 'Z') n = (char)(n + 32);
            if (h != n) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

static bool player_is_registered_for_day(int spieler_id)
{
    for (int i = 0; i < MAX_PORTAL_SPIELER; i++) {
        if (g_store.kreditPlayerIds[i] == spieler_id) return true;
    }
    return false;
}

// ── Callbacks ─────────────────────────────────────────────────

static void textarea_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target_obj(e);
    lv_keyboard_set_textarea(s_kb, ta);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}

static void player_tap_cb(lv_event_t *e)
{
    int pidx = (int)(intptr_t)lv_event_get_user_data(e);
    open_edit(pidx);
}

static void search_changed_cb(lv_event_t *e)
{
    const char *text = lv_textarea_get_text(s_ta_search);
    strncpy(s_search_buf, text ? text : "", sizeof(s_search_buf) - 1);
    s_search_buf[sizeof(s_search_buf) - 1] = '\0';
    build_list();
}

static void save_cb(lv_event_t *e)
{
    if (s_selected_pidx < 0 || s_selected_pidx >= g_store.portalSpielerCount) return;
    PortalSpieler *ps = &g_store.portalSpieler[s_selected_pidx];
    const char *name  = lv_textarea_get_text(s_ta_name);
    const char *email = lv_textarea_get_text(s_ta_email);
    if (!name || strlen(name) == 0) {
        lv_label_set_text(s_lbl_edit_status, "NUMM DARF NET EIDEL SEIN");
        lv_obj_set_style_text_color(s_lbl_edit_status, lv_color_hex(CLR_DANGER), 0);
        return;
    }
    store_queue_spieler_update(ps->id, name, email, s_edit_aktiv);
    lv_label_set_text(s_lbl_edit_status, LV_SYMBOL_OK "  GEQUEUED — BEIM NAACHSTE SYNC APPLIZÉIERT");
    lv_obj_set_style_text_color(s_lbl_edit_status, lv_color_hex(CLR_SUCCESS), 0);
    update_pending_badge();
    build_list();
}

static void pwd_reset_cb(lv_event_t *e)
{
    if (s_selected_pidx < 0 || s_selected_pidx >= g_store.portalSpielerCount) return;
    PortalSpieler *ps = &g_store.portalSpieler[s_selected_pidx];
    store_queue_passwort_reset(ps->id);
    lv_label_set_text(s_lbl_edit_status, LV_SYMBOL_LOOP "  PASSWUERT RESET GEQUEUED");
    lv_obj_set_style_text_color(s_lbl_edit_status, lv_color_hex(CLR_WARN), 0);
    update_pending_badge();
}

static void aktiv_toggle_cb(lv_event_t *e)
{
    s_edit_aktiv = !s_edit_aktiv;
    update_aktiv_btn();
}

static void add_day_cb(lv_event_t *e)
{
    (void)e;
    if (s_selected_pidx < 0 ||
        s_selected_pidx >= g_store.portalSpielerCount) return;

    PortalSpieler *ps = &g_store.portalSpieler[s_selected_pidx];
    if (player_is_registered_for_day(ps->id)) {
        lv_label_set_text(s_lbl_edit_status, LV_SYMBOL_OK "  SCHON SPILLER VUM DAAG");
        lv_obj_set_style_text_color(s_lbl_edit_status, lv_color_hex(CLR_MUTED), 0);
        update_day_btn();
        return;
    }

    store_register_spieler_fuer_tag(ps->id);
    if (player_is_registered_for_day(ps->id)) {
        lv_label_set_text(s_lbl_edit_status, LV_SYMBOL_OK "  SPILLER VUM DAAG DOBÄIGESAT");
        lv_obj_set_style_text_color(s_lbl_edit_status, lv_color_hex(CLR_SUCCESS), 0);
    } else {
        lv_label_set_text(s_lbl_edit_status, LV_SYMBOL_WARNING "  SPILLER-VUM-DAAG-LËSCHT ASS VOLL");
        lv_obj_set_style_text_color(s_lbl_edit_status, lv_color_hex(CLR_DANGER), 0);
    }
    update_day_btn();
    build_list();
}

static void reload_cb(lv_event_t *e)
{
    store_sync();
    if (s_lbl_status) {
        lv_label_set_text(s_lbl_status, LV_SYMBOL_REFRESH "  SYNC LUEFT...");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_MUTED), 0);
    }
    update_pending_badge();
}

static void add_local_cb(lv_event_t *e)
{
    const char *name = lv_textarea_get_text(s_ta_new_name);
    if (!name || strlen(name) == 0) return;
    int new_id;
    store_add_lokal_spieler(name, &new_id);
    lv_textarea_set_text(s_ta_new_name, "");
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kb, NULL);
    build_list();
}

static void cancel_edit_cb(lv_event_t *e)
{
    close_edit();
}

// ── Helper: update aktiv toggle appearance ────────────────────
static void update_aktiv_btn(void)
{
    if (!s_aktiv_btn || !s_lbl_aktiv) return;
    lv_obj_set_style_bg_color(s_aktiv_btn,
        s_edit_aktiv ? lv_color_hex(CLR_PRIMARY) : lv_color_hex(CLR_SIDEBAR), 0);
    lv_obj_set_style_bg_opa(s_aktiv_btn, LV_OPA_COVER, 0);
    lv_label_set_text(s_lbl_aktiv,
        s_edit_aktiv ? LV_SYMBOL_OK "  PORTAL AKTIV" : "PORTAL INAKTIV");
}

static void update_day_btn(void)
{
    if (!s_day_btn || !s_lbl_day) return;

    bool registered = false;
    if (s_selected_pidx >= 0 &&
        s_selected_pidx < g_store.portalSpielerCount) {
        registered = player_is_registered_for_day(
            g_store.portalSpieler[s_selected_pidx].id);
    }

    lv_obj_set_style_bg_color(s_day_btn,
        registered ? lv_color_hex(CLR_SUCCESS) : lv_color_hex(CLR_PRIMARY), 0);
    lv_obj_set_style_bg_opa(s_day_btn, LV_OPA_COVER, 0);
    lv_label_set_text(s_lbl_day,
        registered ? LV_SYMBOL_OK "  SPILLER VUM DAAG" :
                     "+  SPILLER VUM DAAG");
}

// ── Helper: update pending badge in header ────────────────────
static void update_pending_badge(void)
{
    if (!s_lbl_pending) return;
    int cnt = store_pending_update_count();
    if (cnt > 0) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%d PEND.", cnt);
        lv_label_set_text(s_lbl_pending, buf);
        lv_obj_clear_flag(s_lbl_pending, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_lbl_pending, LV_OBJ_FLAG_HIDDEN);
    }
}

// ── Build / refresh player list ───────────────────────────────
// Kept as lean as possible — called on search change, save, and initial load only.
// open_edit() / close_edit() do NOT call this to avoid a full widget rebuild
// while the screen is live (causes the visible "building up" artifact).
static void build_list(void)
{
    lv_obj_clean(s_list);

    bool has_search = (s_search_buf[0] != '\0');
    int  shown = 0;

    for (int i = 0; i < g_store.portalSpielerCount; i++) {
        PortalSpieler *ps = &g_store.portalSpieler[i];

        if (has_search &&
            !str_icontains(ps->name,       s_search_buf) &&
            !str_icontains(ps->mitgliedNr, s_search_buf)) continue;

        int kredit = store_kredite_verfuegbar(ps->id);

        // ── Row: flat flex-row, NO nested containers ──────────
        // Eliminates 2 levels of nested flex → much less layout work per scroll frame.
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_set_size(row, LV_PCT(100), 64);
        lv_obj_add_style(row, &g_style_card, 0);
        lv_obj_set_style_pad_hor(row, 12, 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 10, 0);
        // SHORT_CLICKED fires as soon as the finger lifts without a scroll gesture;
        // avoids the ~100 ms scroll-detection hold that ate the first tap.
        lv_obj_add_event_cb(row, player_tap_cb, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);

        // ── Info column (one nested container — unavoidable for two text lines) ──
        lv_obj_t *info = lv_obj_create(row);
        lv_obj_set_flex_grow(info, 1);
        lv_obj_set_height(info, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(info, LV_OPA_0, 0);
        lv_obj_set_style_border_width(info, 0, 0);
        lv_obj_set_style_pad_all(info, 0, 0);
        lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(info, 2, 0);
        lv_obj_clear_flag(info, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(info, LV_OBJ_FLAG_CLICKABLE);

        // Name label — "NAME [L]" suffix replaces the old LOKAL badge widget
        lv_obj_t *name_lbl = lv_label_create(info);
        if (ps->lokal) {
            char nb[MAX_NAME_LEN + 6];
            snprintf(nb, sizeof(nb), "%s [L]", ps->name);
            lv_label_set_text(name_lbl, nb);
            lv_obj_set_style_text_color(name_lbl, lv_color_hex(CLR_WARN), 0);
        } else {
            lv_label_set_text(name_lbl, ps->name);
            lv_obj_set_style_text_color(name_lbl, lv_color_hex(CLR_TEXT), 0);
        }
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name_lbl, LV_PCT(100));

        if (player_is_registered_for_day(ps->id)) {
            lv_obj_t *day_lbl = lv_label_create(info);
            lv_label_set_text(day_lbl, LV_SYMBOL_OK "  SPILLER VUM DAAG");
            lv_obj_set_style_text_font(day_lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(day_lbl, lv_color_hex(CLR_SUCCESS), 0);
        }

        // Member number (optional second line)
        if (ps->mitgliedNr[0]) {
            lv_obj_t *nr_lbl = lv_label_create(info);
            char nr_buf[36];
            snprintf(nr_buf, sizeof(nr_buf), "Nr. %s", ps->mitgliedNr);
            lv_label_set_text(nr_lbl, nr_buf);
            lv_obj_set_style_text_font(nr_lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(nr_lbl, lv_color_hex(CLR_MUTED), 0);
        }

        // ── Right-side labels — directly in row (no wrapper container) ──
        lv_obj_t *kred_lbl = lv_label_create(row);
        char kred_buf[10];
        snprintf(kred_buf, sizeof(kred_buf), "%dKr", kredit);
        lv_label_set_text(kred_lbl, kred_buf);
        lv_obj_set_style_text_font(kred_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(kred_lbl,
            kredit > 0 ? lv_color_hex(CLR_SUCCESS) : lv_color_hex(CLR_MUTED), 0);

        lv_obj_t *aktiv_lbl = lv_label_create(row);
        lv_label_set_text(aktiv_lbl, ps->portalAktiv ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_font(aktiv_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(aktiv_lbl,
            ps->portalAktiv ? lv_color_hex(CLR_PRIMARY) : lv_color_hex(CLR_MUTED), 0);

        lv_obj_t *chev = lv_label_create(row);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_font(chev, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(chev, lv_color_hex(CLR_MUTED), 0);

        shown++;
    }

    if (shown == 0) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_label_set_text(empty, has_search
            ? "KENG RESULTATER"
            : "KENG SPILLER.\nPORTAL SYNC ODER LOKAL DOBAISETZEN.");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(CLR_MUTED), 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(empty, LV_PCT(100));
    }
}

// ── Edit form helpers ─────────────────────────────────────────
static void open_edit(int pidx)
{
    if (pidx < 0 || pidx >= g_store.portalSpielerCount) return;
    s_selected_pidx = pidx;
    PortalSpieler *ps = &g_store.portalSpieler[pidx];

    lv_textarea_set_text(s_ta_name,  ps->name);
    lv_textarea_set_text(s_ta_email, ps->email);   // populated after sync
    s_edit_aktiv = ps->portalAktiv;
    update_aktiv_btn();
    update_day_btn();
    lv_label_set_text(s_lbl_edit_status, "");

    lv_obj_add_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_edit_form,  LV_OBJ_FLAG_HIDDEN);
    // do NOT call build_list() here — rebuilding all rows on every tap is slow
}

static void close_edit(void)
{
    s_selected_pidx = -1;
    update_day_btn();
    lv_obj_clear_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_edit_form, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kb, NULL);
    // do NOT call build_list() here — it's a full widget rebuild visible to the user
}

// ── screen_spiller_create ─────────────────────────────────────
lv_obj_t *screen_spiller_create(void)
{
    // Reset state
    s_selected_pidx = -1;
    s_edit_aktiv    = false;
    s_search_buf[0] = '\0';

    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H);
    screen_base_init(s_scr);

    // ── Header ────────────────────────────────────────────────
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
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(hdr, 20, 0);
    lv_obj_set_style_pad_column(hdr, 12, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_LIST "  SPILLERVERWALTUNG");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_PRIMARY), 0);
    lv_obj_set_flex_grow(title, 1);

    // Pending updates badge
    s_lbl_pending = lv_label_create(hdr);
    lv_label_set_text(s_lbl_pending, "");
    lv_obj_set_style_text_font(s_lbl_pending, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_pending, lv_color_hex(CLR_WARN), 0);
    lv_obj_set_style_bg_color(s_lbl_pending, lv_color_hex(0x78350F), 0);
    lv_obj_set_style_bg_opa(s_lbl_pending, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_lbl_pending, 6, 0);
    lv_obj_set_style_pad_hor(s_lbl_pending, 8, 0);
    lv_obj_set_style_pad_ver(s_lbl_pending, 4, 0);
    lv_obj_add_flag(s_lbl_pending, LV_OBJ_FLAG_HIDDEN);

    // Sync status text
    s_lbl_status = lv_label_create(hdr);
    lv_label_set_text(s_lbl_status, "");
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_MUTED), 0);

    // Sync button
    lv_obj_t *sync_btn = lv_btn_create(hdr);
    lv_obj_add_style(sync_btn, &g_style_btn_secondary, 0);
    lv_obj_add_event_cb(sync_btn, reload_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(sync_btn);
    lv_label_set_text(sl, LV_SYMBOL_REFRESH "  Sync");
    lv_obj_set_style_text_color(sl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(sl);

    // Back button
    lv_obj_t *back_btn = lv_btn_create(hdr);
    lv_obj_add_style(back_btn, &g_style_btn_secondary, 0);
    lv_obj_add_event_cb(back_btn, [](lv_event_t *e) {
        ui_manager_show(SCREEN_DASHBOARD);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back_btn);
    lv_label_set_text(bl, LV_SYMBOL_HOME "  ZURUCK");
    lv_obj_set_style_text_color(bl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(bl);

    // ── Content area (flex-row: left panel + right panel) ─────
    lv_obj_t *content = lv_obj_create(s_scr);
    lv_obj_set_size(content, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H - 70);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, 70);
    lv_obj_set_style_bg_opa(content, LV_OPA_0, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 16, 0);
    lv_obj_set_style_pad_column(content, 12, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // ── LEFT PANEL: add-local + placeholder/edit form ─────────
    lv_obj_t *left = lv_obj_create(content);
    lv_obj_set_size(left, 430, LV_PCT(100));
    lv_obj_set_style_bg_opa(left, LV_OPA_0, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_style_pad_row(left, 10, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    // Scrollable so LVGL can scroll to show focused textarea above keyboard
    lv_obj_set_scroll_dir(left, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(left, LV_SCROLLBAR_MODE_OFF);

    // Add-local row card
    lv_obj_t *add_card = lv_obj_create(left);
    lv_obj_set_size(add_card, LV_PCT(100), 60);
    lv_obj_add_style(add_card, &g_style_card, 0);
    lv_obj_set_style_pad_all(add_card, 8, 0);
    lv_obj_set_flex_flow(add_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(add_card, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(add_card, 8, 0);
    lv_obj_clear_flag(add_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *add_hdr_lbl = lv_label_create(add_card);
    lv_label_set_text(add_hdr_lbl, "NEI:");
    lv_obj_set_style_text_font(add_hdr_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(add_hdr_lbl, lv_color_hex(CLR_MUTED), 0);

    s_ta_new_name = lv_textarea_create(add_card);
    lv_obj_set_flex_grow(s_ta_new_name, 1);
    lv_obj_set_height(s_ta_new_name, 42);
    lv_textarea_set_placeholder_text(s_ta_new_name, "Lokale Spiller...");
    lv_textarea_set_one_line(s_ta_new_name, true);
    lv_obj_set_style_text_font(s_ta_new_name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(s_ta_new_name, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_text_color(s_ta_new_name, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *add_btn = lv_btn_create(add_card);
    lv_obj_add_style(add_btn, &g_style_btn_primary, 0);
    lv_obj_set_size(add_btn, 100, 42);
    lv_obj_add_event_cb(add_btn, add_local_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *abl = lv_label_create(add_btn);
    lv_label_set_text(abl, "+ Lokal");
    lv_obj_set_style_text_font(abl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(abl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(abl);

    // Placeholder (visible when no player selected)
    s_placeholder = lv_obj_create(left);
    lv_obj_set_size(s_placeholder, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_placeholder, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_placeholder, 0, 0);
    lv_obj_set_style_pad_ver(s_placeholder, 24, 0);
    lv_obj_clear_flag(s_placeholder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_placeholder, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *ph_lbl = lv_label_create(s_placeholder);
    lv_label_set_text(ph_lbl, LV_SYMBOL_RIGHT "  SPILLER TIPPPEN\nFIR ZE ÄNNEREN");
    lv_obj_set_style_text_font(ph_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ph_lbl, lv_color_hex(CLR_MUTED), 0);
    lv_obj_set_style_text_align(ph_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(ph_lbl, LV_PCT(100));

    // Edit form (hidden initially)
    s_edit_form = lv_obj_create(left);
    lv_obj_set_size(s_edit_form, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_edit_form, LV_OPA_0, 0);
    lv_obj_set_style_border_color(s_edit_form, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_width(s_edit_form, 1, 0);
    lv_obj_set_style_border_side(s_edit_form, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_top(s_edit_form, 12, 0);
    lv_obj_set_style_pad_bottom(s_edit_form, 0, 0);
    lv_obj_set_style_pad_hor(s_edit_form, 0, 0);
    lv_obj_set_flex_flow(s_edit_form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_edit_form, 10, 0);
    lv_obj_add_flag(s_edit_form, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_edit_form, LV_OBJ_FLAG_SCROLLABLE);

    // Name field
    lv_obj_t *name_hdr = lv_label_create(s_edit_form);
    lv_label_set_text(name_hdr, "NUMM");
    lv_obj_set_style_text_font(name_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(name_hdr, lv_color_hex(CLR_MUTED), 0);

    s_ta_name = lv_textarea_create(s_edit_form);
    lv_obj_set_size(s_ta_name, LV_PCT(100), 48);
    lv_textarea_set_one_line(s_ta_name, true);
    lv_obj_set_style_text_font(s_ta_name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(s_ta_name, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_text_color(s_ta_name, lv_color_hex(CLR_TEXT), 0);

    // Email field
    lv_obj_t *email_hdr = lv_label_create(s_edit_form);
    lv_label_set_text(email_hdr, "EMAIL");
    lv_obj_set_style_text_font(email_hdr, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(email_hdr, lv_color_hex(CLR_MUTED), 0);

    s_ta_email = lv_textarea_create(s_edit_form);
    lv_obj_set_size(s_ta_email, LV_PCT(100), 48);
    lv_textarea_set_one_line(s_ta_email, true);
    lv_textarea_set_placeholder_text(s_ta_email, "spiller@beispill.lu");
    lv_obj_set_style_text_font(s_ta_email, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(s_ta_email, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_text_color(s_ta_email, lv_color_hex(CLR_TEXT), 0);

    // Portal Aktiv toggle button
    s_aktiv_btn = lv_btn_create(s_edit_form);
    lv_obj_set_size(s_aktiv_btn, LV_PCT(100), 44);
    lv_obj_set_style_radius(s_aktiv_btn, 8, 0);
    lv_obj_set_style_border_width(s_aktiv_btn, 0, 0);
    lv_obj_add_event_cb(s_aktiv_btn, aktiv_toggle_cb, LV_EVENT_CLICKED, NULL);
    s_lbl_aktiv = lv_label_create(s_aktiv_btn);
    lv_obj_set_style_text_font(s_lbl_aktiv, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_aktiv, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(s_lbl_aktiv);
    update_aktiv_btn();

    // Add to today's player list (credits are granted separately on Kreditter)
    s_day_btn = lv_btn_create(s_edit_form);
    lv_obj_set_size(s_day_btn, LV_PCT(100), 44);
    lv_obj_set_style_radius(s_day_btn, 8, 0);
    lv_obj_set_style_border_width(s_day_btn, 0, 0);
    lv_obj_add_event_cb(s_day_btn, add_day_cb, LV_EVENT_CLICKED, NULL);
    s_lbl_day = lv_label_create(s_day_btn);
    lv_obj_set_style_text_font(s_lbl_day, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_day, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(s_lbl_day);
    update_day_btn();

    // Action buttons row (Save + Pwd Reset)
    lv_obj_t *act_row = lv_obj_create(s_edit_form);
    lv_obj_set_size(act_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(act_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(act_row, 0, 0);
    lv_obj_set_style_pad_all(act_row, 0, 0);
    lv_obj_set_flex_flow(act_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(act_row, 8, 0);
    lv_obj_clear_flag(act_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *save_btn = lv_btn_create(act_row);
    lv_obj_add_style(save_btn, &g_style_btn_primary, 0);
    lv_obj_set_flex_grow(save_btn, 1);
    lv_obj_set_height(save_btn, 48);
    lv_obj_add_event_cb(save_btn, save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, LV_SYMBOL_SAVE "  SPÄICHEREN");
    lv_obj_set_style_text_font(save_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(save_lbl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(save_lbl);

    lv_obj_t *pwd_btn = lv_btn_create(act_row);
    lv_obj_add_style(pwd_btn, &g_style_btn_secondary, 0);
    lv_obj_set_flex_grow(pwd_btn, 1);
    lv_obj_set_height(pwd_btn, 48);
    lv_obj_add_event_cb(pwd_btn, pwd_reset_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pwd_lbl = lv_label_create(pwd_btn);
    lv_label_set_text(pwd_lbl, LV_SYMBOL_LOOP "  PWD RESET");
    lv_obj_set_style_text_font(pwd_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(pwd_lbl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(pwd_lbl);

    // Cancel / close edit button
    lv_obj_t *cancel_btn = lv_btn_create(s_edit_form);
    lv_obj_add_style(cancel_btn, &g_style_btn_secondary, 0);
    lv_obj_set_size(cancel_btn, LV_PCT(100), 42);
    lv_obj_add_event_cb(cancel_btn, cancel_edit_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "OFBRIECHEN");
    lv_obj_set_style_text_font(cancel_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cancel_lbl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(cancel_lbl);

    // Edit status label (queued / error feedback)
    s_lbl_edit_status = lv_label_create(s_edit_form);
    lv_label_set_text(s_lbl_edit_status, "");
    lv_obj_set_style_text_font(s_lbl_edit_status, &lv_font_montserrat_12, 0);
    lv_obj_set_width(s_lbl_edit_status, LV_PCT(100));
    lv_obj_set_style_text_align(s_lbl_edit_status, LV_TEXT_ALIGN_CENTER, 0);

    // ── RIGHT PANEL: search field + player list ───────────────
    lv_obj_t *right_panel = lv_obj_create(content);
    lv_obj_set_flex_grow(right_panel, 1);
    lv_obj_set_height(right_panel, LV_PCT(100));
    lv_obj_set_style_bg_opa(right_panel, LV_OPA_0, 0);
    lv_obj_set_style_border_width(right_panel, 0, 0);
    lv_obj_set_style_pad_all(right_panel, 0, 0);
    lv_obj_set_style_pad_row(right_panel, 8, 0);
    lv_obj_set_flex_flow(right_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_panel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(right_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Search textarea
    s_ta_search = lv_textarea_create(right_panel);
    lv_obj_set_size(s_ta_search, LV_PCT(100), 50);
    lv_textarea_set_placeholder_text(s_ta_search,
        LV_SYMBOL_LIST "  Spiller sichen (Numm oder Member-Nr.)...");
    lv_textarea_set_one_line(s_ta_search, true);
    lv_obj_set_style_text_font(s_ta_search, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(s_ta_search, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_text_color(s_ta_search, lv_color_hex(CLR_TEXT), 0);
    lv_obj_set_style_border_color(s_ta_search, lv_color_hex(CLR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ta_search, 1, LV_PART_MAIN);
    lv_obj_add_event_cb(s_ta_search, search_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Scrollable player list — created BEFORE keyboard (keyboard must be last)
    s_list = lv_obj_create(right_panel);
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_style_bg_color(s_list, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    // Disable scroll-throw (momentum) animation — makes scrolling feel direct and crisp.
    lv_obj_set_style_anim_duration(s_list, 0, 0);

    // ── On-screen keyboard — created LAST so it renders on top ─
    s_kb = lv_keyboard_create(s_scr);
    lv_obj_set_size(s_kb, DISPLAY_LOGICAL_W, 300);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

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

    // Wire each text-input to show the keyboard on focus
    lv_obj_add_event_cb(s_ta_new_name, textarea_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_search,   textarea_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_name,     textarea_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_email,    textarea_focus_cb, LV_EVENT_FOCUSED, NULL);

    build_list();
    update_pending_badge();

    return s_scr;
}

void screen_spiller_refresh(void)
{
    if (!s_list) return;
    build_list();
    update_pending_badge();
}

void screen_spiller_tick(void)
{
    if (!s_lbl_status) return;
    // Mirror g_store.syncStatus into the header label; only repaint on change
    static SyncStatus s_last = (SyncStatus)-1;
    SyncStatus cur = g_store.syncStatus;
    if (cur == s_last) return;
    s_last = cur;

    switch (cur) {
        case SYNC_RUNNING:
            lv_label_set_text(s_lbl_status, LV_SYMBOL_REFRESH "  SYNC LUEFT...");
            lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_MUTED), 0);
            break;
        case SYNC_SUCCESS:
            lv_label_set_text(s_lbl_status, LV_SYMBOL_OK "  SYNC OK");
            lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_SUCCESS), 0);
            build_list();   // refresh player list with updated portal data
            break;
        case SYNC_ERROR:
            lv_label_set_text(s_lbl_status, LV_SYMBOL_WARNING "  SYNC FEELER");
            lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_DANGER), 0);
            break;
        default:
            lv_label_set_text(s_lbl_status, "");
            break;
    }
}
