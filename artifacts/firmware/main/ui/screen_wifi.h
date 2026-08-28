#pragma once
#include "lvgl.h"
// Call ONCE during boot (from ui_manager_init) while internal RAM is still
// healthy — creates the persistent scan worker; connect requests are handled
// by the co-processor's boot-time WiFi supervisor.
void      screen_wifi_create_workers(void);
lv_obj_t *screen_wifi_create(void);
void      screen_wifi_refresh(void);
void      screen_wifi_tick(void);
