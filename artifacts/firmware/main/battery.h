#pragma once
// ============================================================
// battery — ADC-based battery level reading
//
// Reads battery voltage via GPIO52 (voltage divider, 1:1 ratio)
// which is the IP5306-based battery add-on wiring documented for
// the Guition JC8012P4A1C board.
//
// Call battery_init() once at boot (before the LVGL task starts).
// Then battery_get_percent() / battery_get_mv() are safe to call
// from any task (adc_oneshot is thread-safe).
//
// Calibration: if the readings seem off, adjust BATT_MV_EMPTY /
// BATT_MV_FULL in app_config.h.  Use battery_get_mv() to read
// the raw millivolt value and compare against a multimeter.
// ============================================================
#ifdef __cplusplus
extern "C" {
#endif

// Initialise ADC on GPIO52.  Returns false if the GPIO is not ADC-
// capable on this build, or ADC unit 1 is already claimed.
bool battery_init(void);

// Returns 0-100 (percentage), or -1 if battery_init() failed.
// Takes ~100 µs (8× oversampled ADC read).  Safe from LVGL task.
int battery_get_percent(void);

// Returns raw millivolt reading at GPIO52 (post-divider).
// Useful for calibrating BATT_MV_EMPTY / BATT_MV_FULL.
// Returns -1 if not initialised.
int battery_get_mv(void);

// True if battery_init() succeeded.
bool battery_is_available(void);

#ifdef __cplusplus
}
#endif
