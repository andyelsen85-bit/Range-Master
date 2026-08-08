// ============================================================
// Kreditter screen — credit management for today's players
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

// ── Grant credit button callback ──────────────────────────────
static void grant_cb(lv_event_t *e)
{
    int spieler_id = (int)(intptr_t)lv_event_get_user_data(e);
    store_add_kredite(spieler_id, 1);
    screen_kredite_refresh();
}

// ── Add player to today's list ────────────────────────────────
static void add_player_cb(lv_event_t *e)
{
    if (!s_dd_add) return;
    uint16_t sel = lv_dropdown_get_selected(s_dd_add);
    if (sel >= (uint16_t)g_store.portalSpielerCount) return;
    PortalSpieler *ps = &g_store.portalSpieler[sel];
    store_register_spieler_fuer_tag(ps->id);
    lv_label_set_text(s_lbl_status, "Spiller dobäigesat");
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_SUCCESS), 0);
    screen_kredite_refresh();
}

// ── Build player list ─────────────────────────────────────────
static void build_player_list(void)
{
    lv_obj_clean(s_player_list);

    int count = 0;
    for (int i = 0; i < MAX_PORTAL_SPIELER; i++) {
        if (g_store.kreditPlayerIds[i] == 0) continue;
        int sid = g_store.kreditPlayerIds[i];
        KreditStand *k = &g_store.kredite[i];

        // Find player name
        const char *name = "Onbekannt";
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

        // Name + credit count
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
                 "%d Kreditter verfügbar  (%d/%d verbraucht)",
                 avail, k->verbraucht, k->gewaehrt);
        lv_obj_t *cred_lbl = lv_label_create(info);
        lv_label_set_text(cred_lbl, cred_buf);
        lv_obj_set_style_text_font(cred_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(cred_lbl,
            avail > 0 ? lv_color_hex(CLR_SUCCESS) : lv_color_hex(CLR_DANGER), 0);

        // +1 credit button
        lv_obj_t *btn = lv_btn_create(row);
        lv_obj_add_style(btn, &g_style_btn_primary, 0);
        lv_obj_set_size(btn, 80, 44);
        lv_obj_add_event_cb(btn, grant_cb, LV_EVENT_CLICKED,
                            (void*)(intptr_t)sid);
        lv_obj_t *bl = lv_label_create(btn);
        lv_label_set_text(bl, "+ 1");
        lv_obj_set_style_text_font(bl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(bl, lv_color_hex(CLR_TEXT), 0);
        lv_obj_center(bl);

        count++;
    }

    if (count == 0) {
        lv_obj_t *empty = lv_label_create(s_player_list);
        lv_label_set_text(empty, "Keng Spiller fir haut registréiert");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(CLR_MUTED), 0);
    }
}

lv_obj_t *screen_kredite_create(void)
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
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(hdr, 20, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_CHARGE "  KREDITTER");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_PRIMARY), 0);

    lv_obj_t *back = lv_btn_create(hdr);
    lv_obj_add_style(back, &g_style_btn_secondary, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *e) {
        ui_manager_show(SCREEN_DASHBOARD);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl2 = lv_label_create(back);
    lv_label_set_text(bl2, LV_SYMBOL_HOME "  Zréck");
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
    lv_label_set_text(add_lbl, "Spiller dobäisetzen:");
    lv_obj_set_style_text_font(add_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(add_lbl, lv_color_hex(CLR_TEXT), 0);

    s_dd_add = lv_dropdown_create(add_row);
    lv_obj_set_size(s_dd_add, 300, 44);
    lv_obj_set_style_text_font(s_dd_add, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(s_dd_add, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_text_color(s_dd_add, lv_color_hex(CLR_TEXT), 0);
    lv_dropdown_set_options(s_dd_add, "—");

    lv_obj_t *add_btn = lv_btn_create(add_row);
    lv_obj_add_style(add_btn, &g_style_btn_primary, 0);
    lv_obj_set_size(add_btn, 120, 44);
    lv_obj_add_event_cb(add_btn, add_player_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *abl = lv_label_create(add_btn);
    lv_label_set_text(abl, "+ Dobäisetzen");
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

    // Rebuild dropdown — heap-allocated: ~13 KB is too large for stack AND for
    // static BSS (static lands in internal DRAM, which is the scarce resource).
    // lv_dropdown_set_options() copies via lv_strdup(), so free() is safe after.
    const size_t opts_sz = MAX_PORTAL_SPIELER * (MAX_NAME_LEN + 1) + 4;
    char *opts = (char *)malloc(opts_sz);
    if (!opts) return;
    opts[0] = '\0'; strncat(opts, "—", opts_sz - 1);
    for (int i = 0; i < g_store.portalSpielerCount; i++) {
        strncat(opts, "\n", opts_sz - strlen(opts) - 1);
        strncat(opts, g_store.portalSpieler[i].name,
                opts_sz - strlen(opts) - 1);
    }
    if (s_dd_add) lv_dropdown_set_options(s_dd_add, opts);
    free(opts);

    build_player_list();
}
