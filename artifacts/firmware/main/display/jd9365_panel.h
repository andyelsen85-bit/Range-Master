#pragma once
#include "esp_lcd_panel_ops.h"

/**
 * Initialise the JD9365 MIPI-DSI panel (hardware only — no LVGL).
 *
 * Performs:
 *   Step 0  D-PHY LDO power-up
 *   Step 1  DSI bus
 *   Step 2  DBI IO
 *   Step 3  DPI panel (2 frame buffers in PSRAM)
 *   Step 4  GPIO hardware reset
 *   Step 5  JD9365 init command sequence
 *   Step 6  esp_lcd_panel_init()
 *
 * @return  Panel handle on success, NULL on any failure.
 */
esp_lcd_panel_handle_t jd9365_panel_init(void);
