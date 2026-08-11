#pragma once
// ============================================================
// click_sound — Button-press feedback via ES8311 I2S DAC
// ============================================================
// Call order in lvgl_task:
//   1. gsl3680_touch_init(disp)   — sets up I2C bus
//   2. click_sound_init()          — init I2S + ES8311 codec
//   3. click_sound_setup_lvgl_hook(disp) — install theme hook
//   4. ui_manager_init()           — builds screens (hook active)
//
// If ES8311 is not detected at boot every call is a silent no-op.
// ============================================================
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialise I2S channel and ES8311 codec.
// Call once after gsl3680_touch_init() (I2C already up).
void click_sound_init(void);

// Install a thin LVGL theme wrapper that fires click_sound_play_if_enabled()
// on LV_EVENT_PRESSED for every new clickable-but-not-scrollable object
// (buttons, toggles — not scroll containers).
// Must be called AFTER click_sound_init() and BEFORE ui_manager_init().
void click_sound_setup_lvgl_hook(lv_display_t *disp);

// Queue a single click beep if g_store.clickSoundEnabled is true.
// Non-blocking; safe to call from any task or LVGL callback.
void click_sound_play_if_enabled(void);

#ifdef __cplusplus
}
#endif
