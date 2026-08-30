// ============================================================
// ASTELLUNGEN screen - settings: connections, machines, custom seqs,
//                     WiFi, products/bills, catering, system info
// Mirrors EinstellungenScreen.tsx
// ============================================================
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lvgl.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_einstellungen.h"
#include "click_sound.h"
#include "lora_stub.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_tab_view;
static lv_obj_t *s_kb;

// ── Tab: Conn ─────────────────────────────────────────────────
static lv_obj_t *s_ta_url;
static lv_obj_t *s_ta_key;
static lv_obj_t *s_ta_gateway;
static lv_obj_t *s_ta_gateway_token;
static lv_obj_t *s_ta_price_credit;
static lv_obj_t *s_ta_price_cal12;
static lv_obj_t *s_ta_price_cal20;
static lv_obj_t *s_lbl_api_status;
static bool s_gateway_check_pending;
static lv_obj_t *s_lbl_machine_test_status;
static bool s_machine_test_pending;
static lv_obj_t *s_auto_sync_switch;
static lv_obj_t *s_auto_sync_seconds;
static lv_obj_t *s_billing_sync_seconds;
static lv_obj_t *s_auto_sync_feedback;
static lv_obj_t *s_config_backup_status;
static lv_obj_t *s_catering_pin;
static lv_obj_t *s_catering_old_pin;
static lv_obj_t *s_catering_status;
static lv_obj_t *s_bill_day_summary;

static void set_label_text_if_changed(lv_obj_t *label, const char *text)
{
    if (!label || !text) return;
    const char *current = lv_label_get_text(label);
    if (!current || strcmp(current, text) != 0) lv_label_set_text(label, text);
}

static void set_textarea_text_if_changed(lv_obj_t *textarea, const char *text)
{
    if (!textarea || !text) return;
    const char *current = lv_textarea_get_text(textarea);
    if (!current || strcmp(current, text) != 0) lv_textarea_set_text(textarea, text);
}

static void refresh_bill_day_summary(void)
{
    if (!s_bill_day_summary) return;
    const BillDaySummary *day = &g_store.billDay;
    char text[1024];
    if (!day->datum[0]) {
        snprintf(text, sizeof(text), "DAGESSUMMARY: NACH KENG PORTAL-DATEN");
    } else {
        snprintf(text, sizeof(text),
                 "DAGESSUMMARY %s%s\n"
                 "SPILLER: %d  |  BEZUELT: %d\n"
                 "SPILLER: %d  |  OFGESCHLOSS: %d  |  CONFIRMED CLAYS: %d\n"
                 "GENERAL TOTAL: %d.%02d EUR",
                 day->datum, day->authoritative ? "" : " (OFFLINE CACHE)",
                 day->uniquePlayers, day->paidPlayers, day->games,
                 day->completedGames, day->confirmedClays,
                 day->generalTotalCent / 100, abs(day->generalTotalCent % 100));
        size_t used = strlen(text);
        for (int i = 0; i < day->categoryCount && used < sizeof(text); ++i) {
            int n = snprintf(text + used, sizeof(text) - used, "\n%s: %d.%02d EUR",
                             day->categories[i].name,
                             day->categories[i].totalCent / 100,
                             abs(day->categories[i].totalCent % 100));
            if (n < 0 || (size_t)n >= sizeof(text) - used) break;
            used += (size_t)n;
        }
        for (int i = 0; i < day->productCount && used < sizeof(text); ++i) {
            const BillLine *line = &day->products[i];
            int n;
            if (line->unitPriceCent == VERKAUF_UNIT_PRICE_UNKNOWN)
                n = snprintf(text + used, sizeof(text) - used,
                             "\nPENDING %s: %d x PRAIS ONBEKANNT (RECONCILIATION)",
                             line->produktName, line->quantity);
            else
                n = snprintf(text + used, sizeof(text) - used,
                             "\n%s%s: %d x %d.%02d = %d.%02d EUR",
                             line->localPending ? "PENDING " : "",
                             line->produktName, line->quantity,
                             line->unitPriceCent / 100,
                             abs(line->unitPriceCent % 100),
                             line->lineTotalCent / 100,
                             abs(line->lineTotalCent % 100));
            if (n < 0 || (size_t)n >= sizeof(text) - used) break;
            used += (size_t)n;
        }
        if (day->productOverflow && used < sizeof(text)) {
            snprintf(text + used, sizeof(text) - used,
                     "\nDETAIL-LIMIT ERREECHT - PORTALDETAILER PRUEWEN");
        }
    }
    set_label_text_if_changed(s_bill_day_summary, text);
}

static void catering_save_pin_cb(lv_event_t *)
{
    if (store_catering_pin_configured() &&
        store_verify_catering_pin(lv_textarea_get_text(s_catering_old_pin)) != CATERING_PIN_OK) {
        lv_textarea_set_text(s_catering_old_pin, "");
        lv_label_set_text(s_catering_status, "ALE PIN ASS NET KORREKT.");
        lv_obj_set_style_text_color(s_catering_status, lv_color_hex(CLR_DANGER), 0);
        return;
    }
    if (store_set_catering_pin(lv_textarea_get_text(s_catering_pin))) {
        lv_textarea_set_text(s_catering_pin, "");
        lv_textarea_set_text(s_catering_old_pin, "");
        lv_label_set_text(s_catering_status, "CATERING PIN GESPEICHERT.");
        lv_obj_set_style_text_color(s_catering_status, lv_color_hex(CLR_SUCCESS), 0);
    } else {
        lv_label_set_text(s_catering_status, "PIN MUSS 4 BIS 16 ZIFFEREN HUN.");
        lv_obj_set_style_text_color(s_catering_status, lv_color_hex(CLR_DANGER), 0);
    }
}
static void catering_enter_cb(lv_event_t *)
{
    if (!store_set_operating_mode(TERMINAL_MODE_CATERING)) {
        lv_label_set_text(s_catering_status, "FIR D'RAUSCHT E PIN KONFIGUREIEREN.");
        lv_obj_set_style_text_color(s_catering_status, lv_color_hex(CLR_DANGER), 0);
        return;
    }
    ui_manager_show(SCREEN_CATERING);
}

static void save_api_settings(void)
{
    const char *url = lv_textarea_get_text(s_ta_url);
    const char *key = lv_textarea_get_text(s_ta_key);
    const char *gateway = lv_textarea_get_text(s_ta_gateway);
    const char *gateway_token = lv_textarea_get_text(s_ta_gateway_token);
    strncpy(g_store.apiUrl, url, MAX_URL_LEN - 1); g_store.apiUrl[MAX_URL_LEN - 1] = '\0';
    strncpy(g_store.apiKey, key, MAX_KEY_LEN - 1); g_store.apiKey[MAX_KEY_LEN - 1] = '\0';
    strncpy(g_store.gatewayUrl, gateway, MAX_URL_LEN - 1); g_store.gatewayUrl[MAX_URL_LEN - 1] = '\0';
    strncpy(g_store.gatewayToken, gateway_token, MAX_KEY_LEN - 1); g_store.gatewayToken[MAX_KEY_LEN - 1] = '\0';
    game_store_save();
}

static void set_api_status(const char *text, uint32_t color)
{
    if (!s_lbl_api_status) return;
    const char *current = lv_label_get_text(s_lbl_api_status);
    if (current && strcmp(current, text) == 0) return;
    lv_label_set_text(s_lbl_api_status, text);
    lv_obj_set_style_text_color(s_lbl_api_status, lv_color_hex(color), 0);
}

static void save_api_cb(lv_event_t *e)
{
    save_api_settings();
    lv_label_set_text(s_lbl_api_status, LV_SYMBOL_OK " GESPEICHERT");
    lv_obj_set_style_text_color(s_lbl_api_status, lv_color_hex(CLR_SUCCESS), 0);
}

static void gateway_test_cb(lv_event_t *e)
{
    // Test the values currently visible to the operator, not stale saved data.
    save_api_settings();
    if (lora_gateway_check()) {
        s_gateway_check_pending = true;
        set_api_status("GATEWAY GËTT GEPRÉIFT...", CLR_WARN);
    } else {
        char status[96];
        lora_copy_status_text(status, sizeof(status));
        set_api_status(status, CLR_DANGER);
    }
}

static void config_backup_cb(lv_event_t *e)
{
    (void)e;
    save_api_settings();
    if (store_sync()) {
        if (s_config_backup_status)
            lv_label_set_text(s_config_backup_status,
                              "SYNC + BACKUP GËTT AM HANNERGROND GESTART...");
    } else if (s_config_backup_status) {
        lv_label_set_text(s_config_backup_status,
                          "BACKUP NET GESTART: SYNC ASS SCHONN AKTIV");
    }
}

static lv_obj_t *build_api_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(parent, 12, 0);
    lv_obj_set_style_pad_all(parent, 16, 0);

    lv_obj_t *section = lv_label_create(parent);
    lv_label_set_text(section, "CONNECTION");
    lv_obj_set_style_text_font(section, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(section, lv_color_hex(CLR_PRIMARY), 0);

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

    lv_obj_t *gateway_lbl = lv_label_create(parent);
    lv_label_set_text(gateway_lbl, "TRAPMASTER GATEWAY URL");
    lv_obj_set_style_text_font(gateway_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gateway_lbl, lv_color_hex(CLR_TEXT), 0);

    s_ta_gateway = lv_textarea_create(parent);
    lv_obj_set_size(s_ta_gateway, LV_PCT(100), 50);
    lv_textarea_set_text(s_ta_gateway, g_store.gatewayUrl);
    lv_textarea_set_one_line(s_ta_gateway, true);
    lv_obj_set_style_text_font(s_ta_gateway, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(s_ta_gateway, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_text_color(s_ta_gateway, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *gateway_token_lbl = lv_label_create(parent);
    lv_label_set_text(gateway_token_lbl, "TRAPMASTER GATEWAY AUTH KEY");
    lv_obj_set_style_text_font(gateway_token_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(gateway_token_lbl, lv_color_hex(CLR_TEXT), 0);

    s_ta_gateway_token = lv_textarea_create(parent);
    lv_obj_set_size(s_ta_gateway_token, LV_PCT(100), 50);
    lv_textarea_set_text(s_ta_gateway_token, g_store.gatewayToken);
    lv_textarea_set_one_line(s_ta_gateway_token, true);
    lv_textarea_set_password_mode(s_ta_gateway_token, true);
    lv_obj_set_style_text_font(s_ta_gateway_token, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(s_ta_gateway_token, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_text_color(s_ta_gateway_token, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *action_row = lv_obj_create(parent);
    lv_obj_set_size(action_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(action_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(action_row, 0, 0);
    lv_obj_set_style_pad_all(action_row, 0, 0);
    lv_obj_set_style_pad_column(action_row, 12, 0);
    lv_obj_set_flex_flow(action_row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(action_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *save_btn = lv_btn_create(action_row);
    lv_obj_add_style(save_btn, &g_style_btn_primary, 0);
    lv_obj_set_size(save_btn, 180, 44);
    lv_obj_add_event_cb(save_btn, save_api_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sl = lv_label_create(save_btn);
    lv_label_set_text(sl, LV_SYMBOL_SAVE " SPEICHERN");
    lv_obj_set_style_text_color(sl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(sl);

    lv_obj_t *test_btn = lv_btn_create(action_row);
    lv_obj_add_style(test_btn, &g_style_btn_secondary, 0);
    lv_obj_set_size(test_btn, 240, 44);
    lv_obj_add_event_cb(test_btn, gateway_test_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *tl = lv_label_create(test_btn);
    lv_label_set_text(tl, LV_SYMBOL_OK " GATEWAY TESTEN");
    lv_obj_set_style_text_color(tl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(tl);

    s_lbl_api_status = lv_label_create(parent);
    lv_label_set_text(s_lbl_api_status, "");
    lv_obj_set_style_text_font(s_lbl_api_status, &lv_font_montserrat_14, 0);

    lv_obj_t *backup_btn = lv_btn_create(parent);
    lv_obj_add_style(backup_btn, &g_style_btn_secondary, 0);
    lv_obj_set_size(backup_btn, 260, 44);
    lv_obj_add_event_cb(backup_btn, config_backup_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *backup_label = lv_label_create(backup_btn);
    lv_label_set_text(backup_label, LV_SYMBOL_UPLOAD " BACKUP ELO");
    lv_obj_set_style_text_color(backup_label, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(backup_label);

    s_config_backup_status = lv_label_create(parent);
    char backup_status[192];
    snprintf(backup_status, sizeof(backup_status), "%s%s%s",
             g_store.configBackupStatus[0] ? g_store.configBackupStatus
                                           : "NACH KEE BACKUP",
             g_store.lastConfigBackupAt[0] ? "\nLESCHT BACKUP: " : "",
             g_store.lastConfigBackupAt);
    lv_label_set_text(s_config_backup_status, backup_status);
    lv_obj_set_style_text_font(s_config_backup_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_config_backup_status, lv_color_hex(CLR_MUTED), 0);

    return parent;
}

// ── Connection section: MASCHINNEN ────────────────────────────
static lv_obj_t *s_mach_sw[MASCHINE_COUNT];

static void mach_sw_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *sw = lv_event_get_target_obj(e);
    g_store.maschinenAktiv[idx] = lv_obj_has_state(sw, LV_STATE_CHECKED);
    game_store_save();
}

static void machine_test_cb(lv_event_t *e)
{
    int machine = (int)(intptr_t)lv_event_get_user_data(e);
    if (machine < MASCHINE_A || machine >= MASCHINE_COUNT) return;

    if (lora_fire_machine((Maschine)machine)) {
        s_machine_test_pending = true;
        if (s_lbl_machine_test_status) {
            char msg[64];
            snprintf(msg, sizeof(msg), "TEST MASCHINN %c GESCHÉCKT...",
                     (char)('A' + machine));
            lv_label_set_text(s_lbl_machine_test_status, msg);
            lv_obj_set_style_text_color(s_lbl_machine_test_status,
                                        lv_color_hex(CLR_WARN), 0);
        }
    } else if (s_lbl_machine_test_status) {
        char status[96];
        lora_copy_status_text(status, sizeof(status));
        lv_label_set_text(s_lbl_machine_test_status, status);
        lv_obj_set_style_text_color(s_lbl_machine_test_status,
                                    lv_color_hex(CLR_DANGER), 0);
    }
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

    lv_obj_t *section = lv_label_create(parent);
    lv_label_set_text(section, "MASCHINN CONFIGURATION");
    lv_obj_set_style_text_font(section, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(section, lv_color_hex(CLR_PRIMARY), 0);

    for (int m = 0; m < MASCHINE_COUNT; m++) {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_set_size(row, LV_PCT(100), 50);
        lv_obj_add_style(row, &g_style_card, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, labels[m]);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_TEXT), 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, 0);
        lv_obj_set_flex_grow(lbl, 1);

        lv_obj_t *test = lv_btn_create(row);
        lv_obj_add_style(test, &g_style_btn_secondary, 0);
        lv_obj_set_size(test, 150, 38);
        lv_obj_set_style_pad_hor(test, 8, 0);
        lv_obj_add_event_cb(test, machine_test_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)m);
        lv_obj_t *test_label = lv_label_create(test);
        lv_label_set_text(test_label, LV_SYMBOL_PLAY " TEST FIRE");
        lv_obj_set_style_text_font(test_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(test_label, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(test_label);

        lv_obj_t *sw = lv_switch_create(row);
        s_mach_sw[m] = sw;
        if (g_store.maschinenAktiv[m]) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, mach_sw_cb, LV_EVENT_VALUE_CHANGED,
                            (void*)(intptr_t)m);
    }

    s_lbl_machine_test_status = lv_label_create(parent);
    lv_label_set_text(s_lbl_machine_test_status,
                      "TEST FIRE löst genau einen Schuss aus — kein Spiel.");
    lv_obj_set_style_text_font(s_lbl_machine_test_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_lbl_machine_test_status, lv_color_hex(CLR_MUTED), 0);
    return parent;
}

// ── System section: CUSTOM SEQUENZEN ──────────────────────────
// Ordered sequence builder: A-G may be added as single machines. A-G
// doublettes require a first machine, a partner, and a seconds delay; H is a
// special one-relay H1/H2 doublette with no partner selector.
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
static lv_obj_t *s_pair_delay_ta[4];
static lv_obj_t *s_pair_status[4];
static int s_pending_pair_first[4] = {-1, -1, -1, -1};

// ── Helpers ───────────────────────────────────────────────────
static void refresh_custom_stats(int ci)
{
    if (!s_stat_tauben[ci]) return;
    int tauben = 0;
    for (int i = 0; i < g_store.customSequenzLen[ci]; i++) {
        CustomSequenzEintrag *entry = &g_store.customSequenzen[ci][i];
        tauben += (entry->maschine == MASCHINE_H || entry->isDoublette) ? 2 : 1;
    }
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
        CustomSequenzEintrag *entry = &g_store.customSequenzen[ci][i];
        Maschine m  = entry->maschine;
        bool    isH = (m == MASCHINE_H);
        lv_obj_t *badge = lv_btn_create(cont);
        lv_obj_set_size(badge, entry->isDoublette ? 86 : 46, 48);
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
        char badge_text[16];
        if (m == MASCHINE_H) {
            snprintf(badge_text, sizeof(badge_text), "H1/H2");
        } else if (entry->isDoublette) {
            snprintf(badge_text, sizeof(badge_text), "%s+%s\n%u.%us",
                     MACH_LBL[(int)m], MACH_LBL[(int)entry->partner],
                     (unsigned)(entry->delayMs / 1000),
                     (unsigned)((entry->delayMs % 1000) / 100));
        } else {
            snprintf(badge_text, sizeof(badge_text), "%s", MACH_LBL[(int)m]);
        }
        lv_label_set_text(lbl, badge_text);
        lv_obj_set_style_text_font(lbl,
            entry->isDoublette && !isH ? &lv_font_montserrat_12 : &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(CLR_TEXT), 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
    }
}

static uint16_t read_pair_delay_ms(int ci)
{
    if (!s_pair_delay_ta[ci]) return 1000;
    const char *text = lv_textarea_get_text(s_pair_delay_ta[ci]);
    char *end = NULL;
    float seconds = strtof(text, &end);
    if (end == text || *end != '\0' || seconds < 0.0f) return 1000;
    if (seconds > 10.0f) seconds = 10.0f;
    return (uint16_t)(seconds * 1000.0f + 0.5f);
}

static void set_pair_status(int ci, const char *text)
{
    if (s_pair_status[ci]) lv_label_set_text(s_pair_status[ci], text);
}

static void add_custom_entry(int ci, CustomSequenzEintrag entry)
{
    if (g_store.customSequenzLen[ci] >= CUSTOM_SEQ_MAX) {
        set_pair_status(ci, "MAXIMAL 16 EINTRAEG");
        return;
    }
    g_store.customSequenzen[ci][g_store.customSequenzLen[ci]++] = entry;
    game_store_save();
    refresh_custom_seq(ci);
    refresh_custom_stats(ci);
}

// ── Build ─────────────────────────────────────────────────────
static lv_obj_t *build_custom_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 14, 0);
    lv_obj_set_style_pad_row(parent, 12, 0);

    lv_obj_t *section = lv_label_create(parent);
    lv_label_set_text(section, "CUSTOM GAME CONFIGURATION");
    lv_obj_set_style_text_font(section, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(section, lv_color_hex(CLR_PRIMARY), 0);

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
        lv_label_set_long_mode(cname, LV_LABEL_LONG_DOT);
        lv_obj_set_width(cname, 0);
        lv_obj_set_flex_grow(cname, 1);

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

        // ── Section label: single A-G entries ─────────────────
        lv_obj_t *add_hdr = lv_label_create(card);
        lv_label_set_text(add_hdr, "EENZEL SCHANZ DOBAISETZEN");
        lv_obj_set_style_text_font(add_hdr, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(add_hdr, lv_color_hex(CLR_MUTED), 0);

        // ── A-G single-machine add buttons ────────────────────
        lv_obj_t *add_row = lv_obj_create(card);
        lv_obj_set_size(add_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(add_row, LV_OPA_0, 0);
        lv_obj_set_style_border_width(add_row, 0, 0);
        lv_obj_set_style_pad_all(add_row, 0, 0);
        lv_obj_set_style_pad_column(add_row, 6, 0);
        lv_obj_set_flex_flow(add_row, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(add_row, LV_OBJ_FLAG_SCROLLABLE);

        for (int mi = 0; mi < (int)MASCHINE_H; mi++) {
            lv_obj_t *ab = lv_btn_create(add_row);
            lv_obj_set_size(ab, 52, 52);
            lv_obj_set_style_radius(ab, 8, 0);
            lv_obj_set_style_bg_color(ab, lv_color_hex(CLR_SIDEBAR), 0);
            lv_obj_set_style_bg_opa(ab, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(ab, 2, 0);
            lv_obj_set_style_border_color(ab, lv_color_hex(CLR_PRIMARY), 0);
            // Pack ci (4 bits) and mi (4 bits)
            int packed = (ci << 4) | mi;
            lv_obj_add_event_cb(ab, [](lv_event_t *ev) {
                int pk = (int)(intptr_t)lv_event_get_user_data(ev);
                int c  = (pk >> 4) & 0xF;
                int m  = pk & 0xF;
                add_custom_entry(c, (CustomSequenzEintrag){
                    (Maschine)m, (Maschine)m, false, 0
                });
            }, LV_EVENT_CLICKED, (void*)(intptr_t)packed);
            lv_obj_t *ml = lv_label_create(ab);
            lv_label_set_text(ml, MACH_LBL[mi]);
            lv_obj_set_style_text_font(ml, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(ml, lv_color_hex(CLR_TEXT), 0);
            lv_obj_center(ml);
        }

        // ── Doublette builder ─────────────────────────────────
        lv_obj_t *pair_hdr = lv_label_create(card);
        lv_label_set_text(pair_hdr, "DOUBLETTE: A-G + PARTNER (DELAY A SEKONNEN)");
        lv_obj_set_style_text_font(pair_hdr, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(pair_hdr, lv_color_hex(CLR_MUTED), 0);

        lv_obj_t *pair_cfg = lv_obj_create(card);
        lv_obj_set_size(pair_cfg, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(pair_cfg, LV_OPA_0, 0);
        lv_obj_set_style_border_width(pair_cfg, 0, 0);
        lv_obj_set_style_pad_all(pair_cfg, 0, 0);
        lv_obj_set_style_pad_column(pair_cfg, 8, 0);
        lv_obj_set_flex_flow(pair_cfg, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(pair_cfg, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *delay_lbl = lv_label_create(pair_cfg);
        lv_label_set_text(delay_lbl, "DELAY:");
        lv_obj_set_style_text_font(delay_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(delay_lbl, lv_color_hex(CLR_TEXT), 0);
        s_pair_delay_ta[ci] = lv_textarea_create(pair_cfg);
        lv_obj_set_size(s_pair_delay_ta[ci], 110, 42);
        lv_textarea_set_one_line(s_pair_delay_ta[ci], true);
        lv_textarea_set_max_length(s_pair_delay_ta[ci], 5);
        lv_textarea_set_text(s_pair_delay_ta[ci], "1.0");
        lv_obj_set_style_bg_color(s_pair_delay_ta[ci], lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_text_color(s_pair_delay_ta[ci], lv_color_hex(CLR_TEXT), 0);
        lv_obj_t *seconds_lbl = lv_label_create(pair_cfg);
        lv_label_set_text(seconds_lbl, "SEK.  (0-10)");
        lv_obj_set_style_text_font(seconds_lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(seconds_lbl, lv_color_hex(CLR_MUTED), 0);

        lv_obj_t *pair_row = lv_obj_create(card);
        lv_obj_set_size(pair_row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(pair_row, LV_OPA_0, 0);
        lv_obj_set_style_border_width(pair_row, 0, 0);
        lv_obj_set_style_pad_all(pair_row, 0, 0);
        lv_obj_set_style_pad_column(pair_row, 6, 0);
        lv_obj_set_flex_flow(pair_row, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(pair_row, LV_OBJ_FLAG_SCROLLABLE);
        for (int mi = 0; mi < (int)MASCHINE_H; mi++) {
            lv_obj_t *pb = lv_btn_create(pair_row);
            lv_obj_set_size(pb, 52, 48);
            lv_obj_set_style_radius(pb, 8, 0);
            lv_obj_set_style_bg_color(pb, lv_color_hex(CLR_SIDEBAR), 0);
            lv_obj_set_style_bg_opa(pb, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(pb, 2, 0);
            lv_obj_set_style_border_color(pb, lv_color_hex(CLR_PRIMARY), 0);
            int packed = (ci << 4) | mi;
            lv_obj_add_event_cb(pb, [](lv_event_t *ev) {
                int pk = (int)(intptr_t)lv_event_get_user_data(ev);
                int c = (pk >> 4) & 0xF;
                int m = pk & 0xF;
                if (s_pending_pair_first[c] < 0) {
                    s_pending_pair_first[c] = m;
                    char msg[48];
                    snprintf(msg, sizeof(msg), "%s GEWIELT - PARTNER WIELEN",
                             MACH_LBL[m]);
                    set_pair_status(c, msg);
                } else if (s_pending_pair_first[c] == m) {
                    set_pair_status(c, "ENG ANER PARTNER-MASCHINN WIELEN");
                } else {
                    int first = s_pending_pair_first[c];
                    add_custom_entry(c, (CustomSequenzEintrag){
                        (Maschine)first, (Maschine)m, true, read_pair_delay_ms(c)
                    });
                    s_pending_pair_first[c] = -1;
                    set_pair_status(c, "DOUBLETTE GESPAECHERT");
                }
            }, LV_EVENT_CLICKED, (void*)(intptr_t)packed);
            lv_obj_t *pl = lv_label_create(pb);
            lv_label_set_text(pl, MACH_LBL[mi]);
            lv_obj_set_style_text_font(pl, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(pl, lv_color_hex(CLR_TEXT), 0);
            lv_obj_center(pl);
        }

        lv_obj_t *h_pair = lv_btn_create(pair_row);
        lv_obj_set_size(h_pair, 120, 48);
        lv_obj_set_style_radius(h_pair, 8, 0);
        lv_obj_set_style_bg_color(h_pair, lv_color_hex(0x78350F), 0);
        lv_obj_set_style_bg_opa(h_pair, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(h_pair, 2, 0);
        lv_obj_set_style_border_color(h_pair, lv_color_hex(0xD97706), 0);
        lv_obj_add_event_cb(h_pair, [](lv_event_t *ev) {
            int c = (int)(intptr_t)lv_event_get_user_data(ev);
            s_pending_pair_first[c] = -1;
            add_custom_entry(c, (CustomSequenzEintrag){
                MASCHINE_H, MASCHINE_H, true, 0
            });
            set_pair_status(c, "H: 1 FIRE - MASCHINN MAACHT H2");
        }, LV_EVENT_CLICKED, (void*)(intptr_t)ci);
        lv_obj_t *hl = lv_label_create(h_pair);
        lv_label_set_text(hl, "H  H1/H2");
        lv_obj_set_style_text_font(hl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(hl, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(hl);

        s_pair_status[ci] = lv_label_create(card);
        lv_label_set_text(s_pair_status[ci], "1. MASCHINN WIELEN - DUERNO PARTNER");
        lv_obj_set_style_text_font(s_pair_status[ci], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(s_pair_status[ci], lv_color_hex(CLR_MUTED), 0);

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

// ── Connection section: WiFi ──────────────────────────────────
static lv_obj_t *build_wifi_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 16, 0);

    lv_obj_t *section = lv_label_create(parent);
    lv_label_set_text(section, "WIFI");
    lv_obj_set_style_text_font(section, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(section, lv_color_hex(CLR_PRIMARY), 0);

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

// ── Tab: Products & Bills ─────────────────────────────────────
static lv_obj_t *build_products_bills_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 16, 0);
    lv_obj_set_style_pad_row(parent, 8, 0);

    struct PriceField { const char *label; const char *id; lv_obj_t **out; } price_fields[] = {
        {"PREIS SPIELKREDIT (EUR)", "GAME_CREDIT", &s_ta_price_credit},
        {"PREIS CAL.12 (EUR)", "AMMO_CAL12", &s_ta_price_cal12},
        {"PREIS CAL.20 (EUR)", "AMMO_CAL20", &s_ta_price_cal20},
    };
    for (unsigned i = 0; i < sizeof(price_fields)/sizeof(price_fields[0]); ++i) {
        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, price_fields[i].label);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(CLR_TEXT), 0);
        *price_fields[i].out = lv_textarea_create(parent);
        lv_obj_set_size(*price_fields[i].out, 180, 50);
        lv_textarea_set_one_line(*price_fields[i].out, true);
        const Produkt *p = store_produkt(price_fields[i].id);
        char price[16]; snprintf(price, sizeof(price), "%d.%02d",
                                 p ? p->preisCent / 100 : 0, p ? p->preisCent % 100 : 0);
        lv_textarea_set_text(*price_fields[i].out, price);
        lv_obj_set_style_bg_color(*price_fields[i].out, lv_color_hex(CLR_BORDER), 0);
        lv_obj_set_style_text_color(*price_fields[i].out, lv_color_hex(CLR_TEXT), 0);
        // Price revisions are issued by the portal. Cached values remain
        // visible offline but must not be edited into a non-existent revision.
        lv_obj_add_state(*price_fields[i].out, LV_STATE_DISABLED);
    }

    s_bill_day_summary = lv_label_create(parent);
    lv_obj_set_style_text_font(s_bill_day_summary, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_bill_day_summary, lv_color_hex(CLR_TEXT), 0);
    lv_label_set_long_mode(s_bill_day_summary, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_bill_day_summary, LV_PCT(100));
    refresh_bill_day_summary();
    return parent;
}

// ── Tab: System ───────────────────────────────────────────────
// Toggle callback for click-sound switch
static void click_sound_sw_cb(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    g_store.clickSoundEnabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    game_store_save();
}

static bool read_auto_sync_seconds(uint32_t *seconds)
{
    if (!s_auto_sync_seconds || !seconds) return false;
    const char *text = lv_textarea_get_text(s_auto_sync_seconds);
    if (!text || !text[0]) return false;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (*end || value < AUTO_SYNC_MIN_SECONDS || value > AUTO_SYNC_MAX_SECONDS)
        return false;
    *seconds = (uint32_t)value;
    return true;
}

static void auto_sync_changed(lv_event_t *e)
{
    (void)e;
    uint32_t seconds;
    if (!read_auto_sync_seconds(&seconds)) {
        lv_label_set_text(s_auto_sync_feedback, "10..86400 SEKONNEN NOUTWENDIG");
        lv_obj_set_style_text_color(s_auto_sync_feedback, lv_color_hex(CLR_DANGER), 0);
        return;
    }
    bool enabled = lv_obj_has_state(s_auto_sync_switch, LV_STATE_CHECKED);
    const char *billing_text = s_billing_sync_seconds
        ? lv_textarea_get_text(s_billing_sync_seconds) : "";
    char *billing_end = NULL;
    unsigned long billing_seconds = strtoul(billing_text, &billing_end, 10);
    if (!billing_text[0] || *billing_end ||
        billing_seconds < BILLING_SYNC_MIN_SECONDS ||
        billing_seconds > BILLING_SYNC_MAX_SECONDS) {
        lv_label_set_text(s_auto_sync_feedback, "BILLING: 20..30 SEKONNEN NOUTWENDIG");
        lv_obj_set_style_text_color(s_auto_sync_feedback, lv_color_hex(CLR_DANGER), 0);
        return;
    }
    store_set_auto_sync(enabled, seconds);
    store_set_billing_sync((uint32_t)billing_seconds);
    lv_label_set_text(s_auto_sync_feedback, "FULL + BILLING SYNC GESPEICHERT");
    lv_obj_set_style_text_color(s_auto_sync_feedback, lv_color_hex(CLR_SUCCESS), 0);
}

// ── Tab: Catering ─────────────────────────────────────────────
static lv_obj_t *build_catering_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 16, 0);
    lv_obj_set_style_pad_row(parent, 8, 0);

    lv_obj_t *cat = lv_obj_create(parent);
    lv_obj_set_size(cat, LV_PCT(100), 180);
    lv_obj_add_style(cat, &g_style_card, 0);
    lv_obj_set_flex_flow(cat, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(cat, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *cat_label = lv_label_create(cat);
    lv_label_set_text(cat_label, "ALE PIN");
    s_catering_old_pin = lv_textarea_create(cat); lv_obj_set_size(s_catering_old_pin, 140, 44);
    lv_textarea_set_one_line(s_catering_old_pin, true); lv_textarea_set_password_mode(s_catering_old_pin, true);
    lv_textarea_set_accepted_chars(s_catering_old_pin, "0123456789"); lv_textarea_set_max_length(s_catering_old_pin, 16);
    cat_label = lv_label_create(cat);
    lv_label_set_text(cat_label, "NEI PIN (4-16)");
    s_catering_pin = lv_textarea_create(cat); lv_obj_set_size(s_catering_pin, 180, 44);
    lv_textarea_set_one_line(s_catering_pin, true); lv_textarea_set_password_mode(s_catering_pin, true);
    lv_textarea_set_accepted_chars(s_catering_pin, "0123456789"); lv_textarea_set_max_length(s_catering_pin, 16);
    lv_obj_t *pin_save = lv_btn_create(cat); lv_obj_add_style(pin_save, &g_style_btn_secondary, 0);
    lv_obj_add_event_cb(pin_save, catering_save_pin_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *pin_save_l = lv_label_create(pin_save); lv_label_set_text(pin_save_l, "PIN SPEICHERN"); lv_obj_center(pin_save_l);
    lv_obj_t *enter = lv_btn_create(cat); lv_obj_add_style(enter, &g_style_btn_danger, 0);
    lv_obj_add_event_cb(enter, catering_enter_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *enter_l = lv_label_create(enter); lv_label_set_text(enter_l, "CATERING STARTEN"); lv_obj_center(enter_l);
    s_catering_status = lv_label_create(parent);
    lv_label_set_text(s_catering_status, g_store.cateringPinConfigured ? "CATERING PIN ASS KONFIGUREIERT." : "KENG CATERING PIN KONFIGUREIERT.");
    return parent;
}

static lv_obj_t *build_system_tab(lv_obj_t *parent)
{
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(parent, 16, 0);
    lv_obj_set_style_pad_row(parent, 8, 0);

    // ── Click-sound toggle ────────────────────────────────────
    lv_obj_t *snd_row = lv_obj_create(parent);
    lv_obj_set_size(snd_row, LV_PCT(100), 48);
    lv_obj_set_style_bg_color(snd_row, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_border_width(snd_row, 0, 0);
    lv_obj_set_style_radius(snd_row, 8, 0);
    lv_obj_set_style_pad_hor(snd_row, 12, 0);
    lv_obj_set_flex_flow(snd_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(snd_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *snd_lbl = lv_label_create(snd_row);
    lv_label_set_text(snd_lbl, LV_SYMBOL_VOLUME_MAX "  KLICK-SOUND");
    lv_obj_set_style_text_font(snd_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(snd_lbl, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *snd_sw = lv_switch_create(snd_row);
    if (g_store.clickSoundEnabled) lv_obj_add_state(snd_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(snd_sw, click_sound_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *sync_row = lv_obj_create(parent);
    lv_obj_set_size(sync_row, LV_PCT(100), 112);
    lv_obj_set_style_bg_color(sync_row, lv_color_hex(CLR_CARD), 0);
    lv_obj_set_style_border_width(sync_row, 0, 0);
    lv_obj_set_style_radius(sync_row, 8, 0);
    lv_obj_set_style_pad_hor(sync_row, 12, 0);
    lv_obj_set_flex_flow(sync_row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(sync_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(sync_row, 16, 0);
    lv_obj_t *sync_label = lv_label_create(sync_row);
    lv_label_set_text(sync_label, "AUTO FULL SYNC");
    lv_obj_set_style_text_color(sync_label, lv_color_hex(CLR_TEXT), 0);
    s_auto_sync_switch = lv_switch_create(sync_row);
    if (g_store.autoSyncEnabled) lv_obj_add_state(s_auto_sync_switch, LV_STATE_CHECKED);
    s_auto_sync_seconds = lv_textarea_create(sync_row);
    lv_obj_set_size(s_auto_sync_seconds, 150, 44);
    lv_textarea_set_one_line(s_auto_sync_seconds, true);
    lv_textarea_set_accepted_chars(s_auto_sync_seconds, "0123456789");
    lv_textarea_set_max_length(s_auto_sync_seconds, 5);
    char seconds_text[12];
    snprintf(seconds_text, sizeof(seconds_text), "%lu",
             (unsigned long)g_store.autoSyncSeconds);
    lv_textarea_set_text(s_auto_sync_seconds, seconds_text);
    lv_obj_t *seconds_label = lv_label_create(sync_row);
    lv_label_set_text(seconds_label, "SEK. (10..86400)");
    lv_obj_set_style_text_color(seconds_label, lv_color_hex(CLR_MUTED), 0);
    lv_obj_t *billing_label = lv_label_create(sync_row);
    lv_label_set_text(billing_label, "BILLING SYNC");
    lv_obj_set_style_text_color(billing_label, lv_color_hex(CLR_TEXT), 0);
    s_billing_sync_seconds = lv_textarea_create(sync_row);
    lv_obj_set_size(s_billing_sync_seconds, 100, 44);
    lv_textarea_set_one_line(s_billing_sync_seconds, true);
    lv_textarea_set_accepted_chars(s_billing_sync_seconds, "0123456789");
    lv_textarea_set_max_length(s_billing_sync_seconds, 2);
    snprintf(seconds_text, sizeof(seconds_text), "%lu",
             (unsigned long)g_store.billingSyncSeconds);
    lv_textarea_set_text(s_billing_sync_seconds, seconds_text);
    lv_obj_t *billing_seconds_label = lv_label_create(sync_row);
    lv_label_set_text(billing_seconds_label, "SEK. (20..30)");
    lv_obj_set_style_text_color(billing_seconds_label, lv_color_hex(CLR_MUTED), 0);
    s_auto_sync_feedback = lv_label_create(parent);
    lv_label_set_text(s_auto_sync_feedback, "");
    lv_obj_add_event_cb(s_auto_sync_switch, auto_sync_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_auto_sync_seconds, auto_sync_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_billing_sync_seconds, auto_sync_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);

    // ── Firmware / board info ─────────────────────────────────
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

    lv_obj_t *tab_conn     = lv_tabview_add_tab(s_tab_view, "Conn");
    lv_obj_t *tab_products = lv_tabview_add_tab(s_tab_view, "Products & Bills");
    lv_obj_t *tab_catering = lv_tabview_add_tab(s_tab_view, "Catering");
    lv_obj_t *tab_sys      = lv_tabview_add_tab(s_tab_view, "System");

    // Keep all settings reachable through four primary tabs.  The tab pages
    // remain vertically scrollable because Conn and System contain several
    // formerly separate pages.
    lv_obj_set_scroll_dir(tab_conn, LV_DIR_VER);
    lv_obj_set_scroll_dir(tab_products, LV_DIR_VER);
    lv_obj_set_scroll_dir(tab_catering, LV_DIR_VER);
    lv_obj_set_scroll_dir(tab_sys, LV_DIR_VER);

    build_api_tab(tab_conn);
    build_mach_tab(tab_conn);
    build_wifi_tab(tab_conn);
    build_products_bills_tab(tab_products);
    build_catering_tab(tab_catering);
    build_system_tab(tab_sys);
    build_custom_tab(tab_sys);

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
        lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    }, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_kb, [](lv_event_t *e) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_kb, NULL);
        lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
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
    lv_obj_add_event_cb(s_ta_gateway, [](lv_event_t *e) {
        lv_keyboard_set_textarea(s_kb, s_ta_gateway);
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_gateway_token, [](lv_event_t *e) {
        lv_keyboard_set_textarea(s_kb, s_ta_gateway_token);
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, NULL);
    for (int ci = 0; ci < 4; ++ci) {
        if (!s_pair_delay_ta[ci]) continue;
        lv_obj_add_event_cb(s_pair_delay_ta[ci], [](lv_event_t *e) {
            lv_keyboard_set_textarea(s_kb, (lv_obj_t *)lv_event_get_target(e));
            lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_FOCUSED, NULL);
    }
    if (s_auto_sync_seconds) {
        lv_obj_add_event_cb(s_auto_sync_seconds, [](lv_event_t *e) {
            lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_NUMBER);
            lv_keyboard_set_textarea(s_kb, s_auto_sync_seconds);
            lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_FOCUSED, NULL);
    }
    if (s_billing_sync_seconds) {
        lv_obj_add_event_cb(s_billing_sync_seconds, [](lv_event_t *e) {
            lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_NUMBER);
            lv_keyboard_set_textarea(s_kb, s_billing_sync_seconds);
            lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_FOCUSED, NULL);
    }
    if (s_catering_pin) {
        lv_obj_add_event_cb(s_catering_pin, [](lv_event_t *) {
            lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_NUMBER);
            lv_keyboard_set_textarea(s_kb, s_catering_pin);
            lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_FOCUSED, NULL);
    }
    if (s_catering_old_pin) {
        lv_obj_add_event_cb(s_catering_old_pin, [](lv_event_t *) {
            lv_keyboard_set_mode(s_kb, LV_KEYBOARD_MODE_NUMBER);
            lv_keyboard_set_textarea(s_kb, s_catering_old_pin);
            lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_FOCUSED, NULL);
    }

    return s_scr;
}

void screen_einstellungen_refresh(void)
{
    refresh_bill_day_summary();
    set_textarea_text_if_changed(s_ta_url, g_store.apiUrl);
    set_textarea_text_if_changed(s_ta_key, g_store.apiKey);
    set_textarea_text_if_changed(s_ta_gateway, g_store.gatewayUrl);
    if (s_config_backup_status) {
        char backup_status[192];
        snprintf(backup_status, sizeof(backup_status), "%s%s%s",
                 g_store.configBackupStatus[0] ? g_store.configBackupStatus
                                               : "NACH KEE BACKUP",
                 g_store.lastConfigBackupAt[0] ? "\nLESCHT BACKUP: " : "",
                 g_store.lastConfigBackupAt);
        set_label_text_if_changed(s_config_backup_status, backup_status);
    }
    set_textarea_text_if_changed(s_ta_gateway_token, g_store.gatewayToken);
    if (s_auto_sync_switch) {
        if (g_store.autoSyncEnabled) lv_obj_add_state(s_auto_sync_switch, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_auto_sync_switch, LV_STATE_CHECKED);
    }
    if (s_auto_sync_seconds) {
        char value[12];
        snprintf(value, sizeof(value), "%lu",
                 (unsigned long)g_store.autoSyncSeconds);
        set_textarea_text_if_changed(s_auto_sync_seconds, value);
    }
    if (s_billing_sync_seconds) {
        char value[12];
        snprintf(value, sizeof(value), "%lu",
                 (unsigned long)g_store.billingSyncSeconds);
        set_textarea_text_if_changed(s_billing_sync_seconds, value);
    }
    lv_obj_t *price_inputs[] = {s_ta_price_credit, s_ta_price_cal12, s_ta_price_cal20};
    const char *price_ids[] = {"GAME_CREDIT", "AMMO_CAL12", "AMMO_CAL20"};
    for (int i = 0; i < 3; ++i) if (price_inputs[i]) {
        const Produkt *p = store_produkt(price_ids[i]); char value[16];
        snprintf(value, sizeof(value), "%d.%02d", p ? p->preisCent / 100 : 0,
                 p ? p->preisCent % 100 : 0);
        set_textarea_text_if_changed(price_inputs[i], value);
    }
    for (int m = 0; m < MASCHINE_COUNT; m++) {
        if (!s_mach_sw[m]) continue;
        if (g_store.maschinenAktiv[m])
            lv_obj_add_state(s_mach_sw[m], LV_STATE_CHECKED);
        else
            lv_obj_clear_state(s_mach_sw[m], LV_STATE_CHECKED);
    }
    s_gateway_check_pending = false;
    if (s_lbl_api_status) lv_label_set_text(s_lbl_api_status, "");
    s_machine_test_pending = false;
    if (s_lbl_machine_test_status) {
        lv_label_set_text(s_lbl_machine_test_status,
                          "TEST FIRE löst genau einen Schuss aus — kein Spiel.");
        lv_obj_set_style_text_color(s_lbl_machine_test_status,
                                    lv_color_hex(CLR_MUTED), 0);
    }
}

void screen_einstellungen_tick(void)
{
    if (s_gateway_check_pending && s_lbl_api_status) {
        char status[96];
        lora_copy_status_text(status, sizeof(status));
        GatewayReachability gateway = lora_gateway_state();
        uint32_t color = gateway == GATEWAY_REACHABLE ? CLR_SUCCESS :
                         gateway == GATEWAY_CHECKING ? CLR_WARN : CLR_DANGER;
        set_api_status(status, color);
        if (!lora_request_busy()) s_gateway_check_pending = false;
    }

    if (s_machine_test_pending && s_lbl_machine_test_status) {
        char status[96];
        lora_copy_status_text(status, sizeof(status));
        uint32_t color = CLR_WARN;
        if (strstr(status, "fired") || strstr(status, "sent")) {
            color = CLR_SUCCESS;
        } else if (strstr(status, "unreachable") || strstr(status, "rejected") ||
                   strstr(status, "invalid") || strstr(status, "unavailable")) {
            color = CLR_DANGER;
        }
        const char *current = lv_label_get_text(s_lbl_machine_test_status);
        if (!current || strcmp(current, status) != 0) {
            lv_label_set_text(s_lbl_machine_test_status, status);
            lv_obj_set_style_text_color(s_lbl_machine_test_status,
                                        lv_color_hex(color), 0);
        }
        if (!lora_request_busy()) s_machine_test_pending = false;
    }
}
