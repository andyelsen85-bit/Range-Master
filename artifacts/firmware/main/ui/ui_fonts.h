#pragma once

// UI fonts retain Montserrat as their primary font and use a compact Latin
// fallback for characters outside the built-in font's ASCII/icon coverage.
#include "lvgl.h"

void ui_fonts_init(void);

extern lv_font_t ui_font_12;
extern lv_font_t ui_font_14;
extern lv_font_t ui_font_16;
extern lv_font_t ui_font_18;
extern lv_font_t ui_font_20;
extern lv_font_t ui_font_22;
extern lv_font_t ui_font_24;
extern lv_font_t ui_font_28;
extern lv_font_t ui_font_36;
extern lv_font_t ui_font_48;

#define UI_FONT_12 (&ui_font_12)
#define UI_FONT_14 (&ui_font_14)
#define UI_FONT_16 (&ui_font_16)
#define UI_FONT_18 (&ui_font_18)
#define UI_FONT_20 (&ui_font_20)
#define UI_FONT_22 (&ui_font_22)
#define UI_FONT_24 (&ui_font_24)
#define UI_FONT_28 (&ui_font_28)
#define UI_FONT_36 (&ui_font_36)
#define UI_FONT_48 (&ui_font_48)