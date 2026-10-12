#pragma once
#include "lvgl.h"
lv_obj_t *screen_dashboard_create(void);
void      screen_dashboard_refresh(void);
void screen_dashboard_shutdown_without_inventory();
void screen_dashboard_continue_shutdown();
void      screen_dashboard_tick(void);
