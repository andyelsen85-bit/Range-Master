// ============================================================
// battery.cpp — ADC-based battery level (Guition JC8012P4A1C)
//
// GPIO52 = ADC1 channel 3 on ESP32-P4.
// The IP5306 battery board uses a 1:1 voltage divider, so the
// ADC pin sees half the battery cell voltage:
//   Cell 3.0 V (empty) → ADC ~1500 mV
//   Cell 4.2 V (full)  → ADC ~2100 mV
//
// Charging state cannot be read from firmware on this board —
// there is no accessible I2C or GPIO charge-state line.
// ============================================================
#include "battery.h"
#include "app_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t s_adc   = NULL;
static adc_cali_handle_t         s_cali  = NULL;
static bool                      s_ready = false;

bool battery_init(void)
{
    // ── ADC unit 1 ───────────────────────────────────────────
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id  = BATT_ADC_UNIT;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc_oneshot_new_unit failed: %s — battery unavailable",
                 esp_err_to_name(err));
        return false;
    }

    // ── Channel config ───────────────────────────────────────
    // ADC_ATTEN_DB_12 → input range 0–3100 mV (covers 0–2100 mV divider range)
    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten    = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;

    err = adc_oneshot_config_channel(s_adc, BATT_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        return false;
    }

    // ── Calibration (curve fitting — preferred on ESP32-P4) ──
    // Falls back to uncalibrated raw conversion if eFuse values
    // are absent or the scheme is not compiled in.
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id  = BATT_ADC_UNIT;
    cali_cfg.chan     = BATT_ADC_CHANNEL;
    cali_cfg.atten    = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;

    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration: curve fitting");
    } else {
        ESP_LOGW(TAG, "Curve-fitting calibration unavailable (%s) — "
                      "using raw conversion (readings may vary ±50 mV)",
                 esp_err_to_name(err));
        s_cali = NULL;
    }

    s_ready = true;
    ESP_LOGI(TAG, "Battery ADC ready: GPIO%d  channel=%d  "
                  "empty=%d mV  full=%d mV",
             (int)BATT_ADC_GPIO, (int)BATT_ADC_CHANNEL,
             BATT_MV_EMPTY, BATT_MV_FULL);
    return true;
}

int battery_get_mv(void)
{
    if (!s_ready || !s_adc) return -1;

    // 8× oversampling to reduce ADC noise
    int32_t raw_sum = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0;
        adc_oneshot_read(s_adc, BATT_ADC_CHANNEL, &raw);
        raw_sum += raw;
    }
    int raw = (int)(raw_sum / 8);

    int mv = 0;
    if (s_cali) {
        adc_cali_raw_to_voltage(s_cali, raw, &mv);
    } else {
        // Approximate: 12-bit ADC, ~3100 mV full scale at DB_12
        mv = (raw * 3100) / 4095;
    }
    return mv;
}

int battery_get_percent(void)
{
    int mv = battery_get_mv();
    if (mv < 0) return -1;
    if (mv <= BATT_MV_EMPTY) return 0;
    if (mv >= BATT_MV_FULL)  return 100;
    return (int)(((long)(mv - BATT_MV_EMPTY) * 100) /
                 (BATT_MV_FULL - BATT_MV_EMPTY));
}

bool battery_is_available(void) { return s_ready; }
