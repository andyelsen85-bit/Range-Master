#pragma once
#include "lvgl.h"
lv_obj_t *screen_kredite_create(void);
void      screen_kredite_refresh(void);
void      screen_kredite_sync_completed(bool success, const char *error);
