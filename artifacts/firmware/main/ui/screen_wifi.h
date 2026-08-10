#pragma once
#include "lvgl.h"
// Call ONCE during boot (from ui_manager_init) while internal RAM is still
// healthy — creates the persistent scan/connect worker tasks so scan_cb and
// connect_cb can queue-send instead of xTaskCreate at tap time.
void      screen_wifi_create_workers(void);
lv_obj_t *screen_wifi_create(void);
void      screen_wifi_refresh(void);
