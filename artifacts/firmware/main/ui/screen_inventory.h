#pragma once
#include "lvgl.h"
lv_obj_t *screen_inventory_create();
void screen_inventory_refresh();
void screen_inventory_tick();
void screen_inventory_open(bool closing,bool beforeShutdown=false);
