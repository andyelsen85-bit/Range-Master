// ============================================================
// Kreditter screen - credit management for today's players
// Mirrors KrediteScreen.tsx
// ============================================================
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "lvgl.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_kredite.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_player_list;
static lv_obj_t *s_dd_add;
static lv_obj_t *s_lbl_status;
static lv_obj_t *s_bill_modal;
static lv_obj_t *s_totals_label;

typedef struct {
    int spielerId;
    lv_obj_t *creditLabel;
    lv_obj_t *ammoLabel;
    lv_obj_t *minusButton;
    lv_obj_t *ammo12MinusButton;
    lv_obj_t *ammo20MinusButton;
    bool creditPositive;
} PlayerRowRefs;

static PlayerRowRefs s_row_refs[MAX_PORTAL_SPIELER];
static int s_row_ref_count;

static void refresh_async(void *)
{
    screen_kredite_refresh();
}

static void request_refresh(void)
{
    // Rebuilding the list deletes the button that emitted the current event.
    // Defer that work until LVGL has finished dispatching the click callback.
    lv_async_call(refresh_async, NULL);
}

static void refresh_player_values(int spieler_id)
{
    KreditStand *stand = NULL;
    for (int i = 0; i < MAX_PORTAL_SPIELER; ++i)
        if (g_store.kreditPlayerIds[i] == spieler_id) {
            stand = &g_store.kredite[i];
            break;
        }
    for (int i = 0; stand && i < s_row_ref_count; ++i) {
        PlayerRowRefs *refs = &s_row_refs[i];
        if (refs->spielerId != spieler_id) continue;
        int available = stand->gewaehrt - stand->verbraucht;
        char text[48];
        snprintf(text, sizeof(text), "%d KREDITTER VERFUGBAR  (%d/%d VERBRAUCHT)",
                 available, stand->verbraucht, stand->gewaehrt);
        const char *current = lv_label_get_text(refs->creditLabel);
        if (!current || strcmp(current, text) != 0)
            lv_label_set_text(refs->creditLabel, text);
        if (refs->creditPositive != (available > 0)) {
            lv_obj_set_style_text_color(refs->creditLabel,
                available > 0 ? lv_color_hex(CLR_SUCCESS) : lv_color_hex(CLR_DANGER), 0);
            refs->creditPositive = available > 0;
        }
        snprintf(text, sizeof(text), "CAL.12: %d     CAL.20: %d",
                 store_munition_cal12(spieler_id), store_munition_cal20(spieler_id));
        current = lv_label_get_text(refs->ammoLabel);
        if (!current || strcmp(current, text) != 0)
            lv_label_set_text(refs->ammoLabel, text);
        auto set_disabled = [](lv_obj_t *button, bool disabled) {
            bool current_disabled = lv_obj_has_state(button, LV_STATE_DISABLED);
            if (disabled != current_disabled) {
                if (disabled) lv_obj_add_state(button, LV_STATE_DISABLED);
                else lv_obj_clear_state(button, LV_STATE_DISABLED);
            }
        };
        set_disabled(refs->minusButton, available <= 0);
        set_disabled(refs->ammo12MinusButton, store_munition_cal12(spieler_id) <= 0);
        set_disabled(refs->ammo20MinusButton, store_munition_cal20(spieler_id) <= 0);
        break;
    }
    if (s_totals_label) {
        char text[96];
        snprintf(text, sizeof(text),
                 "CREDITS                 CAL.12: %" PRId32 "       CAL.20: %" PRId32,
                 g_store.verkaufCal12Total, g_store.verkaufCal20Total);
        const char *current = lv_label_get_text(s_totals_label);
        if (!current || strcmp(current, text) != 0)
            lv_label_set_text(s_totals_label, text);
    }
}

typedef struct {
    int spielerId;
    const char *produktCode;
    int quantity;
} KreditAction;

static void free_action_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

static bool add_action(lv_obj_t *button, lv_event_cb_t callback, int spieler_id,
                       const char *produkt_code, int quantity)
{
    KreditAction *action = (KreditAction *)malloc(sizeof(*action));
    if (!action) {
        lv_obj_add_state(button, LV_STATE_DISABLED);
        return false;
    }
    *action = (KreditAction){spieler_id, produkt_code, quantity};
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, action);
    lv_obj_add_event_cb(button, free_action_cb, LV_EVENT_DELETE, action);
    return true;
}

static void set_disabled_appearance(lv_obj_t *button)
{
    lv_obj_set_style_bg_color(button, lv_color_hex(CLR_MUTED), LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_DISABLED);
}

static PlayerBill *cached_bill(int spieler_id)
{
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &tm);
    if (strcmp(g_store.billDay.datum, today)) return NULL;
    for (int i = 0; i < g_store.billDay.playerCount; ++i)
        if (g_store.billDay.players[i].spielerId == spieler_id)
            return &g_store.billDay.players[i];
    return NULL;
}

static void close_bill_modal(void)
{
    if (s_bill_modal) lv_obj_del(s_bill_modal);
    s_bill_modal = NULL;
}

static bool is_today(const char *datum)
{
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &tm);
    return datum && strcmp(datum, today) == 0;
}

static void paid_cb(lv_event_t *e)
{
    int spieler_id = (int)(intptr_t)lv_event_get_user_data(e);
    if (store_payment_pending(spieler_id)) {
        lv_label_set_text(s_lbl_status, "BEZUELUNG WAART OP PORTAL-ACCEPT");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_WARN), 0);
    } else if (!store_queue_payment(spieler_id)) {
        lv_label_set_text(s_lbl_status, "BEZUELUNG KONNT NET GESPAECHERT GINN");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_DANGER), 0);
    } else if (store_sync()) {
        lv_label_set_text(s_lbl_status, "BEZUELUNG GESPAECHERT - PORTAL SYNC...");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_WARN), 0);
    } else {
        lv_label_set_text(s_lbl_status, "BEZUELUNG OFFLINE PENDING");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_WARN), 0);
    }
    close_bill_modal();
    // A queued/running sync owns the shared billing datasets until its
    // publication generation completes. ui_manager refreshes this screen once.
    if (!store_sync_is_queued_or_running())
        screen_kredite_refresh();
}

static void bill_cb(lv_event_t *e)
{
    int sid = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_bill_modal) return;
    const char *name = "ONBEKANNT";
    KreditStand stand = {};
    PlayerBill *bill_data = cached_bill(sid);
    for (int i = 0; i < g_store.portalSpielerCount; ++i)
        if (g_store.portalSpieler[i].id == sid) { name = g_store.portalSpieler[i].name; break; }
    for (int i = 0; i < MAX_PORTAL_SPIELER; ++i)
        if (g_store.kreditPlayerIds[i] == sid) { stand = g_store.kredite[i]; break; }
    s_bill_modal = lv_obj_create(s_scr);
    lv_obj_set_size(s_bill_modal, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H);
    lv_obj_set_style_bg_color(s_bill_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_bill_modal, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_bill_modal, 0, 0);
    lv_obj_clear_flag(s_bill_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *card = lv_obj_create(s_bill_modal);
    lv_obj_set_size(card, 760, 620);
    lv_obj_center(card); lv_obj_add_style(card, &g_style_card, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(card);
    char title_text[100]; snprintf(title_text, sizeof(title_text), "RECHNUNG - %s", name);
    lv_label_set_text(title, title_text); lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    char totals[256];
    int granted = bill_data ? bill_data->creditGranted : stand.gewaehrt;
    int used = bill_data ? bill_data->creditUsed : stand.verbraucht;
    int remaining = bill_data ? bill_data->creditRemaining : granted - used;
    snprintf(totals, sizeof(totals),
             "KREDITTER: %d GEWAEHRT  |  %d GENOTZT  |  %d RESCHT\n"
             "SPILLER: %d SPILLER / %d OFGESCHLOSS  |  %d CONFIRMED CLAYS",
             granted, used, remaining,
             bill_data ? bill_data->games : 0,
             bill_data ? bill_data->completedGames : 0,
             bill_data ? bill_data->confirmedClays : 0);
    lv_obj_t *summary = lv_label_create(card); lv_label_set_text(summary, totals);
    lv_obj_set_style_text_font(summary, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(summary, lv_color_hex(CLR_TEXT), 0);
    // The item list alone scrolls: settlement controls stay visible even for
    // a long Catering bill.
    lv_obj_t *items = lv_obj_create(card);
    lv_obj_set_size(items, LV_PCT(100), 210);
    lv_obj_set_style_bg_opa(items, LV_OPA_0, 0);
    lv_obj_set_style_border_width(items, 0, 0);
    lv_obj_set_style_pad_all(items, 0, 0);
    lv_obj_set_style_pad_row(items, 5, 0);
    lv_obj_set_flex_flow(items, LV_FLEX_FLOW_COLUMN);
    int fallback_total = 0, fallback_line_count = 0;
    BillCategoryTotal fallback_categories[MAX_BILL_CATEGORIES] = {};
    int fallback_category_count = 0;
    auto add_fallback_category = [&](const char *category, int cents) {
        for (int i = 0; i < fallback_category_count; ++i)
            if (!strcmp(fallback_categories[i].name, category)) {
                fallback_categories[i].totalCent += cents; return;
            }
        if (fallback_category_count < MAX_BILL_CATEGORIES) {
            snprintf(fallback_categories[fallback_category_count].name,
                     sizeof(fallback_categories[0].name), "%s", category);
            fallback_categories[fallback_category_count++].totalCent = cents;
        }
    };
    auto add_item_row = [&](const char *product, const char *category, int quantity,
                             int unit_cent, int line_cent, bool pending) {
        lv_obj_t *line_row = lv_obj_create(items);
        lv_obj_set_size(line_row, LV_PCT(100), 48);
        lv_obj_set_style_pad_hor(line_row, 8, 0);
        lv_obj_set_style_pad_ver(line_row, 4, 0);
        lv_obj_set_flex_flow(line_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(line_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *left = lv_label_create(line_row); char left_text[96];
        bool price_unknown = unit_cent == VERKAUF_UNIT_PRICE_UNKNOWN;
        if (price_unknown)
            snprintf(left_text, sizeof(left_text), "PENDING  %s [%s]\n%d x PRAIS ONBEKANNT",
                     product, category, quantity);
        else
            snprintf(left_text, sizeof(left_text), "%s%s [%s]\n%d x %d.%02d EUR",
                     pending ? "PENDING  " : "", product, category, quantity,
                     unit_cent / 100, abs(unit_cent % 100));
        lv_label_set_text(left, left_text);
        lv_obj_set_style_text_font(left, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(left,
            pending ? lv_color_hex(CLR_WARN) : lv_color_hex(CLR_TEXT), 0);
        lv_obj_t *right = lv_label_create(line_row); char right_text[32];
        if (price_unknown) snprintf(right_text, sizeof(right_text), "RECONCILE");
        else snprintf(right_text, sizeof(right_text), "%d.%02d EUR",
                      line_cent / 100, abs(line_cent % 100));
        lv_label_set_text(right, right_text);
        lv_obj_set_style_text_font(right, &lv_font_montserrat_14, 0);
    };
    if (bill_data) {
        for (int i = 0; i < bill_data->lineCount; ++i) {
            const BillLine *line = &bill_data->lines[i];
            add_item_row(line->produktName, line->category, line->quantity,
                         line->unitPriceCent, line->lineTotalCent, line->localPending);
        }
    } else {
        // Before a bill GET is cached, retained sale events are still a useful
        // local receipt. This includes both Catering and ammunition events.
        for (int i = 0; i < g_store.pendingVerkaufEventCount; ++i) {
            const VerkaufEvent *event = &g_store.pendingVerkaufEvents[i];
            if (event->spielerId != sid || !is_today(event->datum)) continue;
            bool price_unknown = event->unitPriceCent == VERKAUF_UNIT_PRICE_UNKNOWN;
            int cents = price_unknown ? 0 : event->quantity * event->unitPriceCent;
            add_item_row(event->produktName[0] ? event->produktName : "ONBEKANNT",
                         event->category[0] ? event->category : "ONBEKANNT",
                         event->quantity, event->unitPriceCent, cents, true);
            fallback_line_count++;
            fallback_total += cents;
            add_fallback_category(event->category[0] ? event->category : "ONBEKANNT", cents);
        }
        if (fallback_line_count == 0) {
            lv_obj_t *empty = lv_label_create(items);
            lv_label_set_text(empty, "KENG LOKAL VERKAAF-EVENT FIR DESSE SPILLER");
            lv_obj_set_style_text_color(empty, lv_color_hex(CLR_MUTED), 0);
        }
    }
    const BillCategoryTotal *category_totals =
        bill_data ? bill_data->categories : fallback_categories;
    int category_count = bill_data ? bill_data->categoryCount : fallback_category_count;
    for (int i = 0; i < category_count; ++i) {
        lv_obj_t *category = lv_label_create(card); char text[384];
        snprintf(text, sizeof(text), "%s TOTAL: %d.%02d EUR",
                 category_totals[i].name, category_totals[i].totalCent / 100,
                 abs(category_totals[i].totalCent % 100));
        lv_label_set_text(category, text);
        lv_obj_set_style_text_color(category, lv_color_hex(CLR_MUTED), 0);
    }
    lv_obj_t *general = lv_label_create(card); char general_text[128];
    snprintf(general_text, sizeof(general_text), "GENERAL TOTAL: %d.%02d EUR  |  STATUS: %s",
              bill_data ? bill_data->totalCent / 100 : fallback_total / 100,
              bill_data ? abs(bill_data->totalCent % 100) : abs(fallback_total % 100),
             store_payment_pending(sid) ? "PAID PENDING" :
             bill_data && bill_data->state == BILL_PAID ? "PAID" :
             bill_data && bill_data->state == BILL_PENDING_NEUTRAL ? "PENDING NEUTRAL" : "OPEN");
    lv_label_set_text(general, general_text);
    lv_obj_set_style_text_font(general, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(general, lv_color_hex(CLR_PRIMARY), 0);
    if (bill_data && bill_data->lineOverflow) {
        lv_obj_t *overflow = lv_label_create(card);
        lv_label_set_text(overflow, "RECHNUNG ZEILENLIMIT ERREECHT - PORTALDETAILER PRUEWEN");
        lv_obj_set_style_text_color(overflow, lv_color_hex(CLR_DANGER), 0);
    }
    lv_obj_t *hint = lv_label_create(card);
    char audit[160];
    snprintf(audit, sizeof(audit), "%s%s%s",
             bill_data && bill_data->paymentSource[0] ? "BEZUELUNG: " : "",
             bill_data ? bill_data->paymentSource : "",
             bill_data && bill_data->paidAt[0] ? "  (PORTAL AUDIT GESPEICHERT)" :
             "  PAID BLEIFT PENDING BIS PORTAL ACCEPT.");
    lv_label_set_text(hint, audit);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(CLR_MUTED), 0);
    lv_obj_t *actions = lv_obj_create(card);
    lv_obj_set_size(actions, LV_PCT(100), 60); lv_obj_set_style_bg_opa(actions, LV_OPA_0, 0);
    lv_obj_set_style_border_width(actions, 0, 0); lv_obj_set_style_pad_all(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(actions, 16, 0);
    lv_obj_t *cancel = lv_btn_create(actions); lv_obj_add_style(cancel, &g_style_btn_secondary, 0);
    lv_obj_set_size(cancel, 180, 52); lv_obj_add_event_cb(cancel, [](lv_event_t *) { close_bill_modal(); }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(cancel); lv_label_set_text(cl, "ZURUCK"); lv_obj_center(cl);
    lv_obj_t *paid = lv_btn_create(actions);
    lv_obj_add_style(paid, store_payment_pending(sid) ? &g_style_btn_secondary : &g_style_btn_primary, 0);
    lv_obj_set_size(paid, 260, 52);
    lv_obj_add_event_cb(paid, paid_cb, LV_EVENT_CLICKED, (void *)(intptr_t)sid);
    lv_obj_t *pl = lv_label_create(paid);
    bool already_paid = bill_data && bill_data->state == BILL_PAID;
    lv_label_set_text(pl, store_payment_pending(sid) ? "PAID PENDING" :
                          already_paid ? "PAID" : LV_SYMBOL_OK "  PAID CONFIRM");
    if (already_paid) lv_obj_add_state(paid, LV_STATE_DISABLED);
    lv_obj_center(pl);
}

static void munition_cb(lv_event_t *e)
{
    const KreditAction *action = (const KreditAction *)lv_event_get_user_data(e);
    if (!action || !store_queue_verkauf(action->spielerId, action->produktCode,
                                         action->quantity)) {
        lv_label_set_text(s_lbl_status, "VERKAUF KONNT NET GESPAECHERT GINN");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_DANGER), 0);
        return;
    }
    lv_label_set_text(s_lbl_status, "VERKAAF GESPEICHERT");
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_SUCCESS), 0);
    refresh_player_values(action->spielerId);
}

// ── Grant credit (+1) ─────────────────────────────────────────
static void grant_cb(lv_event_t *e)
{
    int spieler_id = (int)(intptr_t)lv_event_get_user_data(e);
    if (!store_adjust_kredite(spieler_id, 1)) {
        lv_label_set_text(s_lbl_status, "KREDITT KONNT NET GESPAECHERT GINN");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_DANGER), 0);
    } else {
        lv_label_set_text(s_lbl_status, "+1 KREDITT GESPAECHERT");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_SUCCESS), 0);
    }
    refresh_player_values(spieler_id);
}

// ── Revoke credit (-1, never below 0 available) ───────────────
static void revoke_cb(lv_event_t *e)
{
    int spieler_id = (int)(intptr_t)lv_event_get_user_data(e);
    if (!store_adjust_kredite(spieler_id, -1)) {
        lv_label_set_text(s_lbl_status, "KREDITT KONNT NET GESPAECHERT GINN");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_DANGER), 0);
    } else {
        lv_label_set_text(s_lbl_status, "-1 KREDITT GESPAECHERT");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_SUCCESS), 0);
    }
    refresh_player_values(spieler_id);
}

// ── Remove player from today's list ──────────────────────────
static void remove_player_cb(lv_event_t *e)
{
    int spieler_id = (int)(intptr_t)lv_event_get_user_data(e);
    char reason[64];
    if (!store_remove_spieler_fuer_tag(spieler_id, reason, sizeof(reason))) {
        char message[112];
        snprintf(message, sizeof(message), "NET GELOSCHT: %s", reason);
        lv_label_set_text(s_lbl_status, message);
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_DANGER), 0);
        return;
    }
    lv_label_set_text(s_lbl_status, "SPILLER GELOSCHT");
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_WARN), 0);
    screen_kredite_refresh();
}

// ── Add player to today's list ────────────────────────────────
static void add_player_cb(lv_event_t *e)
{
    if (!s_dd_add) return;
    uint16_t sel = lv_dropdown_get_selected(s_dd_add);
    if (sel == 0) return;                              // "-" placeholder
    uint16_t idx = sel - 1;                            // offset for leading "-"
    if (idx >= (uint16_t)g_store.portalSpielerCount) return;
    PortalSpieler *ps = &g_store.portalSpieler[idx];
    store_register_spieler_fuer_tag(ps->id);
    lv_label_set_text(s_lbl_status, "SPILLER DOBAIGESAT");
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_SUCCESS), 0);
    screen_kredite_refresh();
}

// ── Build player list ─────────────────────────────────────────
static void build_player_list(void)
{
    lv_obj_clean(s_player_list);
    s_row_ref_count = 0;

    int count = 0;
    // The canonical sales GET is a day aggregate (not player-attributed), so
    // expose its reconciled totals once above the locally attributed rows.
    lv_obj_t *columns = lv_label_create(s_player_list);
    s_totals_label = columns;
    char columns_text[96];
    snprintf(columns_text, sizeof(columns_text),
             "CREDITS                 CAL.12: %" PRId32 "       CAL.20: %" PRId32,
             g_store.verkaufCal12Total, g_store.verkaufCal20Total);
    lv_label_set_text(columns, columns_text);
    lv_obj_set_style_text_font(columns, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(columns, lv_color_hex(CLR_MUTED), 0);
    for (int i = 0; i < MAX_PORTAL_SPIELER; i++) {
        if (g_store.kreditPlayerIds[i] == 0) continue;
        int sid = g_store.kreditPlayerIds[i];
        KreditStand *k = &g_store.kredite[i];

        // Find player name
        const char *name = "ONBEKANNT";
        for (int j = 0; j < g_store.portalSpielerCount; j++) {
            if (g_store.portalSpieler[j].id == sid) {
                name = g_store.portalSpieler[j].name;
                break;
            }
        }

        lv_obj_t *row = lv_obj_create(s_player_list);
        // Each player gets enough vertical room for the labelled control
        // groups; compact single-row controls clipped on the touch display.
        lv_obj_set_size(row, LV_PCT(100), 116);
        lv_obj_add_style(row, &g_style_card, 0);
        lv_obj_set_style_pad_all(row, 10, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Name + the three independent daily counters.
        lv_obj_t *info = lv_obj_create(row);
        lv_obj_set_size(info, 245, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(info, LV_OPA_0, 0);
        lv_obj_set_style_border_width(info, 0, 0);
        lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);

        lv_obj_t *name_lbl = lv_label_create(info);
        lv_label_set_text(name_lbl, name);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(CLR_TEXT), 0);

        char cred_buf[48];
        int avail = k->gewaehrt - k->verbraucht;
        snprintf(cred_buf, sizeof(cred_buf),
                 "%d KREDITTER VERFUGBAR  (%d/%d VERBRAUCHT)",
                 avail, k->verbraucht, k->gewaehrt);
        lv_obj_t *cred_lbl = lv_label_create(info);
        lv_label_set_text(cred_lbl, cred_buf);
        lv_obj_set_style_text_font(cred_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(cred_lbl,
            avail > 0 ? lv_color_hex(CLR_SUCCESS) : lv_color_hex(CLR_DANGER), 0);
        char ammo_buf[48];
        snprintf(ammo_buf, sizeof(ammo_buf), "CAL.12: %d     CAL.20: %d",
                 store_munition_cal12(sid), store_munition_cal20(sid));
        lv_obj_t *ammo_lbl = lv_label_create(info);
        lv_label_set_text(ammo_lbl, ammo_buf);
        lv_obj_set_style_text_font(ammo_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(ammo_lbl, lv_color_hex(CLR_PRIMARY), 0);

        // Keep every operation in an explicitly labelled group.  This makes
        // the two ammunition calibers unambiguous and leaves tap spacing.
        lv_obj_t *btn_grp = lv_obj_create(row);
        lv_obj_set_size(btn_grp, 480, 88);
        lv_obj_set_style_bg_opa(btn_grp, LV_OPA_0, 0);
        lv_obj_set_style_border_width(btn_grp, 0, 0);
        lv_obj_set_style_pad_all(btn_grp, 0, 0);
        lv_obj_set_flex_flow(btn_grp, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(btn_grp, 10, 0);
        auto control_group = [&](const char *caption, int width) {
            lv_obj_t *group = lv_obj_create(btn_grp);
            lv_obj_set_size(group, width, 84);
            lv_obj_set_style_bg_opa(group, LV_OPA_0, 0);
            lv_obj_set_style_border_width(group, 0, 0);
            lv_obj_set_style_pad_all(group, 0, 0);
            lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_style_pad_row(group, 3, 0);
            lv_obj_t *label = lv_label_create(group);
            lv_label_set_text(label, caption);
            lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(CLR_MUTED), 0);
            return group;
        };

        lv_obj_t *bill_group = control_group("BILL", 78);
        lv_obj_t *bill = lv_btn_create(bill_group);
        lv_obj_add_style(bill, &g_style_btn_secondary, 0);
        lv_obj_set_size(bill, 76, 52);
        lv_obj_add_event_cb(bill, bill_cb, LV_EVENT_CLICKED, (void *)(intptr_t)sid);
        lv_obj_t *bill_label = lv_label_create(bill);
        lv_label_set_text(bill_label, store_payment_pending(sid) ? "PENDING" : "BILL");
        lv_obj_set_style_text_font(bill_label, &lv_font_montserrat_12, 0);
        lv_obj_center(bill_label);

        lv_obj_t *credit_group = control_group("KREDIT", 92);
        lv_obj_t *credit_buttons = lv_obj_create(credit_group);
        lv_obj_set_size(credit_buttons, 92, 54); lv_obj_set_style_bg_opa(credit_buttons, LV_OPA_0, 0);
        lv_obj_set_style_border_width(credit_buttons, 0, 0); lv_obj_set_style_pad_all(credit_buttons, 0, 0);
        lv_obj_set_flex_flow(credit_buttons, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(credit_buttons, 4, 0);
        // -1
        lv_obj_t *btn_minus = lv_btn_create(credit_buttons);
        lv_obj_set_size(btn_minus, 44, 52);
        lv_obj_set_style_bg_color(btn_minus, lv_color_hex(CLR_WARN), 0);
        lv_obj_set_style_bg_opa(btn_minus, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn_minus, 8, 0);
        lv_obj_set_style_border_width(btn_minus, 0, 0);
        set_disabled_appearance(btn_minus);
        lv_obj_add_event_cb(btn_minus, revoke_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)sid);
        if (avail <= 0) lv_obj_add_state(btn_minus, LV_STATE_DISABLED);
        lv_obj_t *bml = lv_label_create(btn_minus);
        lv_label_set_text(bml, "-1");
        lv_obj_set_style_text_font(bml, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(bml, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(bml);

        // +1
        lv_obj_t *btn_plus = lv_btn_create(credit_buttons);
        lv_obj_add_style(btn_plus, &g_style_btn_primary, 0);
        lv_obj_set_size(btn_plus, 44, 52);
        lv_obj_add_event_cb(btn_plus, grant_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)sid);
        lv_obj_t *bpl = lv_label_create(btn_plus);
        lv_label_set_text(bpl, "+1");
        lv_obj_set_style_text_font(bpl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(bpl, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(bpl);

        PlayerRowRefs *refs = &s_row_refs[s_row_ref_count++];
        refs->spielerId = sid;
        refs->creditLabel = cred_lbl;
        refs->ammoLabel = ammo_lbl;
        refs->minusButton = btn_minus;
        refs->ammo12MinusButton = NULL;
        refs->ammo20MinusButton = NULL;
        refs->creditPositive = avail > 0;

        lv_obj_t *ammo_buttons[2] = {NULL, NULL};
        const struct {
            const char *label;
            const char *product;
            int quantity;
            int available;
        } ammo_actions[] = {
            {"-1", "AMMO_CAL12", -1, store_munition_cal12(sid)},
            {"+1", "AMMO_CAL12",  1, store_munition_cal12(sid)},
            {"-1", "AMMO_CAL20", -1, store_munition_cal20(sid)},
            {"+1", "AMMO_CAL20",  1, store_munition_cal20(sid)},
        };
        for (size_t action = 0; action < sizeof(ammo_actions) / sizeof(ammo_actions[0]); ++action) {
            if (action == 0 || action == 2) {
                lv_obj_t *ammo_group = control_group(action == 0 ? "CAL.12" : "CAL.20", 92);
                lv_obj_t *buttons = lv_obj_create(ammo_group);
                lv_obj_set_size(buttons, 92, 54); lv_obj_set_style_bg_opa(buttons, LV_OPA_0, 0);
                lv_obj_set_style_border_width(buttons, 0, 0); lv_obj_set_style_pad_all(buttons, 0, 0);
                lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW); lv_obj_set_style_pad_column(buttons, 4, 0);
                ammo_buttons[action / 2] = buttons;
            }
            lv_obj_t *ammo = lv_btn_create(ammo_buttons[action / 2]);
            lv_obj_set_size(ammo, 44, 52);
            lv_obj_add_style(ammo, &g_style_btn_secondary, 0);
            set_disabled_appearance(ammo);
            add_action(ammo, munition_cb, sid, ammo_actions[action].product,
                       ammo_actions[action].quantity);
            if (action == 0) refs->ammo12MinusButton = ammo;
            if (action == 2) refs->ammo20MinusButton = ammo;
            if (ammo_actions[action].quantity < 0 && ammo_actions[action].available <= 0)
                lv_obj_add_state(ammo, LV_STATE_DISABLED);
            lv_obj_t *al = lv_label_create(ammo);
            lv_label_set_text(al, ammo_actions[action].label);
            lv_obj_set_style_text_font(al, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(al, lv_color_hex(CLR_TEXT), 0);
            lv_obj_center(al);
        }

        // X (remove from today's list)
        lv_obj_t *remove_group = control_group("REMOVE", 78);
        lv_obj_t *btn_del = lv_btn_create(remove_group);
        lv_obj_set_size(btn_del, 76, 52);
        lv_obj_set_style_bg_color(btn_del, lv_color_hex(CLR_DANGER), 0);
        lv_obj_set_style_bg_opa(btn_del, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn_del, 8, 0);
        lv_obj_set_style_border_width(btn_del, 0, 0);
        lv_obj_add_event_cb(btn_del, remove_player_cb, LV_EVENT_CLICKED,
                            (void*)(intptr_t)sid);
        lv_obj_t *bdl = lv_label_create(btn_del);
        lv_label_set_text(bdl, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_font(bdl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(bdl, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(bdl);

        count++;
    }

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(s_player_list);
        lv_label_set_text(empty, "Keng SPILLER fir HAUT REGISTRIERT");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(CLR_MUTED), 0);
    }
}

lv_obj_t *screen_kredite_create(void)
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
    lv_label_set_text(title, LV_SYMBOL_CHARGE "  SPILLER VUM DAG");
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

    // Add player row
    lv_obj_t *add_row = lv_obj_create(s_scr);
    lv_obj_set_size(add_row, DISPLAY_LOGICAL_W - 40, 60);
    lv_obj_align(add_row, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_add_style(add_row, &g_style_card, 0);
    lv_obj_set_flex_flow(add_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(add_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(add_row, 12, 0);

    lv_obj_t *add_lbl = lv_label_create(add_row);
    lv_label_set_text(add_lbl, "SPILLER DOBAISETZEN:");
    lv_obj_set_style_text_font(add_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(add_lbl, lv_color_hex(CLR_TEXT), 0);

    s_dd_add = lv_dropdown_create(add_row);
    lv_obj_set_size(s_dd_add, 300, 44);
    lv_obj_set_style_text_font(s_dd_add, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(s_dd_add, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_text_color(s_dd_add, lv_color_hex(CLR_TEXT), 0);
    lv_dropdown_set_options(s_dd_add, "-");

    lv_obj_t *add_btn = lv_btn_create(add_row);
    lv_obj_add_style(add_btn, &g_style_btn_primary, 0);
    lv_obj_set_size(add_btn, 120, 44);
    lv_obj_add_event_cb(add_btn, add_player_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *abl = lv_label_create(add_btn);
    lv_label_set_text(abl, "+ DOBAISETZEN");
    lv_obj_set_style_text_font(abl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(abl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(abl);

    s_lbl_status = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_status, "");
    lv_obj_align(s_lbl_status, LV_ALIGN_TOP_MID, 0, 148);
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_14, 0);

    // Player list
    s_player_list = lv_obj_create(s_scr);
    lv_obj_set_size(s_player_list, DISPLAY_LOGICAL_W - 40,
                    DISPLAY_LOGICAL_H - 70 - 68 - 30 - 10);
    lv_obj_align(s_player_list, LV_ALIGN_TOP_MID, 0, 70 + 68 + 30);
    lv_obj_set_style_bg_color(s_player_list, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(s_player_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_player_list, 0, 0);
    lv_obj_set_style_pad_all(s_player_list, 0, 0);
    lv_obj_set_flex_flow(s_player_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_player_list, 8, 0);

    return s_scr;
}

void screen_kredite_refresh(void)
{
    if (!s_player_list) return;

    // Rebuild dropdown - heap-allocated: ~13 KB is too large for stack AND for
    // static BSS (static lands in internal DRAM, which is the scarce resource).
    // lv_dropdown_set_options() copies via lv_strdup(), so free() is safe after.
    const size_t opts_sz = MAX_PORTAL_SPIELER * (MAX_NAME_LEN + 1) + 4;
    char *opts = (char *)malloc(opts_sz);
    if (!opts) return;
    opts[0] = '\0'; strncat(opts, "-", opts_sz - 1);
    for (int i = 0; i < g_store.portalSpielerCount; i++) {
        strncat(opts, "\n", opts_sz - strlen(opts) - 1);
        strncat(opts, g_store.portalSpieler[i].name,
                opts_sz - strlen(opts) - 1);
    }
    if (s_dd_add) lv_dropdown_set_options(s_dd_add, opts);
    free(opts);

    build_player_list();
}
