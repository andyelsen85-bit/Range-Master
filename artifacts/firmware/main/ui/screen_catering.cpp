#include <stdio.h>
#include <string.h>
#include <time.h>
#include "lvgl.h"
#include "ui_fonts.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_catering.h"

#define CATERING_IDLE_MS 60000u
static lv_obj_t *s_scr, *s_players, *s_products, *s_checkout, *s_total, *s_status, *s_wifi_status, *s_pin_modal, *s_pin, *s_pin_kb;
static lv_obj_t *s_player_buttons[MAX_PORTAL_SPIELER];
static lv_obj_t *s_product_qty_labels[MAX_PRODUKTE];
static int s_player_id, s_qty[MAX_PRODUKTE];
static bool s_submitting, s_confirm_armed;
static uint32_t s_last_touch;
static uint32_t s_content_signature;
static char s_rendered_wifi_status[32];
static void reset_basket(void);
static void rebuild(void);

static void refresh_wifi_status(void) {
    if (!s_wifi_status) return;
    char text[sizeof(s_rendered_wifi_status)];
    if (g_store.wifiConnected) {
        if (g_store.wifiIp[0])
            snprintf(text, sizeof(text), LV_SYMBOL_WIFI "  %s", g_store.wifiIp);
        else
            snprintf(text, sizeof(text), "WIFI: VERBUNDEN");
    } else {
        snprintf(text, sizeof(text), "WIFI: NICHT VERBUNDEN");
    }
    if (!strcmp(text, s_rendered_wifi_status)) return;
    lv_label_set_text(s_wifi_status, text);
    lv_obj_set_style_text_color(s_wifi_status,
        lv_color_hex(g_store.wifiConnected ? CLR_SUCCESS : CLR_MUTED), 0);
    snprintf(s_rendered_wifi_status, sizeof(s_rendered_wifi_status), "%s", text);
}

static uint32_t content_signature(void) {
    uint32_t hash = 2166136261u;
    auto add = [&](uint32_t value) { hash ^= value; hash *= 16777619u; };
    auto add_text = [&](const char *text) {
        if (!text) { add(0); return; }
        while (*text) add((uint8_t)*text++);
        add(0xffu);
    };
    time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
    char today[11]; strftime(today, sizeof(today), "%Y-%m-%d", &tm);
    add_text(today);
    add_text(g_store.kreditDatum);
    for (int i = 0; i < MAX_PORTAL_SPIELER; ++i)
        add((uint32_t)g_store.kreditPlayerIds[i]);
    add((uint32_t)g_store.portalSpielerCount);
    for (int i = 0; i < g_store.portalSpielerCount; ++i) {
        add((uint32_t)g_store.portalSpieler[i].id);
        add(g_store.portalSpieler[i].portalAktiv ? 1u : 0u);
        add_text(g_store.portalSpieler[i].name);
    }
    add((uint32_t)g_store.produkteCount);
    for (int i = 0; i < g_store.produkteCount; ++i) {
        add((uint32_t)g_store.produkte[i].id);
        add((uint32_t)g_store.produkte[i].preisRevisionId);
        add((uint32_t)g_store.produkte[i].preisCent);
        add(g_store.produkte[i].active ? 1u : 0u);
        add_text(g_store.produkte[i].name);
        add_text(g_store.produkte[i].category);
    }
    return hash;
}
static bool ensure_current_content(void) {
    if (content_signature() == s_content_signature) return true;
    reset_basket();
    rebuild();
    lv_label_set_text(s_status,
        "SYNC HAT DIE LISTE GEÄNDERT. BITTE NEU WÄHLEN.");
    return false;
}

static bool player_today(const PortalSpieler *p) {
    return p && store_spieler_fuer_tag_aktiv(p->id);
}
static void activity(void) { s_last_touch = lv_tick_get(); }
static void reset_basket(void) {
    s_player_id = 0; memset(s_qty, 0, sizeof(s_qty)); s_submitting = s_confirm_armed = false; activity();
}
static void refresh_total(void) {
    int cents = 0, count = 0;
    for (int i = 0; i < g_store.produkteCount; ++i) {
        cents += s_qty[i] * g_store.produkte[i].preisCent; count += s_qty[i];
    }
    char text[80]; snprintf(text, sizeof(text), "GESAMT: %d.%02d EUR  (%d ARTIKEL)",
                             cents / 100, cents % 100, count);
    lv_label_set_text(s_total, text);
}
static void refresh_selection(void) {
    for (int i = 0; i < g_store.portalSpielerCount; ++i) {
        if (!s_player_buttons[i]) continue;
        lv_obj_set_style_bg_color(s_player_buttons[i],
            g_store.portalSpieler[i].id == s_player_id
                ? lv_color_hex(CLR_PRIMARY) : lv_color_hex(CLR_BORDER), 0);
    }
    for (int i = 0; i < g_store.produkteCount; ++i) {
        if (!s_product_qty_labels[i]) continue;
        char amount[8]; snprintf(amount, sizeof(amount), "%d", s_qty[i]);
        lv_label_set_text(s_product_qty_labels[i], amount);
    }
    refresh_total();
}
static void product_cb(lv_event_t *e) {
    if (!ensure_current_content()) return;
    intptr_t packed = (intptr_t)lv_event_get_user_data(e);
    int product_id = (int)(packed >> 1), i = -1;
    for (int candidate = 0; candidate < g_store.produkteCount; ++candidate)
        if (g_store.produkte[candidate].id == product_id) {
            i = candidate;
            break;
        }
    if (!s_player_id || s_submitting || i < 0 || i >= g_store.produkteCount) return;
    if ((packed & 1) && s_qty[i] < 99) s_qty[i]++;
    else if (!(packed & 1) && s_qty[i] > 0) s_qty[i]--;
    s_confirm_armed = false; activity(); refresh_selection();
}
static void player_cb(lv_event_t *e) {
    if (!ensure_current_content()) return;
    s_player_id = (int)(intptr_t)lv_event_get_user_data(e);
    memset(s_qty, 0, sizeof(s_qty)); s_confirm_armed = false; activity();
    refresh_selection();
}
static void confirm_cb(lv_event_t *) {
    if (!s_player_id || s_submitting) return;
    if (!ensure_current_content()) return;
    int ids[MAX_PRODUKTE], qty[MAX_PRODUKTE], n = 0;
    for (int i = 0; i < g_store.produkteCount; ++i) if (s_qty[i]) {
        ids[n] = g_store.produkte[i].id; qty[n++] = s_qty[i];
    }
    if (!n) { lv_label_set_text(s_status, "MINDESTENS EINEN ARTIKEL WÄHLEN."); return; }
    if (!s_confirm_armed) {
        s_confirm_armed = true;
        char summary[192] = "ZUSAMMENFASSUNG: ";
        size_t used = strlen(summary);
        for (int i = 0; i < g_store.produkteCount && used + 8 < sizeof(summary); ++i) {
            if (!s_qty[i]) continue;
            int written = snprintf(summary + used, sizeof(summary) - used, "%s%d x %.20s",
                                    used == strlen("ZUSAMMENFASSUNG: ") ? "" : ", ",
                                   s_qty[i], g_store.produkte[i].name);
            if (written < 0 || (size_t)written >= sizeof(summary) - used) break;
            used += (size_t)written;
        }
        snprintf(summary + used, sizeof(summary) - used, "\nERNEUT BESTÄTIGEN.");
        lv_label_set_text(s_status, summary);
        return;
    }
    s_submitting = true; // prevents a second touch before store persistence returns
    bool ok = store_queue_catering_basket(s_player_id, ids, qty, n);
    if (ok) {
        lv_label_set_text(s_status, "VERKAUF GESPEICHERT.");
    } else {
        char message[160];
        snprintf(message, sizeof(message), "VERKAUF NICHT GESPEICHERT: %s",
                 store_last_catering_error());
        lv_label_set_text(s_status, message);
        // Keep the basket available for a retry after a transient persistence
        // failure; only the in-flight guard must be released.
        s_submitting = false;
    }
    if (ok) {
        reset_basket();
        refresh_selection();
    }
}
static void exit_pin_cb(lv_event_t *) {
    CateringPinVerifyResult result = store_verify_catering_pin(lv_textarea_get_text(s_pin));
    if (result == CATERING_PIN_OK) {
        if (!store_set_operating_mode(TERMINAL_MODE_NORMAL)) {
            lv_textarea_set_text(s_pin, "");
            lv_label_set_text(s_status, "NORMALMODUS NICHT GESPEICHERT.");
            return;
        }
        lv_obj_del(s_pin_modal); s_pin_modal = s_pin = s_pin_kb = NULL;
        ui_manager_show(SCREEN_DASHBOARD);
        return;
    }
    lv_textarea_set_text(s_pin, "");
    if (result == CATERING_PIN_LOCKED) {
        uint32_t remaining = store_catering_pin_lockout_remaining();
        char message[64];
        if (remaining) snprintf(message, sizeof(message), "ZU VIELE FALSCHE PIN. WARTEN %lu SEKUNDEN.",
                                (unsigned long)remaining);
        else snprintf(message, sizeof(message), "ZU VIELE FALSCHE PIN. KORREKTE PIN ERFORDERLICH.");
        lv_label_set_text(s_status, message);
    } else lv_label_set_text(s_status, "FALSCHE PIN.");
}
static void close_pin_cb(lv_event_t *) {
    if (s_pin_modal) lv_obj_del(s_pin_modal);
    s_pin_modal = s_pin = s_pin_kb = NULL;
}
static void exit_open_cb(lv_event_t *) {
    if (s_pin_modal) return;
    s_pin_modal = lv_obj_create(s_scr); lv_obj_set_size(s_pin_modal, 540, 500);
    lv_obj_center(s_pin_modal); lv_obj_add_style(s_pin_modal, &g_style_card, 0);
    lv_obj_set_flex_flow(s_pin_modal, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_pin_modal, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *l = lv_label_create(s_pin_modal); lv_label_set_text(l, "CATERING VERLASSEN - PIN");
    s_pin = lv_textarea_create(s_pin_modal); lv_obj_set_width(s_pin, 360);
    lv_textarea_set_one_line(s_pin, true); lv_textarea_set_password_mode(s_pin, true);
    lv_textarea_set_accepted_chars(s_pin, "0123456789"); lv_textarea_set_max_length(s_pin, 16);
    lv_obj_t *actions = lv_obj_create(s_pin_modal); lv_obj_set_size(actions, LV_PCT(100), 50);
    lv_obj_set_style_bg_opa(actions, LV_OPA_0, 0); lv_obj_set_style_border_width(actions, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *b = lv_btn_create(actions); lv_obj_add_style(b, &g_style_btn_primary, 0);
    lv_obj_add_event_cb(b, exit_pin_cb, LV_EVENT_CLICKED, NULL);
    l = lv_label_create(b); lv_label_set_text(l, "PIN PRÜFEN"); lv_obj_center(l);
    b = lv_btn_create(actions); lv_obj_add_style(b, &g_style_btn_secondary, 0);
    lv_obj_add_event_cb(b, close_pin_cb, LV_EVENT_CLICKED, NULL);
    l = lv_label_create(b); lv_label_set_text(l, "ABBRECHEN"); lv_obj_center(l);
    s_pin_kb = lv_keyboard_create(s_pin_modal);
    lv_obj_set_size(s_pin_kb, 480, 190); lv_keyboard_set_mode(s_pin_kb, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(s_pin_kb, s_pin);
    lv_obj_add_event_cb(s_pin_kb, [](lv_event_t *e) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_ENTER) exit_pin_cb(e);
    }, LV_EVENT_KEY, NULL);
}
static lv_obj_t *button(lv_obj_t *p, const char *text, lv_style_t *style) {
    lv_obj_t *b = lv_btn_create(p); lv_obj_add_style(b, style, 0);
    lv_obj_t *l = lv_label_create(b); lv_label_set_text(l, text);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_font(l, UI_FONT_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(l); return b;
}
static void rebuild(void) {
    if (!s_players) return;
    lv_obj_clean(s_players); lv_obj_clean(s_products);
    memset(s_player_buttons, 0, sizeof(s_player_buttons));
    memset(s_product_qty_labels, 0, sizeof(s_product_qty_labels));
    lv_obj_t *heading = lv_label_create(s_players);
    lv_label_set_text(heading, "SPIELER DES TAGES");
    lv_obj_set_style_text_font(heading, UI_FONT_16, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(CLR_MUTED), 0);
    for (int i = 0; i < g_store.portalSpielerCount; ++i) if (player_today(&g_store.portalSpieler[i])) {
        lv_obj_t *b = button(s_players, g_store.portalSpieler[i].name,
                             g_store.portalSpieler[i].id == s_player_id ? &g_style_btn_primary : &g_style_btn_secondary);
        s_player_buttons[i] = b;
        lv_obj_set_width(b, LV_PCT(100)); lv_obj_add_event_cb(b, player_cb, LV_EVENT_CLICKED,
                       (void *)(intptr_t)g_store.portalSpieler[i].id);
    }
    heading = lv_label_create(s_products);
    lv_label_set_text(heading, "ESSEN & GETRÄNKE");
    lv_obj_set_style_text_font(heading, UI_FONT_16, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(CLR_MUTED), 0);
    for (int i = 0; i < g_store.produkteCount; ++i) {
        Produkt *p = &g_store.produkte[i];
        if (!p->active || (strcmp(p->category, "FOOD") && strcmp(p->category, "DRINK"))) continue;
        lv_obj_t *row = lv_obj_create(s_products); lv_obj_set_size(row, LV_PCT(100), 56);
        lv_obj_set_style_bg_opa(row, LV_OPA_0, 0); lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW); lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        char name[64]; snprintf(name, sizeof(name), "%s  %d.%02d", p->name, p->preisCent / 100, p->preisCent % 100);
        lv_obj_t *l = lv_label_create(row); lv_label_set_text(l, name);
        lv_obj_set_style_text_font(l, UI_FONT_14, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(CLR_TEXT), 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, 0);
        lv_obj_set_flex_grow(l, 1);
        lv_obj_t *minus = button(row, "-", &g_style_btn_secondary); lv_obj_set_size(minus, 50, 44);
        lv_obj_add_event_cb(minus, product_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)((((intptr_t)p->id) << 1)));
        char amount[8]; snprintf(amount, sizeof(amount), "%d", s_qty[i]);
        l = lv_label_create(row); lv_label_set_text(l, amount);
        s_product_qty_labels[i] = l;
        lv_obj_set_style_text_color(l, lv_color_hex(CLR_TEXT), 0);
        lv_obj_t *plus = button(row, "+", &g_style_btn_primary); lv_obj_set_size(plus, 50, 44);
        lv_obj_add_event_cb(plus, product_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)((((intptr_t)p->id) << 1) | 1));
    }
    refresh_total();
    s_content_signature = content_signature();
}
lv_obj_t *screen_catering_create(void) {
    s_scr = lv_obj_create(NULL); lv_obj_set_size(s_scr, DISPLAY_LOGICAL_W, DISPLAY_LOGICAL_H); screen_base_init(s_scr);
    lv_obj_set_style_text_color(s_scr, lv_color_hex(CLR_TEXT), 0);
    lv_obj_t *title = lv_label_create(s_scr); lv_label_set_text(title, "CATERING"); lv_obj_add_style(title, &g_style_label_title, 0); lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 18);
    s_wifi_status = lv_label_create(s_scr); lv_obj_set_style_text_font(s_wifi_status, UI_FONT_14, 0);
    lv_obj_align(s_wifi_status, LV_ALIGN_TOP_RIGHT, -285, 34);
    s_rendered_wifi_status[0] = '\0'; refresh_wifi_status();
    lv_obj_t *exit = button(s_scr, "CATERING VERLASSEN", &g_style_btn_danger);
    lv_obj_set_size(exit, 260, 52); lv_obj_align(exit, LV_ALIGN_TOP_RIGHT, -18, 16);
    lv_obj_add_event_cb(exit, exit_open_cb, LV_EVENT_CLICKED, NULL);

    s_players = lv_obj_create(s_scr); lv_obj_set_size(s_players, 290, 670); lv_obj_align(s_players, LV_ALIGN_TOP_LEFT, 18, 92); lv_obj_add_style(s_players, &g_style_card, 0); lv_obj_set_flex_flow(s_players, LV_FLEX_FLOW_COLUMN);
    s_products = lv_obj_create(s_scr); lv_obj_set_size(s_products, 620, 670); lv_obj_align(s_products, LV_ALIGN_TOP_LEFT, 326, 92); lv_obj_add_style(s_products, &g_style_card, 0); lv_obj_set_flex_flow(s_products, LV_FLEX_FLOW_COLUMN);
    s_checkout = lv_obj_create(s_scr); lv_obj_set_size(s_checkout, 298, 670); lv_obj_align(s_checkout, LV_ALIGN_TOP_LEFT, 964, 92); lv_obj_add_style(s_checkout, &g_style_card, 0);
    lv_obj_t *checkout_heading = lv_label_create(s_checkout); lv_label_set_text(checkout_heading, "BESTELLUNG");
    lv_obj_set_style_text_font(checkout_heading, UI_FONT_16, 0);
    lv_obj_set_style_text_color(checkout_heading, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(checkout_heading, LV_ALIGN_TOP_LEFT, 0, 0);
    s_total = lv_label_create(s_checkout); lv_obj_align(s_total, LV_ALIGN_TOP_LEFT, 0, 42); lv_obj_set_style_text_font(s_total, UI_FONT_20, 0);
    lv_obj_set_style_text_color(s_total, lv_color_hex(CLR_TEXT), 0);
    s_status = lv_label_create(s_checkout); lv_obj_set_width(s_status, LV_PCT(100)); lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 0, 82);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(s_status, 110);
    lv_obj_set_style_text_color(s_status, lv_color_hex(CLR_TEXT), 0);
    lv_obj_t *confirm = button(s_checkout, "VERKAUF BESTÄTIGEN", &g_style_btn_primary); lv_obj_set_size(confirm, LV_PCT(100), 70); lv_obj_align(confirm, LV_ALIGN_TOP_LEFT, 0, 210); lv_obj_add_event_cb(confirm, confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cancel = button(s_checkout, "ABBRECHEN", &g_style_btn_secondary); lv_obj_set_size(cancel, LV_PCT(100), 55); lv_obj_align(cancel, LV_ALIGN_TOP_LEFT, 0, 296); lv_obj_add_event_cb(cancel, [](lv_event_t *) { reset_basket(); refresh_selection(); }, LV_EVENT_CLICKED, NULL);
    reset_basket(); rebuild(); return s_scr;
}
void screen_catering_refresh(void) { reset_basket(); rebuild(); }
void screen_catering_tick(void) {
    refresh_wifi_status();
    uint32_t current_signature = content_signature();
    if (current_signature != s_content_signature) {
        reset_basket();
        rebuild();
        return;
    }
    if (s_pin_modal) {
        uint32_t remaining = store_catering_pin_lockout_remaining();
        if (remaining) {
            char message[64];
            snprintf(message, sizeof(message), "ZU VIELE FALSCHE PIN. WARTEN %lu SEKUNDEN.",
                     (unsigned long)remaining);
            lv_label_set_text(s_status, message);
        }
    }
    if (!s_pin_modal && lv_tick_elaps(s_last_touch) > CATERING_IDLE_MS && (s_player_id || s_submitting)) { reset_basket(); refresh_selection(); }
}