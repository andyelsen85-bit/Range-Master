// ============================================================
// SPILLERverwaltung screen - player management
// Mirrors SPILLERScreen.tsx
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_spiller.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_list;
static lv_obj_t *s_ta_new_name;
static lv_obj_t *s_lbl_status;
static lv_obj_t *s_kb;

// ── Add local player ──────────────────────────────────────────
static void add_local_cb(lv_event_t *e)
{
    const char *name = lv_textarea_get_text(s_ta_new_name);
    if (!name || strlen(name) == 0) {
        lv_label_set_text(s_lbl_status, "NUMM ASS EIDEL");
        lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_DANGER), 0);
        return;
    }
    int new_id;
    store_add_lokal_spieler(name, &new_id);
    lv_textarea_set_text(s_ta_new_name, "");
    lv_label_set_text(s_lbl_status, LV_SYMBOL_OK " SPILLER DOBAIGESAT (LOKAL)");
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_SUCCESS), 0);
    screen_spiller_refresh();
}

// ── Sync portal list ──────────────────────────────────────────
static void reload_cb(lv_event_t *e)
{
    store_sync();
    lv_label_set_text(s_lbl_status, LV_SYMBOL_REFRESH " SYNC LUEFT...");
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(CLR_MUTED), 0);
}

// ── Build player rows ─────────────────────────────────────────
static void build_list(void)
{
    lv_obj_clean(s_list);

    for (int i = 0; i < g_store.portalSpielerCount; i++) {
        PortalSpieler *ps = &g_store.portalSpieler[i];

        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_set_size(row, LV_PCT(100), 64);
        lv_obj_add_style(row, &g_style_card, 0);
        lv_obj_set_style_pad_all(row, 10, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // Info column
        lv_obj_t *info = lv_obj_create(row);
        lv_obj_set_size(info, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(info, LV_OPA_0, 0);
        lv_obj_set_style_border_width(info, 0, 0);
        lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);

        // Name + badge
        lv_obj_t *name_row = lv_obj_create(info);
        lv_obj_set_size(name_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(name_row, LV_OPA_0, 0);
        lv_obj_set_style_border_width(name_row, 0, 0);
        lv_obj_set_style_pad_all(name_row, 0, 0);
        lv_obj_set_flex_flow(name_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(name_row, 8, 0);

        lv_obj_t *name_lbl = lv_label_create(name_row);
        lv_label_set_text(name_lbl, ps->name);
        lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(CLR_TEXT), 0);

        if (ps->lokal) {
            lv_obj_t *badge = lv_label_create(name_row);
            lv_label_set_text(badge, "LOKAL");
            lv_obj_set_style_text_font(badge, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(badge, lv_color_hex(CLR_WARN), 0);
            lv_obj_set_style_bg_color(badge, lv_color_hex(0x78350F), 0);
            lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(badge, 4, 0);
            lv_obj_set_style_pad_hor(badge, 6, 0);
            lv_obj_set_style_pad_ver(badge, 2, 0);
        }

        if (strlen(ps->mitgliedNr) > 0) {
            lv_obj_t *nr_lbl = lv_label_create(info);
            char nr_buf[40];
            snprintf(nr_buf, sizeof(nr_buf), "Nr: %s", ps->mitgliedNr);
            lv_label_set_text(nr_lbl, nr_buf);
            lv_obj_set_style_text_font(nr_lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(nr_lbl, lv_color_hex(CLR_MUTED), 0);
        }

        // Portal active indicator
        lv_obj_t *aktiv_lbl = lv_label_create(row);
        lv_label_set_text(aktiv_lbl, ps->portalAktiv ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_font(aktiv_lbl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(aktiv_lbl,
            ps->portalAktiv ? lv_color_hex(CLR_SUCCESS) : lv_color_hex(CLR_MUTED), 0);
    }

    if (g_store.portalSpielerCount == 0) {
        lv_obj_t *empty = lv_label_create(s_list);
        lv_label_set_text(empty, "KENG SPILLER. PORTAL LUEDEN ODER LOKAL DOBAISETZEN.");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(CLR_MUTED), 0);
    }
}

lv_obj_t *screen_spiller_create(void)
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
    lv_label_set_text(title, LV_SYMBOL_LIST "  SPILLERVERWALTUNG");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_PRIMARY), 0);

    // Action buttons row
    lv_obj_t *btn_row = lv_obj_create(hdr);
    lv_obj_set_size(btn_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_0, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_row, 8, 0);

    lv_obj_t *reload_btn = lv_btn_create(btn_row);
    lv_obj_add_style(reload_btn, &g_style_btn_secondary, 0);
    lv_obj_add_event_cb(reload_btn, reload_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(reload_btn);
    lv_label_set_text(rl, LV_SYMBOL_REFRESH " Portal");
    lv_obj_set_style_text_color(rl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(rl);

    lv_obj_t *back = lv_btn_create(btn_row);
    lv_obj_add_style(back, &g_style_btn_secondary, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *e) {
        ui_manager_show(SCREEN_DASHBOARD);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl2 = lv_label_create(back);
    lv_label_set_text(bl2, LV_SYMBOL_HOME "  ZURUCK");
    lv_obj_set_style_text_color(bl2, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(bl2);

    // Add local player row
    lv_obj_t *add_row = lv_obj_create(s_scr);
    lv_obj_set_size(add_row, DISPLAY_LOGICAL_W - 40, 60);
    lv_obj_align(add_row, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_add_style(add_row, &g_style_card, 0);
    lv_obj_set_flex_flow(add_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(add_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(add_row, 12, 0);

    lv_obj_t *add_hdr = lv_label_create(add_row);
    lv_label_set_text(add_hdr, "Neie SPILLER:");
    lv_obj_set_style_text_font(add_hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(add_hdr, lv_color_hex(CLR_TEXT), 0);

    s_ta_new_name = lv_textarea_create(add_row);
    lv_obj_set_size(s_ta_new_name, 280, 44);
    lv_textarea_set_placeholder_text(s_ta_new_name, "Numm...");
    lv_textarea_set_one_line(s_ta_new_name, true);
    lv_obj_set_style_text_font(s_ta_new_name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(s_ta_new_name, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_text_color(s_ta_new_name, lv_color_hex(CLR_TEXT), 0);

    lv_obj_t *add_btn = lv_btn_create(add_row);
    lv_obj_add_style(add_btn, &g_style_btn_primary, 0);
    lv_obj_set_size(add_btn, 130, 44);
    lv_obj_add_event_cb(add_btn, add_local_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *abl = lv_label_create(add_btn);
    lv_label_set_text(abl, "+ Lokal");
    lv_obj_set_style_text_font(abl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(abl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(abl);

    s_lbl_status = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_status, "");
    lv_obj_align(s_lbl_status, LV_ALIGN_TOP_MID, 0, 148);
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_montserrat_14, 0);

    // ── On-screen keyboard ────────────────────────────────────────
    // Created last so it renders on top of all other widgets.
    s_kb = lv_keyboard_create(s_scr);
    lv_obj_set_size(s_kb, DISPLAY_LOGICAL_W, 320);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

    // Dark theme styling
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

    // Tapping the name textarea shows the keyboard
    lv_obj_add_event_cb(s_ta_new_name, [](lv_event_t *e) {
        lv_keyboard_set_textarea(s_kb, s_ta_new_name);
        lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_FOCUSED, NULL);

    // Scrollable player list
    s_list = lv_obj_create(s_scr);
    lv_obj_set_size(s_list, DISPLAY_LOGICAL_W - 40,
                    DISPLAY_LOGICAL_H - 70 - 68 - 30);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 70 + 68 + 30);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 8, 0);

    return s_scr;
}

void screen_spiller_refresh(void)
{
    if (!s_list) return;
    build_list();
}
