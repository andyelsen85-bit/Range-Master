#pragma once
#include "lvgl.h"
lv_obj_t *screen_spiel_create(void);
void      screen_spiel_refresh(void);
void      screen_spiel_tick(void);
enum SpielActionResult {
    SPIEL_ACTION_OK, SPIEL_ACTION_IGNORED, SPIEL_ACTION_SCORE_FIRST,
    SPIEL_ACTION_RADIO_BUSY
};
SpielActionResult spiel_action_fire();
SpielActionResult spiel_action_repeat();
SpielActionResult spiel_action_skip();
SpielActionResult spiel_action_score(int points);
