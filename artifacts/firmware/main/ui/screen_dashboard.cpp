// ============================================================
// Dashboard screen - home screen with game history + nav sidebar
// Mirrors DashboardScreen.tsx layout:
//   LEFT  960px - "Leschte SPILLER" game history card
//   RIGHT 320px - navigation buttons + offline queue panel
//   TOP    80px - header with club name + WiFi
// ============================================================
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ui_time_fmt.h"
#include "lvgl.h"
#include "ui_fonts.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_dashboard.h"
#include "coprocessor.h"
#include "lora_stub.h"


// Persistent widget handles for screen_dashboard_refresh()
static lv_obj_t *s_scr;
static lv_obj_t *s_lbl_sync_status;
static lv_obj_t *s_lbl_pending;
static lv_obj_t *s_lbl_wifi;
static lv_obj_t *s_lbl_gateway;
static lv_obj_t *s_lbl_clock;
static lv_obj_t *s_history_list;
static lv_obj_t *s_shutdown_modal;
static lv_obj_t *s_shutdown_message;
static bool s_shutdown_sync_pending;
static uint32_t s_history_signature = UINT32_MAX;
static CopWifiState s_rendered_wifi_state = (CopWifiState)-1;
static GatewayReachability s_rendered_gateway_state = (GatewayReachability)-1;

#define SIDEBAR_W   320
#define HEADER_H     80
#define CONTENT_W   (DISPLAY_LOGICAL_W - SIDEBAR_W)
#define CONTENT_H   (DISPLAY_LOGICAL_H - HEADER_H)

static void set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    if (!label || !text) return;
    const char *current = lv_label_get_text(label);
    if (!current || strcmp(current, text) != 0) lv_label_set_text(label, text);
}

static uint32_t current_history_signature(void)
{
    // Hash only stable fields that affect the five rendered rows. This avoids
    // deleting/recreating the list every two seconds while still noticing a
    // completed sync that changes history without changing its row count.
    uint32_t hash = 2166136261u;
    int count = g_store.historyCount;
    hash = (hash ^ (uint32_t)count) * 16777619u;
    for (int i = 0; i < count; ++i) {
        const FinishedGame *fg = &g_store.history[i];
        for (const char *p = fg->finishedAt; *p; ++p)
            hash = (hash ^ (uint8_t)*p) * 16777619u;
        hash = (hash ^ (uint32_t)fg->spieler_count) * 16777619u;
        hash = (hash ^ (uint32_t)fg->base.ergebnisse_count) * 16777619u;
        for (int r = 0; r < fg->base.ergebnisse_count; ++r)
            hash = (hash ^ (uint32_t)fg->base.ergebnisse[r].punkte) * 16777619u;
    }
    return hash;
}

// ── Large primary nav button (full sidebar width) ─────────────
static lv_obj_t *big_btn(lv_obj_t *parent, const char *sym,
                          const char *label, int h, Screen target)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_add_style(btn, &g_style_btn_primary, 0);
    lv_obj_set_size(btn, LV_PCT(100), h);
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        Screen s = (Screen)(intptr_t)lv_event_get_user_data(e);
        ui_manager_show(s);
    }, LV_EVENT_CLICKED, (void *)(intptr_t)target);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn, 6, 0);

    lv_obj_t *ic = lv_label_create(btn);
    lv_label_set_text(ic, sym);
    lv_obj_set_style_text_font(ic, UI_FONT_28, 0);
    lv_obj_set_style_text_color(ic, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, UI_FONT_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_TEXT), 0);
    return btn;
}

// ── Secondary nav button (fixed pixel width) ──────────────────
static lv_obj_t *sec_btn(lv_obj_t *parent, const char *sym,
                          const char *label, int w, int h, Screen target)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_add_style(btn, &g_style_btn_secondary, 0);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        Screen s = (Screen)(intptr_t)lv_event_get_user_data(e);
        ui_manager_show(s);
    }, LV_EVENT_CLICKED, (void *)(intptr_t)target);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn, 4, 0);

    lv_obj_t *ic = lv_label_create(btn);
    lv_label_set_text(ic, sym);
    lv_obj_set_style_text_font(ic, UI_FONT_22, 0);
    lv_obj_set_style_text_color(ic, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, UI_FONT_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_TEXT), 0);
    return btn;
}

// ── Sync button callback ───────────────────────────────────────
static void sync_cb(lv_event_t *e)
{
    store_sync();
}

// ── Graceful deep-sleep shutdown ───────────────────────────────
static void close_shutdown_modal(void)
{
    if (!s_shutdown_modal) return;
    lv_obj_del(s_shutdown_modal);
    s_shutdown_modal = NULL;
    s_shutdown_message = NULL;
    s_shutdown_sync_pending = false;
}

static void set_shutdown_message(const char *text, uint32_t color)
{
    if (!s_shutdown_message) return;
    lv_label_set_text(s_shutdown_message, text);
    lv_obj_set_style_text_color(s_shutdown_message, lv_color_hex(color), 0);
}

static void shutdown_cancel_cb(lv_event_t *e)
{
    if (s_shutdown_sync_pending) return;
    close_shutdown_modal();
}

static void shutdown_confirm_cb(lv_event_t *e)
{
    if (s_shutdown_sync_pending) return;

    if (cop_wifi_state() != COP_WIFI_CONNECTED) {
        set_shutdown_message("WIFI IST NICHT VERBUNDEN.\n"
                             "ZUERST VERBINDEN, DANN ERNEUT VERSUCHEN.", CLR_DANGER);
        return;
    }

    // The global sync result does not identify its source. Refuse while a
    // previous sync is active so the only completion we observe belongs to
    // this shutdown request.
    if (store_sync_is_queued_or_running()) {
        set_shutdown_message("EIN SYNC LÄUFT BEREITS.\n"
                             "WARTEN, DANN ERNEUT VERSUCHEN.", CLR_WARN);
        return;
    }

    if (!store_sync()) {
        SyncUiState sync_state = {};
        store_get_sync_ui_state(&sync_state);
        char message[160];
        snprintf(message, sizeof(message), "SYNC KONNTE NICHT STARTEN.\n%.100s",
                 sync_state.error[0] ? sync_state.error : "ERNEUT VERSUCHEN.");
        set_shutdown_message(message, CLR_DANGER);
        return;
    }

    s_shutdown_sync_pending = true;
    set_shutdown_message("SYNC VOR DEM AUSSCHALTEN...\n"
                         "TERMINAL NICHT AUSSCHALTEN.", CLR_WARN);
}

static void shutdown_open_cb(lv_event_t *e)
{
    if (s_shutdown_modal) return;

    s_shutdown_modal = lv_obj_create(s_scr);
    lv_obj_set_size(s_shutdown_modal, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H);
    lv_obj_align(s_shutdown_modal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_shutdown_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_shutdown_modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_shutdown_modal, 0, 0);
    lv_obj_set_style_pad_all(s_shutdown_modal, 0, 0);
    lv_obj_clear_flag(s_shutdown_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dialog = lv_obj_create(s_shutdown_modal);
    lv_obj_set_size(dialog, 560, 270);
    lv_obj_align(dialog, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_style(dialog, &g_style_card, 0);
    lv_obj_set_style_pad_all(dialog, 24, 0);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(dialog, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(dialog, 16, 0);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(dialog);
    lv_label_set_text(title, "SYNC & AUSSCHALTEN?");
    lv_obj_set_style_text_font(title, UI_FONT_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_DANGER), 0);

    s_shutdown_message = lv_label_create(dialog);
    lv_label_set_text(s_shutdown_message,
                       "DAS TERMINAL SYNCHRONISIERT ZUERST.\n"
                       "TIEFSCHLAF NUR BEI ERFOLGREICHEM SYNC.");
    lv_obj_set_style_text_align(s_shutdown_message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_shutdown_message, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *actions = lv_obj_create(dialog);
    lv_obj_set_size(actions, LV_PCT(100), 54);
    lv_obj_set_style_bg_opa(actions, LV_OPA_0, 0);
    lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_style_pad_column(actions, 12, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *confirm = lv_btn_create(actions);
    lv_obj_add_style(confirm, &g_style_btn_danger, 0);
    lv_obj_set_size(confirm, 250, 54);
    lv_obj_add_event_cb(confirm, shutdown_confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *confirm_label = lv_label_create(confirm);
    lv_label_set_text(confirm_label, LV_SYMBOL_POWER "  SYNC & AUSSCHALTEN");
    lv_obj_set_style_text_font(confirm_label, UI_FONT_14, 0);
    lv_obj_center(confirm_label);

    lv_obj_t *cancel = lv_btn_create(actions);
    lv_obj_add_style(cancel, &g_style_btn_secondary, 0);
    lv_obj_set_size(cancel, 160, 54);
    lv_obj_add_event_cb(cancel, shutdown_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "ABBRECHEN");
    lv_obj_center(cancel_label);
}

// ── screen_dashboard_create ───────────────────────────────────
lv_obj_t *screen_dashboard_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H);
    screen_base_init(s_scr);   // dark bg, opaque, non-scrollable

    // ── Header bar (full width, 80 px tall) ───────────────────
    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_size(header, DISPLAY_LOGICAL_W, HEADER_H);
    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(header, lv_color_hex(CLR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_pad_left(header, 32, 0);
    lv_obj_set_style_pad_right(header, 32, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *club_lbl = lv_label_create(header);
    lv_label_set_text(club_lbl, CLUB_NAME);
    lv_obj_set_style_text_font(club_lbl, UI_FONT_20, 0);
    lv_obj_set_style_text_color(club_lbl, lv_color_hex(CLR_PRIMARY), 0);
    lv_obj_align(club_lbl, LV_ALIGN_LEFT_MID, 0, 0);

    // WiFi status (right side of header)
    s_lbl_wifi = lv_label_create(header);
    lv_label_set_text(s_lbl_wifi, LV_SYMBOL_WIFI "  NICHT VERBUNDEN");
    lv_obj_set_style_text_font(s_lbl_wifi, UI_FONT_14, 0);
    lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(s_lbl_wifi, LV_ALIGN_RIGHT_MID, 0, 0);

    s_lbl_gateway = lv_label_create(header);
    lv_label_set_text(s_lbl_gateway, "GATEWAY: NICHT KONFIGURIERT");
    lv_obj_set_style_text_font(s_lbl_gateway, UI_FONT_14, 0);
    lv_obj_set_style_text_color(s_lbl_gateway, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(s_lbl_gateway, LV_ALIGN_RIGHT_MID, -260, 0);

    s_lbl_clock = lv_label_create(header);
    lv_label_set_text(s_lbl_clock, "ZEIT: NICHT SYNCHRONISIERT");
    lv_obj_set_style_text_font(s_lbl_clock, UI_FONT_14, 0);
    lv_obj_set_style_text_color(s_lbl_clock, lv_color_hex(CLR_MUTED), 0);
    lv_obj_set_width(s_lbl_clock, 250);
    lv_label_set_long_mode(s_lbl_clock, LV_LABEL_LONG_DOT);
    lv_obj_align(s_lbl_clock, LV_ALIGN_RIGHT_MID, -510, 0);

    // ── Left content area (game history) ──────────────────────
    lv_obj_t *content = lv_obj_create(s_scr);
    lv_obj_set_size(content, CONTENT_W, CONTENT_H);
    lv_obj_align(content, LV_ALIGN_TOP_LEFT, 0, HEADER_H);
    lv_obj_set_style_bg_color(content, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 24, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    // Card inside the content area
    lv_obj_t *hist_card = lv_obj_create(content);
    lv_obj_set_size(hist_card, LV_PCT(100), LV_PCT(100));
    lv_obj_add_style(hist_card, &g_style_card, 0);
    lv_obj_set_style_border_width(hist_card, 2, 0);
    lv_obj_set_style_pad_all(hist_card, 24, 0);
    lv_obj_set_style_pad_row(hist_card, 12, 0);
    lv_obj_set_flex_flow(hist_card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *hist_hdr = lv_label_create(hist_card);
    lv_label_set_text(hist_hdr, "LETZTE SPIELE");
    lv_obj_set_style_text_font(hist_hdr, UI_FONT_16, 0);
    lv_obj_set_style_text_color(hist_hdr, lv_color_hex(CLR_MUTED), 0);
    lv_obj_set_style_text_letter_space(hist_hdr, 2, 0);

    s_history_list = lv_list_create(hist_card);
    lv_obj_set_size(s_history_list, LV_PCT(100), 0);
    lv_obj_set_flex_grow(s_history_list, 1);
    lv_obj_set_style_bg_opa(s_history_list, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_history_list, 0, 0);
    lv_obj_set_style_pad_all(s_history_list, 0, 0);

    // ── Right sidebar ─────────────────────────────────────────
    lv_obj_t *sidebar = lv_obj_create(s_scr);
    lv_obj_set_size(sidebar, SIDEBAR_W, CONTENT_H);
    lv_obj_align(sidebar, LV_ALIGN_TOP_RIGHT, 0, HEADER_H);
    lv_obj_add_style(sidebar, &g_style_sidebar, 0);
    lv_obj_set_style_border_side(sidebar, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(sidebar, 2, 0);
    lv_obj_set_style_border_color(sidebar, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_pad_all(sidebar, 16, 0);
    lv_obj_set_style_pad_row(sidebar, 10, 0);
    lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sidebar, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Large primary "SPILL START" button (140 px tall)
    big_btn(sidebar, LV_SYMBOL_PLAY, "SPIEL START", 140, SCREEN_START);

    // 2. "SPILLER VUM DAG" (credits) - full width, 88 px
    sec_btn(sidebar, LV_SYMBOL_CHARGE, "SPIELER DES TAGES",
            SIDEBAR_W - 32, 88, SCREEN_KREDITE);

    // 3. Row: "SPILLER" | "ASTELLUNGEN" side-by-side
    // Inner width = SIDEBAR_W - 2*pad(16) = 288. Half = (288-8)/2 = 140 px each.
    lv_obj_t *row = lv_obj_create(sidebar);
    lv_obj_set_size(row, SIDEBAR_W - 32, 88);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    sec_btn(row, LV_SYMBOL_LIST,     "SPIELER",    140, 88, SCREEN_SPILLER);
    sec_btn(row, LV_SYMBOL_SETTINGS, "EINSTELLUNGEN",140, 88, SCREEN_EINSTELLUNGEN);

    // 4. "SPILLGESCHICHT" - muted ghost style
    lv_obj_t *hist_btn = lv_btn_create(sidebar);
    lv_obj_set_size(hist_btn, SIDEBAR_W - 32, 68);
    lv_obj_set_style_bg_color(hist_btn, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_bg_opa(hist_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hist_btn, 0, 0);
    lv_obj_set_style_radius(hist_btn, 8, 0);
    lv_obj_set_flex_flow(hist_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hist_btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(hist_btn, 10, 0);
    lv_obj_add_event_cb(hist_btn, [](lv_event_t *) {
        ui_manager_show(SCREEN_GESCHICHTE);
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hist_ic = lv_label_create(hist_btn);
    lv_label_set_text(hist_ic, LV_SYMBOL_FILE);
    lv_obj_set_style_text_font(hist_ic, UI_FONT_18, 0);
    lv_obj_set_style_text_color(hist_ic, lv_color_hex(CLR_MUTED), 0);

    lv_obj_t *hist_lbl = lv_label_create(hist_btn);
    lv_label_set_text(hist_lbl, "SPIELVERLAUF");
    lv_obj_set_style_text_font(hist_lbl, UI_FONT_14, 0);
    lv_obj_set_style_text_color(hist_lbl, lv_color_hex(CLR_MUTED), 0);

    // 5. Graceful shutdown: sync first, then put the ESP32 into deep sleep.
    lv_obj_t *shutdown_btn = lv_btn_create(sidebar);
    lv_obj_add_style(shutdown_btn, &g_style_btn_danger, 0);
    lv_obj_set_size(shutdown_btn, SIDEBAR_W - 32, 50);
    lv_obj_add_event_cb(shutdown_btn, shutdown_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *shutdown_label = lv_label_create(shutdown_btn);
    lv_label_set_text(shutdown_label, LV_SYMBOL_POWER "  AUSSCHALTEN");
    lv_obj_set_style_text_font(shutdown_label, UI_FONT_14, 0);
    lv_obj_center(shutdown_label);

    // 6. Offline Queue panel - flex-grow fills the remaining space
    lv_obj_t *queue = lv_obj_create(sidebar);
    lv_obj_set_flex_grow(queue, 1);
    lv_obj_set_width(queue, SIDEBAR_W - 32);
    lv_obj_set_style_bg_color(queue, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_bg_opa(queue, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(queue, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_width(queue, 2, 0);
    lv_obj_set_style_radius(queue, 10, 0);
    lv_obj_set_style_pad_all(queue, 14, 0);
    lv_obj_set_style_pad_row(queue, 8, 0);
    lv_obj_set_flex_flow(queue, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(queue, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(queue, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *q_hdr = lv_label_create(queue);
    lv_label_set_text(q_hdr, "OFFLINE-WARTESCHLANGE");
    lv_obj_set_style_text_font(q_hdr, UI_FONT_12, 0);
    lv_obj_set_style_text_color(q_hdr, lv_color_hex(CLR_MUTED), 0);
    lv_obj_set_style_text_letter_space(q_hdr, 1, 0);

    s_lbl_pending = lv_label_create(queue);
    lv_label_set_text(s_lbl_pending, "0 AKTIONEN AUSSTEHEND");
    lv_obj_set_style_text_font(s_lbl_pending, UI_FONT_14, 0);
    lv_obj_set_style_text_color(s_lbl_pending, lv_color_hex(CLR_TEXT), 0);
    lv_label_set_long_mode(s_lbl_pending, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_lbl_pending, LV_PCT(100));

    s_lbl_sync_status = lv_label_create(queue);
    lv_label_set_text(s_lbl_sync_status, "BEREET");
    lv_obj_set_style_text_font(s_lbl_sync_status, UI_FONT_12, 0);
    lv_obj_set_style_text_color(s_lbl_sync_status, lv_color_hex(CLR_MUTED), 0);
    lv_label_set_long_mode(s_lbl_sync_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_lbl_sync_status, LV_PCT(100));

    lv_obj_t *sync_btn = lv_btn_create(queue);
    lv_obj_add_style(sync_btn, &g_style_btn_primary, 0);
    lv_obj_set_size(sync_btn, LV_PCT(100), 50);
    lv_obj_add_event_cb(sync_btn, sync_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sync_lbl = lv_label_create(sync_btn);
    lv_label_set_text(sync_lbl, LV_SYMBOL_REFRESH "  ALLES SYNCHRONISIEREN");
    lv_obj_set_style_text_font(sync_lbl, UI_FONT_14, 0);
    lv_obj_set_style_text_color(sync_lbl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(sync_lbl);

    return s_scr;
}

// ── screen_dashboard_refresh ──────────────────────────────────
void screen_dashboard_refresh(void)
{
    if (!s_lbl_sync_status) return;

    SyncUiState sync_state = {};
    store_get_sync_ui_state(&sync_state);

    // Sync status label
    char buf[128];
    switch (sync_state.status) {
        case SYNC_IDLE:
            set_label_text_if_changed(s_lbl_sync_status, "BEREET");
            break;
        case SYNC_RUNNING:
            set_label_text_if_changed(s_lbl_sync_status,
                                       LV_SYMBOL_REFRESH " SYNCHRONISIERT...");
            break;
        case SYNC_SUCCESS:
            set_label_text_if_changed(s_lbl_sync_status,
                                       LV_SYMBOL_OK " SYNCHRONISIERT");
            break;
        case SYNC_ERROR:
            snprintf(buf, sizeof(buf),
                      LV_SYMBOL_WARNING " Fehler: %.40s", sync_state.error);
            set_label_text_if_changed(s_lbl_sync_status, buf);
            break;
    }

    // Pending games
    snprintf(buf, sizeof(buf), "%d AKTIONEN | %s | LETZTER SYNC: %lld",
             store_pending_action_count(),
              g_store.offlineCacheLoaded ? "OFFLINE-CACHE" : "KEIN CACHE",
             (long long)g_store.lastSuccessfulSyncAt);
    set_label_text_if_changed(s_lbl_pending, buf);

    // WiFi
    CopWifiState wifi_state = cop_wifi_state();
    if (wifi_state == COP_WIFI_CONNECTED) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI "  %s", g_store.wifiIp);
        set_label_text_if_changed(s_lbl_wifi, buf);
    } else {
        snprintf(buf, sizeof(buf), "WIFI: %s", cop_wifi_state_label(wifi_state));
        set_label_text_if_changed(s_lbl_wifi, buf);
    }
    if (wifi_state != s_rendered_wifi_state) {
        uint32_t color = wifi_state == COP_WIFI_CONNECTED ? CLR_SUCCESS :
                         (wifi_state == COP_WIFI_CONNECTING ||
                          wifi_state == COP_WIFI_RECONNECTING) ? CLR_WARN :
                         wifi_state == COP_WIFI_NOT_CONFIGURED ? CLR_MUTED : CLR_DANGER;
        lv_obj_set_style_text_color(s_lbl_wifi, lv_color_hex(color), 0);
        s_rendered_wifi_state = wifi_state;
    }
    if (s_lbl_gateway) {
        GatewayReachability gateway_state = lora_gateway_state();
        snprintf(buf, sizeof(buf), "GATEWAY: %s",
                 lora_gateway_state_label(gateway_state));
        set_label_text_if_changed(s_lbl_gateway, buf);
        if (gateway_state != s_rendered_gateway_state) {
            uint32_t color = gateway_state == GATEWAY_REACHABLE ? CLR_SUCCESS :
                             gateway_state == GATEWAY_CHECKING ? CLR_WARN :
                             gateway_state == GATEWAY_NOT_CONFIGURED ? CLR_MUTED : CLR_DANGER;
            lv_obj_set_style_text_color(s_lbl_gateway, lv_color_hex(color), 0);
            s_rendered_gateway_state = gateway_state;
        }
    }
    if (s_lbl_clock) {
        time_t now = time(NULL);
        struct tm local;
        localtime_r(&now, &local);
        char clock_text[48];
        strftime(clock_text, sizeof(clock_text), "ZEIT: %d.%m.%Y  %H:%M:%S", &local);
        set_label_text_if_changed(s_lbl_clock, clock_text);
        lv_obj_set_style_text_color(
            s_lbl_clock,
            lv_color_hex(now >= 1704067200 ? CLR_SUCCESS : CLR_DANGER), 0);
    }

    // History list (most-recent 5 entries) - proper-height rows. Do not
    // destroy and rebuild it on the periodic status-only refresh.
    uint32_t history_signature = current_history_signature();
    if (history_signature == s_history_signature) return;
    s_history_signature = history_signature;
    lv_obj_clean(s_history_list);
    if (g_store.historyCount == 0) {
        lv_obj_t *empty = lv_label_create(s_history_list);
        lv_label_set_text(empty, "NOCH KEINE SPIELE");
        lv_obj_set_style_text_font(empty, UI_FONT_16, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(CLR_MUTED), 0);
    } else {
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
        int show = n < 5 ? n : 5;
        for (int ii = 0; ii < show; ii++) {
            const FinishedGame *fg = &g_store.history[idx[ii]];

            // Find winner name
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

            lv_obj_t *row = lv_obj_create(s_history_list);
            lv_obj_set_size(row, LV_PCT(100), 52);
            lv_obj_set_style_bg_color(row, lv_color_hex(CLR_CARD), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(row, lv_color_hex(CLR_BORDER), 0);
            lv_obj_set_style_border_width(row, 1, 0);
            lv_obj_set_style_radius(row, 6, 0);
            lv_obj_set_style_pad_hor(row, 14, 0);
            lv_obj_set_style_pad_ver(row, 0, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                                  LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

            char ts[24];
            fmt_local_time(fg->finishedAt, ts, sizeof(ts));
            lv_obj_t *ts_lbl = lv_label_create(row);
            lv_label_set_text(ts_lbl, ts[0] ? ts : "-");
            lv_obj_set_style_text_font(ts_lbl, UI_FONT_14, 0);
            lv_obj_set_style_text_color(ts_lbl, lv_color_hex(CLR_MUTED), 0);
            lv_obj_set_width(ts_lbl, 180);

            lv_obj_t *mode_lbl = lv_label_create(row);
            lv_label_set_text(mode_lbl, modus_label(fg->base.modus));
            lv_obj_set_style_text_font(mode_lbl, UI_FONT_16, 0);
            lv_obj_set_style_text_color(mode_lbl, lv_color_hex(CLR_PRIMARY), 0);
            lv_obj_set_width(mode_lbl, 120);

            char w_buf[80];
            snprintf(w_buf, sizeof(w_buf), LV_SYMBOL_CHARGE " %s  %dPKT",
                     winner, best_pts);
            lv_obj_t *win_lbl = lv_label_create(row);
            lv_label_set_text(win_lbl, w_buf);
            lv_obj_set_style_text_font(win_lbl, UI_FONT_16, 0);
            lv_obj_set_style_text_color(win_lbl, lv_color_hex(CLR_TEXT), 0);
            lv_label_set_long_mode(win_lbl, LV_LABEL_LONG_DOT);
            lv_obj_set_width(win_lbl, 0);
            lv_obj_set_flex_grow(win_lbl, 1);

            char p_buf[12];
            snprintf(p_buf, sizeof(p_buf), "%d SPIELER", fg->spieler_count);
            lv_obj_t *p_lbl = lv_label_create(row);
            lv_label_set_text(p_lbl, p_buf);
            lv_obj_set_style_text_font(p_lbl, UI_FONT_14, 0);
            lv_obj_set_style_text_color(p_lbl, lv_color_hex(CLR_MUTED), 0);
        }
    }
}

void screen_dashboard_tick(void)
{
    SyncUiState sync_state = {};
    store_get_sync_ui_state(&sync_state);
    if (s_shutdown_sync_pending) {
        if (sync_state.status == SYNC_SUCCESS) {
            // The sync worker has reported success; no shutdown path can
            // reach deep sleep before this point.
            ESP_LOGI("dashboard", "Sync complete; entering deep sleep");
            esp_deep_sleep_start();
        }
        if (sync_state.status == SYNC_ERROR) {
            s_shutdown_sync_pending = false;
            char message[160];
            snprintf(message, sizeof(message), "SYNC FEHLGESCHLAGEN. TERMINAL BLEIBT AN.\n%.100s",
                       sync_state.error[0] ? sync_state.error : "ERNEUT VERSUCHEN.");
            set_shutdown_message(message, CLR_DANGER);
            screen_dashboard_refresh();
        }
    }

    static uint32_t last = 0;
    if (lv_tick_get() - last > 2000) {
        last = lv_tick_get();
        screen_dashboard_refresh();
    }
}
