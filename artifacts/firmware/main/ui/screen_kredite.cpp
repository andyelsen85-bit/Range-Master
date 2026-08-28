// ============================================================
// Kreditter screen - credit management for today's players
// Mirrors KrediteScreen.tsx
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_kredite.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_player_list;
static lv_obj_t *s_dd_add;
static lv_obj_t *s_lbl_status;

static void munition_cb(lv_event_t *e)
{
    intptr_t packed = (intptr_t)lv_event_get_user_data(e);
    int spieler_id = (int)(packed >> 4);
    int code = (int)(packed & 0xF);
    const char *product = code <= 2 ? "AMMO_CAL12" : "AMMO_CAL20";
    int qty = code <= 2 ? code : code - 2;
    if (!store_queue_verkauf(spieler_id, product, qty))
        lv_label_set_text(s_lbl_status, "VERKAUF KONNT NET GESPAECHERT GINN");
    screen_kredite_refresh();
}

// ── Grant credit (+1) ─────────────────────────────────────────
static void grant_cb(lv_event_t *e)
{
    int spieler_id = (int)(intptr_t)lv_event_get_user_data(e);
    store_add_kredite(spieler_id, 1);
    screen_kredite_refresh();
}

// ── Revoke credit (-1, never below 0 available) ───────────────
static void revoke_cb(lv_event_t *e)
{
    int spieler_id = (int)(intptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < MAX_PORTAL_SPIELER; i++) {
        if (g_store.kreditPlayerIds[i] != spieler_id) continue;
        KreditStand *k = &g_store.kredite[i];
        if (k->gewaehrt > k->verbraucht) {   // keep available >= 0
            k->gewaehrt--;
            game_store_save();
        }
        break;
    }
    screen_kredite_refresh();
}

// ── Remove player from today's list ──────────────────────────
static void remove_player_cb(lv_event_t *e)
{
    int spieler_id = (int)(intptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < MAX_PORTAL_SPIELER; i++) {
        if (g_store.kreditPlayerIds[i] != spieler_id) continue;
        g_store.kreditPlayerIds[i] = 0;
        g_store.kredite[i]         = (KreditStand){0, 0};
        game_store_save();
        break;
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

    int count = 0;
    // The canonical sales GET is a day aggregate (not player-attributed), so
    // expose its reconciled totals once above the locally attributed rows.
    lv_obj_t *columns = lv_label_create(s_player_list);
    char columns_text[96];
    snprintf(columns_text, sizeof(columns_text),
             "CREDITS                 CAL.12: %d       CAL.20: %d",
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
        lv_obj_set_size(row, LV_PCT(100), 64);
        lv_obj_add_style(row, &g_style_card, 0);
        lv_obj_set_style_pad_all(row, 10, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Name + the three independent daily counters.
        lv_obj_t *info = lv_obj_create(row);
        lv_obj_set_size(info, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
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

        // Button group: [-1]  [+1]  [X]
        lv_obj_t *btn_grp = lv_obj_create(row);
        lv_obj_set_size(btn_grp, LV_SIZE_CONTENT, 44);
        lv_obj_set_style_bg_opa(btn_grp, LV_OPA_0, 0);
        lv_obj_set_style_border_width(btn_grp, 0, 0);
        lv_obj_set_style_pad_all(btn_grp, 0, 0);
        lv_obj_set_flex_flow(btn_grp, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(btn_grp, 8, 0);

        // -1
        lv_obj_t *btn_minus = lv_btn_create(btn_grp);
        lv_obj_set_size(btn_minus, 72, 44);
        lv_obj_set_style_bg_color(btn_minus, lv_color_hex(CLR_WARN), 0);
        lv_obj_set_style_bg_opa(btn_minus, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn_minus, 8, 0);
        lv_obj_set_style_border_width(btn_minus, 0, 0);
        lv_obj_add_event_cb(btn_minus, revoke_cb, LV_EVENT_CLICKED,
                            (void*)(intptr_t)sid);
        lv_obj_t *bml = lv_label_create(btn_minus);
        lv_label_set_text(bml, "- 1");
        lv_obj_set_style_text_font(bml, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(bml, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(bml);

        // +1
        lv_obj_t *btn_plus = lv_btn_create(btn_grp);
        lv_obj_add_style(btn_plus, &g_style_btn_primary, 0);
        lv_obj_set_size(btn_plus, 72, 44);
        lv_obj_add_event_cb(btn_plus, grant_cb, LV_EVENT_CLICKED,
                            (void*)(intptr_t)sid);
        lv_obj_t *bpl = lv_label_create(btn_plus);
        lv_label_set_text(bpl, "+ 1");
        lv_obj_set_style_text_font(bpl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(bpl, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(bpl);

        // Ammunition sales: compact, explicit caliber buttons. Quantities are
        // append-only sale events, not mutable UI totals.
        for (int action = 1; action <= 4; ++action) {
            lv_obj_t *ammo = lv_btn_create(btn_grp);
            lv_obj_set_size(ammo, 58, 44);
            lv_obj_add_style(ammo, &g_style_btn_secondary, 0);
            int code = action <= 2 ? action : action; // 1/2 Cal.12, 1/2 Cal.20
            intptr_t packed = ((intptr_t)sid << 4) | code;
            lv_obj_add_event_cb(ammo, munition_cb, LV_EVENT_CLICKED, (void *)packed);
            lv_obj_t *al = lv_label_create(ammo);
            char text[12];
            if (action <= 2) snprintf(text, sizeof(text), "12 +%d", action);
            else snprintf(text, sizeof(text), "20 +%d", action - 2);
            lv_label_set_text(al, text);
            lv_obj_set_style_text_font(al, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(al, lv_color_hex(CLR_TEXT), 0);
            lv_obj_center(al);
        }

        // X (remove from today's list)
        lv_obj_t *btn_del = lv_btn_create(btn_grp);
        lv_obj_set_size(btn_del, 52, 44);
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
