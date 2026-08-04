#pragma once
#include "lvgl.h"

/**
 * Initialise the GSL3680 capacitive touch controller over I²C
 * and register it as an LVGL input device for the given display.
 */
void gsl3680_touch_init(lv_display_t *disp);
