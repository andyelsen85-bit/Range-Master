// ============================================================
// battery.cpp — ADC-based battery level (Guition JC8012P4A1C)
//
// GPIO52 = ADC1 channel 3 on ESP32-P4.
// The IP5306 battery board uses a 1:1 voltage divider, so the
// ADC pin sees half the battery cell voltage:
//   Cell 3.0 V (empty) → ADC ~1500 mV
//   Cell 4.2 V (full)  → ADC ~2100 mV
//
// No calibration scheme is used — the esp_adc calibration API
// differs between targets and IDF versions.  A simple raw→mV
// linear mapping is accurate within ±50 mV, which is close
// enough for a percentage bar.  Use battery_get_mv() + a
// multimeter to tune BATT_MV_EMPTY / BATT_MV_FULL in app_config.h
// if the percentage looks wrong on your unit.
//
// Charging state cannot be read from firmware on this board.
// ============================================================
#include "battery.h"
#include "app_config.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t s_adc   = NULL;
static bool                      s_ready = false;

// ADC full-scale millivolts for ADC_ATTEN_DB_12 on ESP32-P4
// (12-bit, ~3100 mV full-scale).
#define ADC_FULL_SCALE_MV   3100
#define ADC_MAX_RAW         4095

bool battery_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id  = (adc_unit_t)BATT_ADC_UNIT_NUM;
    unit_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc_oneshot_new_unit failed: %s — battery unavailable",
                 esp_err_to_name(err));
        return false;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten    = ADC_ATTEN_DB_12;   // 0–3100 mV range; covers divider output
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;

    err = adc_oneshot_config_channel(s_adc,
                                     (adc_channel_t)BATT_ADC_CHANNEL_NUM,
                                     &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "adc_oneshot_config_channel failed: %s",
                 esp_err_to_name(err));
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        return false;
    }

    s_ready = true;
    ESP_LOGI(TAG, "Battery ADC ready: GPIO%d  ch=%d  "
                  "empty=%d mV  full=%d mV",
             (int)BATT_ADC_GPIO, BATT_ADC_CHANNEL_NUM,
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
        adc_oneshot_read(s_adc, (adc_channel_t)BATT_ADC_CHANNEL_NUM, &raw);
        raw_sum += raw;
    }
    int raw_avg = (int)(raw_sum / 8);
    // Linear raw → mV (no calibration; ±50 mV typical error)
    return (raw_avg * ADC_FULL_SCALE_MV) / ADC_MAX_RAW;
}

int battery_get_percent(void)
{
    int mv = battery_get_mv();
    if (mv < 0)            return -1;
    if (mv <= BATT_MV_EMPTY) return 0;
    if (mv >= BATT_MV_FULL)  return 100;
    return (int)(((long)(mv - BATT_MV_EMPTY) * 100) /
                 (BATT_MV_FULL - BATT_MV_EMPTY));
}

bool battery_is_available(void) { return s_ready; }
