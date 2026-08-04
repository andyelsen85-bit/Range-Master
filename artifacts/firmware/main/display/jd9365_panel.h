#pragma once
#include "lvgl.h"

/**
 * Initialise the JD9365 MIPI-DSI panel and register it as an LVGL display.
 *
 * @return  Pointer to the lv_display_t on success, NULL on failure.
 */
lv_display_t *jd9365_panel_init(void);
