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
#include "gsl3680_firmware.h"

static const char *TAG = "gsl3680";

// ── GSL3680 registers ────────────────────────────────────────
#define GSL3680_REG_STATUS   0x80
#define GSL3680_REG_TOUCH    0x84   // 4 bytes per touch point
#define GSL3680_MAX_POINTS   5

// ── Firmware blob helpers ────────────────────────────────────
// Low-level I²C write: [reg, b0, b1, b2, b3]
static inline void gsl_i2c_write(uint8_t reg, uint8_t b0, uint8_t b1,
                                  uint8_t b2, uint8_t b3, uint8_t len)
{
    uint8_t buf[5] = {reg, b0, b1, b2, b3};
    i2c_master_write_to_device(TOUCH_I2C_PORT, TOUCH_I2C_ADDR,
                               buf, 1 + len, pdMS_TO_TICKS(50));
}

// Clear IC registers before firmware load (puts MCU in known state)
static void gsl3680_clear_reg(void)
{
    gsl_i2c_write(0xE0, 0x88, 0, 0, 0, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gsl_i2c_write(0x88, 0x01, 0, 0, 0, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    gsl_i2c_write(0xE4, 0x04, 0, 0, 0, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    gsl_i2c_write(0xE0, 0x00, 0, 0, 0, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
}

// Reset IC over I²C (GPIO reset is handled in gsl3680_touch_init)
static void gsl3680_reset_chip(void)
{
    gsl_i2c_write(0xE0, 0x88, 0, 0, 0, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gsl_i2c_write(0xE4, 0x04, 0, 0, 0, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gsl_i2c_write(0xBC, 0x00, 0x00, 0x00, 0x00, 4);
    vTaskDelay(pdMS_TO_TICKS(10));
}

// Send the startup command that tells the IC to begin running firmware
static void gsl3680_startup_chip(void)
{
    gsl_i2c_write(0xE0, 0x00, 0, 0, 0, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

// Upload the full DPRAM firmware blob from gsl3680_firmware.h.
// Each entry with addr == 0xf0 selects a DPRAM page (write 1 byte).
// Every other entry writes 4 bytes (little-endian uint32) into that page.
static void gsl3680_load_firmware(void)
{
    ESP_LOGI(TAG, "Loading GSL3680 firmware (%u entries)...",
             (unsigned)(sizeof(GSLX680_FW) / sizeof(GSLX680_FW[0])));

    // Step 1: clear registers and do an I²C-level reset
    gsl3680_clear_reg();
    gsl3680_reset_chip();

    // Step 2: stream every entry in the firmware table
    uint32_t n = sizeof(GSLX680_FW) / sizeof(GSLX680_FW[0]);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t  addr = GSLX680_FW[i].addr;
        uint32_t val  = GSLX680_FW[i].val;
        uint8_t  b0   = (uint8_t)( val        & 0xFF);
        uint8_t  b1   = (uint8_t)((val >>  8) & 0xFF);
        uint8_t  b2   = (uint8_t)((val >> 16) & 0xFF);
        uint8_t  b3   = (uint8_t)((val >> 24) & 0xFF);

        if (addr == 0xF0) {
            // Page-select: only the low byte is written
            gsl_i2c_write(addr, b0, 0, 0, 0, 1);
        } else {
            gsl_i2c_write(addr, b0, b1, b2, b3, 4);
        }
    }

    // Step 3: start the chip
    gsl3680_startup_chip();

    ESP_LOGI(TAG, "GSL3680 firmware upload complete");
}

// ── LVGL input read callback ─────────────────────────────────
static void gsl3680_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    // ── Diagnostics — log the first 20 status reads unconditionally,
    // then once every 500 calls, so the user can paste the monitor
    // output and we can diagnose I²C failures or missing touch data.
    static uint32_t s_call = 0;
    ++s_call;
    bool log_status = (s_call <= 20) || (s_call % 500 == 0);

    // Read touch count / status register
    uint8_t reg = GSL3680_REG_STATUS;
    uint8_t status[4] = {0};
    esp_err_t err = i2c_master_write_read_device(
        TOUCH_I2C_PORT, TOUCH_I2C_ADDR,
        &reg, 1, status, 4, pdMS_TO_TICKS(10));

    if (log_status) {
        ESP_LOGI(TAG, "[%lu] stat i2c=%s  raw=[%02x %02x %02x %02x]",
                 (unsigned long)s_call,
                 err == ESP_OK ? "OK" : esp_err_to_name(err),
                 status[0], status[1], status[2], status[3]);
    }

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

    // Read first touch point (4 bytes at register 0x84)
    //   tp[0] = y_lo,  tp[1] = finger_id[7:4] | y_hi[3:0]
    //   tp[2] = x_lo,  tp[3] = 0000            | x_hi[3:0]
    uint8_t tp_reg = GSL3680_REG_TOUCH;
    uint8_t tp[4] = {0};
    err = i2c_master_write_read_device(
        TOUCH_I2C_PORT, TOUCH_I2C_ADDR,
        &tp_reg, 1, tp, 4, pdMS_TO_TICKS(10));

    if (err != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    // GSL3680 firmware reports in 12-bit virtual space (0..4095) regardless
    // of the physical panel size.  Scale to physical panel pixels first.
    uint16_t raw_x = ((tp[3] & 0x0F) << 8) | tp[2];   // 0..4095
    uint16_t raw_y = ((tp[1] & 0x0F) << 8) | tp[0];   // 0..4095

    // Scale to physical portrait pixels (round to nearest)
    uint32_t phys_x = ((uint32_t)raw_x * DISPLAY_H_RES + 2048) / 4096; // 0..799
    uint32_t phys_y = ((uint32_t)raw_y * DISPLAY_V_RES + 2048) / 4096; // 0..1279

    // Log every actual touch event (first 30, then every 50th)
    static uint32_t s_touch = 0;
    ++s_touch;
    if (s_touch <= 30 || s_touch % 50 == 0) {
        ESP_LOGI(TAG, "TOUCH #%lu  tp=[%02x %02x %02x %02x]  "
                      "raw(%u,%u) -> phys(%lu,%lu)  cnt=%u",
                 (unsigned long)s_touch,
                 tp[0], tp[1], tp[2], tp[3],
                 (unsigned)raw_x, (unsigned)raw_y,
                 (unsigned long)phys_x, (unsigned long)phys_y,
                 (unsigned)count);
    }

    // Rotate 90° CCW: physical portrait (phys_x, phys_y) → logical landscape
    //   logical_x = phys_y            (0..1279)
    //   logical_y = (H_RES-1) - phys_x  (0..799)
    int32_t lx = (int32_t)phys_y;
    int32_t ly = (int32_t)(DISPLAY_H_RES - 1 - phys_x);

    // Clamp to logical display
    if (lx < 0) lx = 0;
    if (lx >= DISPLAY_LOGICAL_W) lx = DISPLAY_LOGICAL_W - 1;
    if (ly < 0) ly = 0;
    if (ly >= DISPLAY_LOGICAL_H) ly = DISPLAY_LOGICAL_H - 1;

    data->point.x = lx;
    data->point.y = ly;
    data->state   = LV_INDEV_STATE_PRESSED;
}

// ── Public init ──────────────────────────────────────────────
void gsl3680_touch_init(lv_display_t *disp)
{
    ESP_LOGI(TAG, "Initialising GSL3680 touch on I²C%d SCL=%d SDA=%d",
             TOUCH_I2C_PORT, TOUCH_I2C_SCL, TOUCH_I2C_SDA);

    // Reset pin
    gpio_config_t rst_cfg = {};
    rst_cfg.pin_bit_mask = 1ULL << TOUCH_RESET_PIN;
    rst_cfg.mode         = GPIO_MODE_OUTPUT;
    rst_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    rst_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rst_cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&rst_cfg);
    gpio_set_level(TOUCH_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TOUCH_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(20));

    // I²C master init — use assignment style to avoid C++ nested-designator error
    i2c_config_t i2c_cfg = {};
    i2c_cfg.mode           = I2C_MODE_MASTER;
    i2c_cfg.sda_io_num     = TOUCH_I2C_SDA;
    i2c_cfg.scl_io_num     = TOUCH_I2C_SCL;
    i2c_cfg.sda_pullup_en  = GPIO_PULLUP_ENABLE;
    i2c_cfg.scl_pullup_en  = GPIO_PULLUP_ENABLE;
    i2c_cfg.master.clk_speed = TOUCH_I2C_FREQ;
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
