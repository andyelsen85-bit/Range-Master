// ============================================================
// GSL3680 capacitive touch driver (I²C → LVGL indev)
//
// Touch reading is decoupled from LVGL rendering:
//   touch_task  (priority 6, core 1) — polls I²C at 100 Hz,
//               writes s_cached_* under a mutex.
//   gsl3680_read_cb — called by lv_timer_handler(); reads the
//               cache only (no I²C, no blocking wait), so it
//               returns instantly even while PPA flush is running
//               on the same core.
// ============================================================
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
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

// ── Shared touch cache ────────────────────────────────────────
// Written exclusively by touch_task.
// Read exclusively by gsl3680_read_cb (LVGL context).
// Mutex ensures a consistent x+y+pressed triple every read.
static SemaphoreHandle_t s_touch_mux     = NULL;
static int32_t           s_cached_x      = 0;
static int32_t           s_cached_y      = 0;
static bool              s_cached_pressed = false;

// ── Firmware blob helpers ────────────────────────────────────
static inline void gsl_i2c_write(uint8_t reg, uint8_t b0, uint8_t b1,
                                  uint8_t b2, uint8_t b3, uint8_t len)
{
    uint8_t buf[5] = {reg, b0, b1, b2, b3};
    i2c_master_write_to_device(TOUCH_I2C_PORT, TOUCH_I2C_ADDR,
                               buf, 1 + len, pdMS_TO_TICKS(50));
}

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

static void gsl3680_reset_chip(void)
{
    gsl_i2c_write(0xE0, 0x88, 0, 0, 0, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gsl_i2c_write(0xE4, 0x04, 0, 0, 0, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gsl_i2c_write(0xBC, 0x00, 0x00, 0x00, 0x00, 4);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void gsl3680_startup_chip(void)
{
    gsl_i2c_write(0xE0, 0x00, 0, 0, 0, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void gsl3680_load_firmware(void)
{
    ESP_LOGI(TAG, "Loading GSL3680 firmware (%u entries)...",
             (unsigned)(sizeof(GSLX680_FW) / sizeof(GSLX680_FW[0])));

    gsl3680_clear_reg();
    gsl3680_reset_chip();

    uint32_t n = sizeof(GSLX680_FW) / sizeof(GSLX680_FW[0]);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t  addr = GSLX680_FW[i].addr;
        uint32_t val  = GSLX680_FW[i].val;
        uint8_t  b0   = (uint8_t)( val        & 0xFF);
        uint8_t  b1   = (uint8_t)((val >>  8) & 0xFF);
        uint8_t  b2   = (uint8_t)((val >> 16) & 0xFF);
        uint8_t  b3   = (uint8_t)((val >> 24) & 0xFF);

        if (addr == 0xF0) {
            gsl_i2c_write(addr, b0, 0, 0, 0, 1);
        } else {
            gsl_i2c_write(addr, b0, b1, b2, b3, 4);
        }
    }

    gsl3680_startup_chip();
    ESP_LOGI(TAG, "GSL3680 firmware upload complete");
}

// ── Raw I²C read — called ONLY from touch_task ───────────────
// Reads one touch point, converts to physical coords, updates cache.
static void gsl3680_hw_read(void)
{
    // Status register
    uint8_t reg    = GSL3680_REG_STATUS;
    uint8_t status[4] = {0};
    esp_err_t err = i2c_master_write_read_device(
        TOUCH_I2C_PORT, TOUCH_I2C_ADDR,
        &reg, 1, status, 4, pdMS_TO_TICKS(10));

    if (err != ESP_OK || (status[0] & 0x0F) == 0) {
        // No touch or I²C error — mark released
        xSemaphoreTake(s_touch_mux, portMAX_DELAY);
        s_cached_pressed = false;
        xSemaphoreGive(s_touch_mux);
        return;
    }

    // Touch point register
    uint8_t tp_reg = GSL3680_REG_TOUCH;
    uint8_t tp[4]  = {0};
    err = i2c_master_write_read_device(
        TOUCH_I2C_PORT, TOUCH_I2C_ADDR,
        &tp_reg, 1, tp, 4, pdMS_TO_TICKS(10));

    if (err != ESP_OK) {
        xSemaphoreTake(s_touch_mux, portMAX_DELAY);
        s_cached_pressed = false;
        xSemaphoreGive(s_touch_mux);
        return;
    }

    uint16_t raw_x = ((tp[3] & 0x0F) << 8) | tp[2];
    uint16_t raw_y = ((tp[1] & 0x0F) << 8) | tp[0];

    // Convert raw sensor axes → physical panel coordinates.
    // Sensor was calibrated in landscape; raw_x → phys_y, raw_y → phys_x.
    // Calibrated edge ranges: raw_x ∈ [22..1647], raw_y ∈ [19..883]
#define TOUCH_RAW_X_MIN  22
#define TOUCH_RAW_X_MAX  1647
#define TOUCH_RAW_Y_MIN  19
#define TOUCH_RAW_Y_MAX  883
    int32_t phys_x = (int32_t)((raw_y - TOUCH_RAW_Y_MIN) *
                     (DISPLAY_H_RES - 1) / (TOUCH_RAW_Y_MAX - TOUCH_RAW_Y_MIN));
    int32_t phys_y = (int32_t)((TOUCH_RAW_X_MAX - raw_x) *
                     (DISPLAY_V_RES - 1) / (TOUCH_RAW_X_MAX - TOUCH_RAW_X_MIN));

    if (phys_x < 0)              phys_x = 0;
    if (phys_x >= DISPLAY_H_RES) phys_x = DISPLAY_H_RES - 1;
    if (phys_y < 0)              phys_y = 0;
    if (phys_y >= DISPLAY_V_RES) phys_y = DISPLAY_V_RES - 1;

    // Periodic logging for calibration verification
    static uint32_t s_touch = 0;
    ++s_touch;
    if (s_touch <= 50 || s_touch % 50 == 0) {
        ESP_LOGI(TAG, "TOUCH #%lu  raw(%u,%u) -> phys(%ld,%ld)",
                 (unsigned long)s_touch,
                 (unsigned)raw_x, (unsigned)raw_y,
                 (long)phys_x, (long)phys_y);
    }

    xSemaphoreTake(s_touch_mux, portMAX_DELAY);
    s_cached_x       = phys_x;
    s_cached_y       = phys_y;
    s_cached_pressed = true;
    xSemaphoreGive(s_touch_mux);
}

// ── Touch polling task ────────────────────────────────────────
// Runs at priority 6 (above lvgl_task at 5) on core 1.
// PPA_TRANS_MODE_BLOCKING internally waits on a semaphore — so this
// task CAN preempt the LVGL task during a flush and keep touch data
// fresh even while the display pipeline is busy.
static void touch_task(void *arg)
{
    for (;;) {
        gsl3680_hw_read();
        vTaskDelay(pdMS_TO_TICKS(10));   // 100 Hz polling
    }
}

// ── LVGL indev callback — cache read only, no I²C ────────────
// Called by lv_timer_handler() on the LVGL task.
// Returns instantly; never blocks on I²C or hardware.
static void gsl3680_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    xSemaphoreTake(s_touch_mux, portMAX_DELAY);
    data->point.x = s_cached_x;
    data->point.y = s_cached_y;
    data->state   = s_cached_pressed
                    ? LV_INDEV_STATE_PRESSED
                    : LV_INDEV_STATE_RELEASED;
    xSemaphoreGive(s_touch_mux);
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

    // I²C master
    i2c_config_t i2c_cfg = {};
    i2c_cfg.mode             = I2C_MODE_MASTER;
    i2c_cfg.sda_io_num       = TOUCH_I2C_SDA;
    i2c_cfg.scl_io_num       = TOUCH_I2C_SCL;
    i2c_cfg.sda_pullup_en    = GPIO_PULLUP_ENABLE;
    i2c_cfg.scl_pullup_en    = GPIO_PULLUP_ENABLE;
    i2c_cfg.master.clk_speed = TOUCH_I2C_FREQ;
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_I2C_PORT, &i2c_cfg));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_I2C_PORT, I2C_MODE_MASTER,
                                       0, 0, 0));

    gsl3680_load_firmware();
    ESP_LOGI(TAG, "GSL3680 ready");

    // Create the shared mutex before starting the task
    s_touch_mux = xSemaphoreCreateMutex();
    configASSERT(s_touch_mux);

    // Touch polling task — higher priority than LVGL so it can preempt
    // the render task during blocking PPA operations
    xTaskCreatePinnedToCore(
        touch_task,
        "touch_poll",
        2048,    // 2 KB stack — only I²C reads and a mutex
        NULL,
        6,       // priority 6 > lvgl_task priority 5
        NULL,
        1        // core 1 — same as lvgl_task; preempts during PPA waits
    );
    ESP_LOGI(TAG, "touch_poll task started (100 Hz, prio 6, core 1)");

    // Register LVGL indev — read_cb now reads cache, not hardware
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, gsl3680_read_cb);
    lv_indev_set_display(indev, disp);
}
