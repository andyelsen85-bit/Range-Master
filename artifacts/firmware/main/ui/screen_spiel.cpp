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
#include "coprocessor.h"

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
static lv_obj_t *s_lbl_wifi_status;
static lv_obj_t *s_btn_fire;
static lv_obj_t *s_lbl_fire_button;
static lv_obj_t *s_score_table;
static lv_obj_t *s_btn_score[3];
static lv_obj_t *s_quit_modal;
static lv_obj_t *s_quit_message;
static uint32_t s_grid_signature = UINT32_MAX;
static uint32_t s_table_signature = UINT32_MAX;

static void set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    if (!label || !text) return;
    const char *current = lv_label_get_text(label);
    if (!current || strcmp(current, text) != 0) lv_label_set_text(label, text);
}

// ── Score button callbacks ────────────────────────────────────
static void score_cb(lv_event_t *e)
{
    int pts = (int)(intptr_t)lv_event_get_user_data(e);
    store_eintragen(pts);
    screen_spiel_refresh();
}

static void fire_cb(lv_event_t *e)
{
    if (g_store.taubeIndex >= g_store.sequenzLen) return;
    SequenzEintrag *se = &g_store.sequenz[g_store.taubeIndex];
    if (se->isPair && se->isDoublette) return; // second result never sends another FIRE
    if (g_store.currentFireSent || lora_request_busy()) return;

    bool queued = false;
    if (se->isPair && se->maschine != MASCHINE_H) {
        queued = lora_fire_doublette_game(se->maschine, se->partner, se->delayMs);
    } else {
        // H remains one physical relay and one radio command. H2 is generated
        // locally by the addressed relay after its fixed system delay.
        queued = lora_fire_machine_game(se->maschine);
    }
    if (queued) g_store.currentFireSent = true;
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

static void close_quit_modal(void)
{
    if (!s_quit_modal) return;
    lv_obj_del(s_quit_modal);
    s_quit_modal = NULL;
    s_quit_message = NULL;
}

static void quit_cancel_cb(lv_event_t *e)
{
    close_quit_modal();
}

static void quit_confirm_cb(lv_event_t *e)
{
    if (store_cancel_spiel()) {
        close_quit_modal();
        return;
    }
    if (s_quit_message) {
        lv_label_set_text(s_quit_message,
                           "KREDITE KONNTEN NICHT WIEDERHERGESTELLT WERDEN.\n"
                           "SYNC AUSFÜHREN ODER WARTESCHLANGE LEEREN.");
        lv_obj_set_style_text_color(s_quit_message, lv_color_hex(CLR_DANGER), 0);
    }
}

static void quit_open_cb(lv_event_t *e)
{
    if (s_quit_modal) return;

    s_quit_modal = lv_obj_create(s_scr);
    lv_obj_set_size(s_quit_modal, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H);
    lv_obj_align(s_quit_modal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_quit_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_quit_modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_quit_modal, 0, 0);
    lv_obj_set_style_pad_all(s_quit_modal, 0, 0);
    lv_obj_clear_flag(s_quit_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dialog = lv_obj_create(s_quit_modal);
    lv_obj_set_size(dialog, 520, 250);
    lv_obj_align(dialog, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_style(dialog, &g_style_card, 0);
    lv_obj_set_style_pad_all(dialog, 24, 0);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dialog, LV_FLEX_ALIGN_CENTER,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(dialog, 14, 0);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(dialog);
    lv_label_set_text(title, "SPIEL BEENDEN?");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_DANGER), 0);

    s_quit_message = lv_label_create(dialog);
    lv_label_set_text(s_quit_message,
                       "DAS SPIEL WIRD NICHT GESPEICHERT.\n"
                       "ALLE SPIELERKREDITE WERDEN WIEDERHERGESTELLT.");
    lv_obj_set_style_text_align(s_quit_message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_quit_message, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *actions = lv_obj_create(dialog);
    lv_obj_set_size(actions, LV_PCT(100), 54);
    lv_obj_set_style_bg_opa(actions, LV_OPA_0, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(actions, 12, 0);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *confirm = lv_btn_create(actions);
    lv_obj_add_style(confirm, &g_style_btn_danger, 0);
    lv_obj_set_size(confirm, 210, 54);
    lv_obj_add_event_cb(confirm, quit_confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *confirm_label = lv_label_create(confirm);
    lv_label_set_text(confirm_label, "JA, BEENDEN");
    lv_obj_center(confirm_label);

    lv_obj_t *cancel = lv_btn_create(actions);
    lv_obj_add_style(cancel, &g_style_btn_secondary, 0);
    lv_obj_set_size(cancel, 210, 54);
    lv_obj_add_event_cb(cancel, quit_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "SPIEL FORTSETZEN");
    lv_obj_center(cancel_label);
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

    lv_obj_t *quit_btn = lv_btn_create(topbar);
    lv_obj_add_style(quit_btn, &g_style_btn_danger, 0);
    lv_obj_set_size(quit_btn, 142, 42);
    lv_obj_add_event_cb(quit_btn, quit_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *quit_label = lv_label_create(quit_btn);
    lv_label_set_text(quit_label, "SPIEL BEENDEN");
    lv_obj_center(quit_label);

    // ── Left panel: game controls (280px) ──────────────────
    lv_obj_t *left = lv_obj_create(s_scr);
    lv_obj_set_size(left, 280, DISPLAY_LOGICAL_H - 70);
    lv_obj_align(left, LV_ALIGN_TOP_LEFT, 0, 70);
    lv_obj_set_style_bg_color(left, lv_color_hex(CLR_SIDEBAR), 0);
    lv_obj_set_style_bg_opa(left, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(left, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(left, 2, 0);
    lv_obj_set_style_border_color(left, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    // Keep the controls in a predictable top-to-bottom reading order:
    // WDH/SKIP, score buttons, WERFEN target, then FIRE.
    lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(left, 20, 0);
    lv_obj_set_style_pad_row(left, 10, 0);

    // ── Top controls: Wiederhole + SKIP ────────────────────
    lv_obj_t *ctrl_row = lv_obj_create(left);
    lv_obj_set_size(ctrl_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ctrl_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(ctrl_row, 0, 0);
    lv_obj_set_style_pad_all(ctrl_row, 0, 0);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(ctrl_row, 8, 0);
    lv_obj_clear_flag(ctrl_row, LV_OBJ_FLAG_SCROLLABLE);

    s_btn_wiederhole = lv_btn_create(ctrl_row);
    lv_obj_add_style(s_btn_wiederhole, &g_style_btn_secondary, 0);
    // Keep both controls within the 240px inner panel while meeting a more
    // usable touch size than the previous 112x40 targets.
    lv_obj_set_size(s_btn_wiederhole, 116, 48);
    lv_obj_set_style_pad_hor(s_btn_wiederhole, 10, 0);
    lv_obj_add_event_cb(s_btn_wiederhole, wiederhole_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wl = lv_label_create(s_btn_wiederhole);
    lv_label_set_text(wl, LV_SYMBOL_REFRESH " WDH");
    lv_obj_set_style_text_font(wl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(wl);

    lv_obj_t *skip_btn = lv_btn_create(ctrl_row);
    lv_obj_add_style(skip_btn, &g_style_btn_secondary, 0);
    lv_obj_set_size(skip_btn, 116, 48);
    lv_obj_set_style_pad_hor(skip_btn, 10, 0);
    lv_obj_add_event_cb(skip_btn, skip_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(skip_btn);
    lv_label_set_text(sl, LV_SYMBOL_NEXT " SKIP");
    lv_obj_set_style_text_font(sl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(sl);

    // ── Score buttons: 0, 1, 2 ─────────────────────────────
    static const char *sc_labels[] = {"0", "1", "2"};
    static int sc_pts[]            = {0, 1, 2};
    static uint32_t sc_colors[]    = {CLR_DANGER, CLR_WARN, CLR_SUCCESS};

    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(left);
        s_btn_score[i] = btn;
        lv_obj_set_size(btn, LV_PCT(100), 72);
        // Stretch the point buttons into the available middle area so the
        // FIRE trigger remains anchored at the bottom of the panel.
        lv_obj_set_flex_grow(btn, 1);
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

    // ── Throw target, directly above the FIRE trigger ──────
    lv_obj_t *mach_hdr = lv_label_create(left);
    lv_label_set_text(mach_hdr, "WERFER");
    lv_obj_set_style_text_font(mach_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mach_hdr, lv_color_hex(CLR_MUTED), 0);

    s_lbl_maschine = lv_label_create(left);
    lv_label_set_text(s_lbl_maschine, "A");
    lv_obj_set_style_text_font(s_lbl_maschine, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_lbl_maschine, lv_color_hex(CLR_PRIMARY), 0);

    // ── FIRE trigger at the bottom of the control stack ────
    s_btn_fire = lv_btn_create(left);
    lv_obj_set_size(s_btn_fire, LV_PCT(100), 64);
    lv_obj_set_style_bg_color(s_btn_fire, lv_color_hex(CLR_PRIMARY), 0);
    lv_obj_set_style_bg_opa(s_btn_fire, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_btn_fire, 10, 0);
    lv_obj_set_style_border_width(s_btn_fire, 0, 0);
    lv_obj_add_event_cb(s_btn_fire, fire_cb, LV_EVENT_CLICKED, NULL);
    s_lbl_fire_button = lv_label_create(s_btn_fire);
    lv_label_set_text(s_lbl_fire_button, LV_SYMBOL_PLAY " MASCHINE A STARTEN");
    lv_obj_set_style_text_font(s_lbl_fire_button, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_lbl_fire_button, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(s_lbl_fire_button);

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
    lv_label_set_text(pg_hdr, "STÄNDE");
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
    // The post and points labels are fixed-width siblings.  Let the name
    // column consume the remaining space instead of pushing those values off
    // the banner when a roster name is long.
    lv_obj_set_width(name_col, 0);
    lv_obj_set_flex_grow(name_col, 1);

    lv_obj_t *shooter_hdr_lbl = lv_label_create(name_col);
    lv_label_set_text(shooter_hdr_lbl, "AKTUELLER SCHÜTZE");
    lv_obj_set_style_text_font(shooter_hdr_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(shooter_hdr_lbl, lv_color_hex(CLR_PRIMARY), 0);

    s_lbl_active_name = lv_label_create(name_col);
    lv_label_set_text(s_lbl_active_name, "---");
    lv_obj_set_style_text_font(s_lbl_active_name, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(s_lbl_active_name, lv_color_hex(CLR_TEXT), 0);
    lv_label_set_long_mode(s_lbl_active_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_lbl_active_name, LV_PCT(100));

    // Post + Points
    s_lbl_active_post = lv_label_create(shooter_bar);
    lv_label_set_text(s_lbl_active_post, "P-");
    lv_obj_set_style_text_font(s_lbl_active_post, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(s_lbl_active_post, lv_color_hex(CLR_PRIMARY), 0);

    s_lbl_active_pts = lv_label_create(shooter_bar);
    lv_label_set_text(s_lbl_active_pts, "0 PKT");
    lv_obj_set_style_text_font(s_lbl_active_pts, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(s_lbl_active_pts, lv_color_hex(CLR_PRIMARY), 0);

    lv_obj_t *network_row = lv_obj_create(right);
    lv_obj_set_size(network_row, LV_PCT(100), 24);
    lv_obj_set_style_bg_opa(network_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(network_row, 0, 0);
    lv_obj_set_style_pad_all(network_row, 0, 0);
    lv_obj_set_flex_flow(network_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(network_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_lbl_wifi_status = lv_label_create(network_row);
    lv_label_set_text(s_lbl_wifi_status, "WIFI: -");
    lv_obj_set_style_text_font(s_lbl_wifi_status, &lv_font_montserrat_12, 0);
    s_lbl_fire_status = lv_label_create(network_row);
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
    lv_table_set_cell_value(s_score_table, 0, 0, "SPIELER");
    lv_table_set_cell_value(s_score_table, 0, 1, "STAND");
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
        GatewayReachability gateway = lora_gateway_state();
        char fire_status[64];
        snprintf(fire_status, sizeof(fire_status), "GATEWAY: %s",
                 lora_gateway_state_label(gateway));
        set_label_text_if_changed(s_lbl_fire_status, fire_status);
        uint32_t color = gateway == GATEWAY_REACHABLE ? CLR_SUCCESS :
                         gateway == GATEWAY_CHECKING ? CLR_WARN :
                         gateway == GATEWAY_NOT_CONFIGURED ? CLR_MUTED : CLR_DANGER;
        lv_obj_set_style_text_color(s_lbl_fire_status, lv_color_hex(color), 0);
    }
    if (s_lbl_wifi_status) {
        CopWifiState wifi = cop_wifi_state();
        char text[48];
        snprintf(text, sizeof(text), "WIFI: %s", cop_wifi_state_label(wifi));
        set_label_text_if_changed(s_lbl_wifi_status, text);
        uint32_t color = wifi == COP_WIFI_CONNECTED ? CLR_SUCCESS :
                         (wifi == COP_WIFI_CONNECTING ||
                          wifi == COP_WIFI_RECONNECTING) ? CLR_WARN :
                         wifi == COP_WIFI_NOT_CONFIGURED ? CLR_MUTED : CLR_DANGER;
        lv_obj_set_style_text_color(s_lbl_wifi_status, lv_color_hex(color), 0);
    }

    // ── Compute current sequenz entry state ───────────────
    bool isHMaschine = false;
    bool isPair = false;
    bool isPairSecond = false;
    if (s->taubeIndex < s->sequenzLen) {
        SequenzEintrag *se = &s->sequenz[s->taubeIndex];
        isHMaschine = (se->maschine == MASCHINE_H && se->isPair);
        isPair = se->isPair;
        isPairSecond = se->isPair && se->isDoublette;
    }
    // Every pair's second result stays at the same post as its first result.
    int effIdx = (isPairSecond && s->taubeIndex > 0) ? s->taubeIndex - 1 : s->taubeIndex;
    int pairOffset = 0;
    for (int i = 0; i < effIdx && i < s->sequenzLen; ++i)
        if (s->sequenz[i].isPair && s->sequenz[i].isDoublette) pairOffset++;
    int logicalIdx = effIdx - pairOffset;

    // Inline post formula: ((startPosten-1 + effIdx) % 5) + 1
    // (mirrors getCurrentPosten in gameStore.ts)
    auto pos_of = [&](int startPosten) -> int {
        return ((startPosten - 1 + logicalIdx) % 5) + 1;
    };

    // ── Top bar ───────────────────────────────────────────
    char buf[64];
    set_label_text_if_changed(s_lbl_modus, modus_label(s->modus));
    snprintf(buf, sizeof(buf), "LAUF %d", s->lauf);
    set_label_text_if_changed(s_lbl_lauf, buf);
    snprintf(buf, sizeof(buf), "TAUBE %d / %d", s->taubeIndex + 1, s->sequenzLen);
    set_label_text_if_changed(s_lbl_taube, buf);

    // ── Machine label: single / A-G pair / H1-H2 ───────────
    if (s->taubeIndex < s->sequenzLen) {
        SequenzEintrag *se = &s->sequenz[s->taubeIndex];
        char ml[16];
        if (isHMaschine) {
            snprintf(ml, sizeof(ml), "H%d", isPairSecond ? 2 : 1);
            lv_obj_set_style_text_color(s_lbl_maschine, lv_color_hex(CLR_WARN), 0);
        } else if (isPair && !isPairSecond) {
            snprintf(ml, sizeof(ml), "%s+%s", maschine_label(se->maschine),
                     maschine_label(se->partner));
            lv_obj_set_style_text_color(s_lbl_maschine, lv_color_hex(CLR_PRIMARY), 0);
        } else {
            snprintf(ml, sizeof(ml), "%s", maschine_label(se->maschine));
            lv_obj_set_style_text_color(s_lbl_maschine, lv_color_hex(CLR_PRIMARY), 0);
        }
        set_label_text_if_changed(s_lbl_maschine, ml);
    }

    if (s_btn_fire && s_lbl_fire_button) {
        bool can_fire = s->taubeIndex < s->sequenzLen && !isPairSecond &&
                        !s->currentFireSent && !lora_request_busy();
        if (can_fire) {
            char fire_label[40];
            SequenzEintrag *se = &s->sequenz[s->taubeIndex];
            if (se->isPair && se->maschine == MASCHINE_H) {
                snprintf(fire_label, sizeof(fire_label), LV_SYMBOL_PLAY " H STARTEN (MASCHINE H2)");
            } else if (se->isPair) {
                snprintf(fire_label, sizeof(fire_label), LV_SYMBOL_PLAY " %s + %s STARTEN",
                         maschine_label(se->maschine), maschine_label(se->partner));
            } else {
                snprintf(fire_label, sizeof(fire_label), LV_SYMBOL_PLAY " MASCHINE %s STARTEN",
                         maschine_label(se->maschine));
            }
            set_label_text_if_changed(s_lbl_fire_button, fire_label);
            lv_obj_clear_state(s_btn_fire, LV_STATE_DISABLED);
        } else {
            if (isPairSecond)
                set_label_text_if_changed(s_lbl_fire_button, "2. ERGEBNIS - NICHT STARTEN");
            else if (s->currentFireSent)
                set_label_text_if_changed(s_lbl_fire_button, "BEREITS GESTARTET");
            else
                set_label_text_if_changed(s_lbl_fire_button,
                    s->taubeIndex < s->sequenzLen ? "GATEWAY BESCHÄFTIGT" : "KEINE MASCHINE");
            lv_obj_add_state(s_btn_fire, LV_STATE_DISABLED);
        }
    }
    // H1/H2 have one score per clay (hit or miss), never a second-shot point.
    for (int i = 0; i < 3; ++i) {
        if (!s_btn_score[i]) continue;
        if (isHMaschine && i == 1) lv_obj_add_state(s_btn_score[i], LV_STATE_DISABLED);
        else lv_obj_clear_state(s_btn_score[i], LV_STATE_DISABLED);
    }

    // ── Active shooter's current post ─────────────────────
    int active_post = 1;
    if (s->spielerCount > 0 && s->spielerIndex < s->spielerCount)
        active_post = pos_of(s->spieler[s->spielerIndex].startPosten);

    // ── 5-post grid ───────────────────────────────────────
    // Column width: right panel usable width ≈ 976px. 5 cols × 186 + 4 × 6 gap ≈ 954.
    static const int POST_W = 186;
    static const int POST_H = 128;

    uint32_t grid_signature = 2166136261u;
    auto hash_grid = [&](uint32_t value) {
        grid_signature ^= value;
        grid_signature *= 16777619u;
    };
    hash_grid((uint32_t)active_post);
    hash_grid((uint32_t)s->spielerIndex);
    hash_grid((uint32_t)s->spielerCount);
    hash_grid((uint32_t)logicalIdx);
    for (int i = 0; i < s->spielerCount; ++i) {
        hash_grid((uint32_t)s->spieler[i].id);
        hash_grid((uint32_t)s->spieler[i].startPosten);
        hash_grid((uint32_t)s->spieler[i].punkte);
    }
    if (grid_signature != s_grid_signature) {
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
            snprintf(phdr, sizeof(phdr), "STAND %d", post);
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
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
            lv_obj_set_width(name_lbl, 0);
            lv_obj_set_flex_grow(name_lbl, 1);

            char pts_buf[10];
            snprintf(pts_buf, sizeof(pts_buf), "%dp", s->spieler[i].punkte);
            lv_obj_t *pts_lbl = lv_label_create(prow);
            lv_label_set_text(pts_lbl, pts_buf);
            lv_obj_set_style_text_font(pts_lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(pts_lbl,
                isActive ? lv_color_hex(0x000000) : lv_color_hex(CLR_PRIMARY), 0);
            lv_obj_set_width(pts_lbl, 32);
        }

        if (!anyPlayer) {
            lv_obj_t *dash = lv_label_create(col);
            lv_label_set_text(dash, "-");
            lv_obj_set_style_text_font(dash, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(dash, lv_color_hex(CLR_BORDER), 0);
        }
        }
        s_grid_signature = grid_signature;
    }

    // ── Active shooter banner ─────────────────────────────
    if (s->spielerCount > 0 && s->spielerIndex < s->spielerCount) {
        Spieler *sp = &s->spieler[s->spielerIndex];
        set_label_text_if_changed(s_lbl_active_name, sp->name);
        char pb[8];
        snprintf(pb, sizeof(pb), "P%d", active_post);
        set_label_text_if_changed(s_lbl_active_post, pb);
        char ptb[16];
        snprintf(ptb, sizeof(ptb), "%d PKT", sp->punkte);
        set_label_text_if_changed(s_lbl_active_pts, ptb);
    }

    // ── Score table: show current (rotated) position ──────
    uint32_t table_signature = grid_signature ^ 0x9e3779b9u;
    if (table_signature != s_table_signature) {
        lv_table_set_row_cnt(s_score_table, s->spielerCount + 1);
        for (int i = 0; i < s->spielerCount; i++) {
            int cur_post = pos_of(s->spieler[i].startPosten);
            char rb[16];
            lv_table_set_cell_value(s_score_table, i + 1, 0, s->spieler[i].name);
            lv_table_set_cell_ctrl(s_score_table, i + 1, 0,
                                   LV_TABLE_CELL_CTRL_TEXT_CROP);
            snprintf(rb, sizeof(rb), "P%d", cur_post);
            lv_table_set_cell_value(s_score_table, i + 1, 1, rb);
            snprintf(rb, sizeof(rb), "%d", s->spieler[i].punkte);
            lv_table_set_cell_value(s_score_table, i + 1, 2, rb);
        }
        s_table_signature = table_signature;
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
