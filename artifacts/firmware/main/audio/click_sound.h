#pragma once
// ============================================================
// click_sound — Touch feedback via ES8311 I2S DAC
// ============================================================
// Initialise once after gsl3680_touch_init() (which sets up I2C).
// All play calls are non-blocking (post to a queue).
// If ES8311 is not detected at boot, every call is a silent no-op.
// ============================================================

#ifdef __cplusplus
extern "C" {
#endif

// Call once after gsl3680_touch_init(); safe to call even if ES8311
// is absent — click sound simply stays disabled.
void click_sound_init(void);

// Queue a single click beep if g_store.clickSoundEnabled is true.
// Non-blocking; safe to call from any task / LVGL callback.
void click_sound_play_if_enabled(void);

#ifdef __cplusplus
}
#endif
