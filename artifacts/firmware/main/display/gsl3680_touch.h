#pragma once
#include "lvgl.h"

/**
 * Initialise the GSL3680 capacitive touch controller over I²C,
 * register it as an LVGL input device for the given display, and
 * return the indev handle so callers can attach event hooks.
 */
lv_indev_t *gsl3680_touch_init(lv_display_t *disp);
