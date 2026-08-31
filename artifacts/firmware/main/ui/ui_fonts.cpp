// Montserrat remains first in each lookup chain, retaining its ASCII metrics
// and LV_SYMBOL glyphs.  These mutable copies exist solely to attach the
// accent-only fallback without modifying or casting the const LVGL fonts.
#include "ui_fonts.h"

extern "C" {
extern const lv_font_t ui_font_fallback_12;
extern const lv_font_t ui_font_fallback_14;
extern const lv_font_t ui_font_fallback_16;
extern const lv_font_t ui_font_fallback_18;
extern const lv_font_t ui_font_fallback_20;
extern const lv_font_t ui_font_fallback_22;
extern const lv_font_t ui_font_fallback_24;
extern const lv_font_t ui_font_fallback_28;
extern const lv_font_t ui_font_fallback_36;
extern const lv_font_t ui_font_fallback_48;
}

lv_font_t ui_font_12;
lv_font_t ui_font_14;
lv_font_t ui_font_16;
lv_font_t ui_font_18;
lv_font_t ui_font_20;
lv_font_t ui_font_22;
lv_font_t ui_font_24;
lv_font_t ui_font_28;
lv_font_t ui_font_36;
lv_font_t ui_font_48;

void ui_fonts_init(void)
{
    static bool initialized;
    if (initialized) return;

    ui_font_12 = lv_font_montserrat_12;
    ui_font_14 = lv_font_montserrat_14;
    ui_font_16 = lv_font_montserrat_16;
    ui_font_18 = lv_font_montserrat_18;
    ui_font_20 = lv_font_montserrat_20;
    ui_font_22 = lv_font_montserrat_22;
    ui_font_24 = lv_font_montserrat_24;
    ui_font_28 = lv_font_montserrat_28;
    ui_font_36 = lv_font_montserrat_36;
    ui_font_48 = lv_font_montserrat_48;

    ui_font_12.fallback = &ui_font_fallback_12;
    ui_font_14.fallback = &ui_font_fallback_14;
    ui_font_16.fallback = &ui_font_fallback_16;
    ui_font_18.fallback = &ui_font_fallback_18;
    ui_font_20.fallback = &ui_font_fallback_20;
    ui_font_22.fallback = &ui_font_fallback_22;
    ui_font_24.fallback = &ui_font_fallback_24;
    ui_font_28.fallback = &ui_font_fallback_28;
    ui_font_36.fallback = &ui_font_fallback_36;
    ui_font_48.fallback = &ui_font_fallback_48;
    initialized = true;
}