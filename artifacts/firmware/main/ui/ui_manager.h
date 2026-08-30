#pragma once
// ============================================================
// UI manager — screen router for LVGL
// ============================================================
#include "game_store.h"
#include "app_config.h"    // DISPLAY_LOGICAL_W/H, CLUB_NAME, APP_VERSION, etc.

/** Create all LVGL screens and show the dashboard. */
void ui_manager_init(void);

/** Register the touch input so sync can cancel an active press safely. */
void ui_manager_set_input_device(lv_indev_t *indev);

/** Switch to the given screen (called when g_store.screen changes). */
void ui_manager_show(Screen s);

/** Call from the LVGL timer task to react to store state changes. */
void ui_manager_tick(void);

/** Base screen styling (dark bg, opaque, non-scrollable). Each screen's
 *  _create() calls this right after lv_obj_create(NULL). */
void screen_base_init(lv_obj_t *scr);

// ── Shared style helpers ─────────────────────────────────────
#include "lvgl.h"

extern lv_style_t g_style_card;
extern lv_style_t g_style_btn_primary;
extern lv_style_t g_style_btn_secondary;
extern lv_style_t g_style_btn_danger;
extern lv_style_t g_style_label_title;
extern lv_style_t g_style_label_mono;
extern lv_style_t g_style_sidebar;

// Colour palette
#define CLR_BG          0x0A0F1A
#define CLR_CARD        0x111827
#define CLR_BORDER      0x1F2937
#define CLR_PRIMARY     0x3B82F6
#define CLR_PRIMARY_DIM 0x1D4ED8
#define CLR_SUCCESS     0x22C55E
#define CLR_WARN        0xF59E0B
#define CLR_DANGER      0xEF4444
#define CLR_TEXT        0xF9FAFB
#define CLR_MUTED       0x6B7280
#define CLR_SIDEBAR     0x0D1521
