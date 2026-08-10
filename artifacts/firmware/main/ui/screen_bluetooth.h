#pragma once
#include "lvgl.h"
// Call ONCE during boot (from ui_manager_init) while internal RAM is still
// healthy — creates the persistent BLE pairing worker so pair_cb can
// queue-send instead of xTaskCreate at tap time.
void      screen_bluetooth_create_workers(void);
lv_obj_t *screen_bluetooth_create(void);
void      screen_bluetooth_refresh(void);
