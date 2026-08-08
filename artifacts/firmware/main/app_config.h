#pragma once
// ============================================================
// TrapMaster Firmware — Hardware configuration
// Board: Guition JC8012P4A1C-I-W-Y  (ESP32-P4 + ESP32-C6)
// ============================================================
#include "driver/gpio.h"   // GPIO_NUM_x constants needed by pin macros

// ── Display (MIPI DSI / JD9365) ─────────────────────────────
#define DISPLAY_H_RES           800
#define DISPLAY_V_RES           1280
#define LCD_RST_PIN             GPIO_NUM_27   // JD9365 hardware reset (active-low)
// Physical panel is portrait-native (800×1280); we rotate to landscape.
#define DISPLAY_ROTATION        LV_DISPLAY_ROTATION_90
// Logical resolution after rotation
#define DISPLAY_LOGICAL_W       1280
#define DISPLAY_LOGICAL_H       800

#define MIPI_DSI_LANE_NUM       2
#define MIPI_DSI_LANE_BIT_RATE  1000  // Mbps per lane — Guition JC8012P4A1C requires 1000, not 500

// ── Touch (GSL3680 / I²C) ───────────────────────────────────
#define TOUCH_I2C_PORT          I2C_NUM_0
#define TOUCH_I2C_SCL           GPIO_NUM_8
#define TOUCH_I2C_SDA           GPIO_NUM_7
#define TOUCH_I2C_ADDR          0x40        // GSL3680 default
#define TOUCH_RESET_PIN         GPIO_NUM_9
#define TOUCH_INT_PIN           GPIO_NUM_10
#define TOUCH_I2C_FREQ          400000

// ── Backlight PWM ───────────────────────────────────────────
#define LCD_BL_PIN              GPIO_NUM_26
#define LCD_BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define LCD_BL_LEDC_TIMER       LEDC_TIMER_0
#define LCD_BL_DUTY_MAX         1023

// ── UART to ESP32-C6 co-processor ───────────────────────────
#define C6_UART_PORT            UART_NUM_1
#define C6_UART_TX              GPIO_NUM_4
#define C6_UART_RX              GPIO_NUM_5
#define C6_UART_BAUD            115200
#define C6_UART_RX_BUF         4096
#define C6_AT_TIMEOUT_MS        8000

// ── UART for LoRa module (phase 2) ──────────────────────────
#define LORA_UART_PORT          UART_NUM_2
#define LORA_UART_TX            GPIO_NUM_17
#define LORA_UART_RX            GPIO_NUM_18
#define LORA_UART_BAUD          9600

// ── LVGL ────────────────────────────────────────────────────
#define LVGL_TICK_PERIOD_MS     2
#define LVGL_TASK_MAX_DELAY_MS  500
#define LVGL_BUF_LINES          40          // double-buffer each 40 rows

// ── NVS namespace ───────────────────────────────────────────
#define NVS_NAMESPACE           "trapmaster"

// ── Portal API ──────────────────────────────────────────────
#define DEFAULT_API_URL         "https://rangemaster.hostzone.lu"

// ── Misc ────────────────────────────────────────────────────
#define APP_VERSION             "1.0.0-phase1"
#define CLUB_NAME               "F.S.H.C.L. SEKTIOUN WOLZ"
