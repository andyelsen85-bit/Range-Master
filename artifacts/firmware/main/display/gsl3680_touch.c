// ============================================================
// GSL3680 capacitive touch driver (I²C → LVGL indev)
// ============================================================
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "lvgl.h"
#include "app_config.h"
#include "gsl3680_touch.h"

static const char *TAG = "gsl3680";

// ── GSL3680 registers ────────────────────────────────────────
#define GSL3680_REG_STATUS   0x80
#define GSL3680_REG_TOUCH    0x84   // 4 bytes per touch point
#define GSL3680_MAX_POINTS   5

// ── Firmware blob helpers ────────────────────────────────────
// The GSL3680 needs a firmware upload on power-on.
// A minimal stub that writes the "start" command is used here.
// Production builds should include the full 0x6500-byte blob.
static void gsl3680_load_firmware(void)
{
    // 1. Reset
    uint8_t buf[4] = {0x88};
    i2c_master_write_to_device(TOUCH_I2C_PORT, TOUCH_I2C_ADDR,
                               buf, 1, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(10));

    // 2. Clear register
    uint8_t clear[2] = {0xE0, 0x00};
    i2c_master_write_to_device(TOUCH_I2C_PORT, TOUCH_I2C_ADDR,
                               clear, 2, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(5));

    // 3. Start
    uint8_t start[2] = {0xE4, 0x04};
    i2c_master_write_to_device(TOUCH_I2C_PORT, TOUCH_I2C_ADDR,
                               start, 2, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(5));
}

// ── LVGL input read callback ─────────────────────────────────
static void gsl3680_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    // Read touch count register
    uint8_t reg = GSL3680_REG_STATUS;
    uint8_t status[4] = {0};
    esp_err_t err = i2c_master_write_read_device(
        TOUCH_I2C_PORT, TOUCH_I2C_ADDR,
        &reg, 1, status, 4, pdMS_TO_TICKS(10));

    if (err != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint8_t count = status[0] & 0x0F;
    if (count == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    if (count > GSL3680_MAX_POINTS) count = GSL3680_MAX_POINTS;

    // Read first touch point (4 bytes: id, y_lo, x_hi, x_lo, y_hi)
    uint8_t tp_reg = GSL3680_REG_TOUCH;
    uint8_t tp[4] = {0};
    err = i2c_master_write_read_device(
        TOUCH_I2C_PORT, TOUCH_I2C_ADDR,
        &tp_reg, 1, tp, 4, pdMS_TO_TICKS(10));

    if (err != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    // GSL3680 raw coords are in panel-native orientation (800×1280)
    uint16_t raw_x = ((tp[3] & 0x0F) << 8) | tp[2];
    uint16_t raw_y = ((tp[1] & 0x0F) << 8) | tp[0];

    // Rotate 90° to logical landscape (1280×800):
    //   logical_x = raw_y
    //   logical_y = DISPLAY_H_RES - 1 - raw_x
    int32_t lx = (int32_t)raw_y;
    int32_t ly = (int32_t)(DISPLAY_H_RES - 1 - raw_x);

    // Clamp
    if (lx < 0) lx = 0;
    if (lx >= DISPLAY_LOGICAL_W) lx = DISPLAY_LOGICAL_W - 1;
    if (ly < 0) ly = 0;
    if (ly >= DISPLAY_LOGICAL_H) ly = DISPLAY_LOGICAL_H - 1;

    data->point.x = (int32_t)lx;
    data->point.y = (int32_t)ly;
    data->state   = LV_INDEV_STATE_PRESSED;
}

// ── Public init ──────────────────────────────────────────────
void gsl3680_touch_init(lv_display_t *disp)
{
    ESP_LOGI(TAG, "Initialising GSL3680 touch on I²C%d SCL=%d SDA=%d",
             TOUCH_I2C_PORT, TOUCH_I2C_SCL, TOUCH_I2C_SDA);

    // Reset pin
    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << TOUCH_RESET_PIN,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_cfg);
    gpio_set_level(TOUCH_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TOUCH_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    // I²C master init
    i2c_config_t i2c_cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = TOUCH_I2C_SDA,
        .scl_io_num       = TOUCH_I2C_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = TOUCH_I2C_FREQ,
    };
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_I2C_PORT, &i2c_cfg));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_I2C_PORT, I2C_MODE_MASTER,
                                       0, 0, 0));

    gsl3680_load_firmware();
    ESP_LOGI(TAG, "GSL3680 ready");

    // Register LVGL indev
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, gsl3680_read_cb);
    lv_indev_set_display(indev, disp);
}
