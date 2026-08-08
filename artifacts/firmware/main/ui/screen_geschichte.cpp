// ============================================================
// Spillgeschicht screen — local game history list
// Mirrors SpielgeschichteScreen.tsx / SpillgeschichteScreen.tsx
// ============================================================
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_manager.h"
#include "game_store.h"
#include "screen_geschichte.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_list;
static lv_obj_t *s_lbl_empty;

// ── Build one history row ─────────────────────────────────────
static void build_history_rows(void)
{
    lv_obj_clean(s_list);
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    if (g_store.historyCount == 0) {
        lv_obj_clear_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    for (int i = g_store.historyCount - 1; i >= 0; i--) {
        const FinishedGame *fg = &g_store.history[i];
        // Compute winner name
        int best_id = -1, best_pts = -1;
        for (int t = 0; t < fg->base.teilnahmen_count; t++) {
            if (fg->base.teilnahmen[t].punkte > best_pts) {
                best_pts = fg->base.teilnahmen[t].punkte;
                best_id  = fg->base.teilnahmen[t].spielerId;
            }
        }
        const char *winner_name = "";
        for (int j = 0; j < fg->spieler_count; j++) {
            if (fg->spielerIds[j] == best_id) {
                winner_name = fg->spielerNamen[j];
                break;
            }
        }

        char row[128];
        snprintf(row, sizeof(row),
                 "%s  |  %s  |  %d Spiller  |  " LV_SYMBOL_CHARGE " %s (%d Pkt)",
                 fg->finishedAt,
                 modus_label(fg->base.modus),
                 fg->spieler_count,
                 winner_name, best_pts);
        lv_list_add_btn(s_list, NULL, row);
    }
}

lv_obj_t *screen_geschichte_create(void)
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
    lv_label_set_text(title, LV_SYMBOL_LIST "  SPILLGESCHICHT");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_PRIMARY), 0);

    lv_obj_t *back = lv_btn_create(hdr);
    lv_obj_add_style(back, &g_style_btn_secondary, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *e) {
        ui_manager_show(SCREEN_DASHBOARD);
    }, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_HOME "  Zréck");
    lv_obj_set_style_text_color(bl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_center(bl);

    // Count label
    s_lbl_empty = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_empty, "Keng Spiller nach");
    lv_obj_set_style_text_font(s_lbl_empty, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(s_lbl_empty, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(s_lbl_empty, LV_ALIGN_CENTER, 0, 0);

    // History list
    s_list = lv_list_create(s_scr);
    lv_obj_set_size(s_list, DISPLAY_LOGICAL_W - 40, DISPLAY_LOGICAL_H - 86);
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_style_bg_color(s_list, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 4, 0);
    lv_obj_set_style_pad_row(s_list, 4, 0);
    lv_obj_set_style_text_font(s_list, &lv_font_montserrat_14, 0);

    return s_scr;
}

void screen_geschichte_refresh(void)
{
    if (!s_list) return;
    build_history_rows();
}
