// ============================================================
// click_sound.cpp — ES8311 I²S touch-click feedback
// ============================================================
// Hardware path:
//   ESP32-P4  ──I²S──►  ES8311 DAC  ──analog──►  Amplifier  ──►  CN3 Speaker
//   ESP32-P4  ──I²C──►  ES8311 (codec config, shared with GSL3680 touch bus)
//
// I²C bus:   I2C_NUM_0 (GPIO7 SDA / GPIO8 SCL) — shared, legacy driver.
//            ES8311 init calls i2c_master_write_to_device() which is
//            thread-safe (internal mutex) with the touch task.
// I²S port:  I2S_NUM_0, ESP32-P4 is master (generates BCLK/WS).
//            ES8311 operates in slave mode (derives clocks from I²S bus).
//
// ⚠ GPIO PINS — verify against your board schematic!
//   The defines below are in app_config.h.  If there is no sound:
//   1. Check that I2S_MCLK_PIN / I2S_BCLK_PIN / I2S_WS_PIN / I2S_DOUT_PIN
//      match the ES8311 connections on your PCB.
//   2. Check that ES8311_I2C_ADDR (0x18 or 0x19) matches the ADDR pin tie.
//   3. If the board has a PA-enable GPIO (amp shutdown pin), set I2S_PA_PIN.
// ============================================================
#include "click_sound.h"
#include "app_config.h"
#include "game_store.h"

#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"        // legacy I²C — shared with touch driver
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "click";

// ── Tone parameters ───────────────────────────────────────────
#define CLICK_SAMPLE_RATE   16000   // Hz
#define CLICK_DURATION_MS   35      // ms
#define CLICK_FREQ_HZ       900     // tone frequency
#define CLICK_SAMPLES       (CLICK_SAMPLE_RATE * CLICK_DURATION_MS / 1000)  // 560 samples

// ── State ────────────────────────────────────────────────────
static i2s_chan_handle_t s_tx_chan   = NULL;
static QueueHandle_t     s_play_q   = NULL;
// Stereo (L+R) pre-generated PCM for the click tone
static int16_t           s_pcm[CLICK_SAMPLES * 2];
static bool              s_ready    = false;

// ── ES8311 I²C helpers ────────────────────────────────────────
// Resolved at probe time; ES8311 ADDR pin low → 0x18, high → 0x19.
static uint8_t s_es8311_addr = ES8311_I2C_ADDR;

// Write one ES8311 register; uses the legacy I²C driver already
// initialised by gsl3680_touch_init().
static esp_err_t es8311_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(
               TOUCH_I2C_PORT, s_es8311_addr,
               buf, sizeof(buf),
               pdMS_TO_TICKS(100));
}

// Probe ES8311 by reading one byte from candidate addresses.
// A zero-byte write is unreliable with the legacy I²C driver (some IDF
// versions do not place the address on the bus when data_len == 0).
// A 1-byte read forces a full start + address + read cycle.
// Try 0x18 (ADDR pin low) then 0x19 (ADDR pin high).
static bool es8311_probe(void)
{
    static const uint8_t candidates[] = { 0x18, 0x19 };
    for (uint8_t addr : candidates) {
        uint8_t dummy;
        esp_err_t r = i2c_master_read_from_device(
                          TOUCH_I2C_PORT, addr,
                          &dummy, 1,
                          pdMS_TO_TICKS(50));
        if (r == ESP_OK) {
            s_es8311_addr = addr;
            ESP_LOGI("click", "ES8311 found at I²C addr 0x%02X", addr);
            return true;
        }
    }
    return false;
}

// ── ES8311 DAC initialisation ────────────────────────────────
// Configures ES8311 in I²S slave mode, 16 kHz, 16-bit, stereo output.
// MCLK is derived internally from BCLK (no separate MCLK wire required,
// though the I²S controller still outputs one on I2S_MCLK_PIN).
//
// Register map references:
//   Everest ES8311 datasheet (rev 1.4) + Espressif esp-codec-dev es8311.c
static esp_err_t es8311_dac_init(void)
{
    if (!es8311_probe()) {
        ESP_LOGW(TAG, "ES8311 not found on I²C addr 0x%02X — click sound disabled",
                 ES8311_I2C_ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "ES8311 found — initialising DAC");

    // ── Reset ─────────────────────────────────────────────────
    es8311_write(0x00, 0x1F);                   // chip reset
    vTaskDelay(pdMS_TO_TICKS(20));
    es8311_write(0x00, 0x00);                   // normal operation

    // ── Clock ─────────────────────────────────────────────────
    // 0x01 = 0x30: slave mode, use BCLK internally to synthesise MCLK
    es8311_write(0x01, 0x30);
    es8311_write(0x02, 0x00);                   // ADC OSR ÷1
    es8311_write(0x0D, 0x01);                   // SCLK divider = 1
    es8311_write(0x0E, 0x02);                   // BCLK prescale ÷2
    es8311_write(0x0F, 0x44);                   // DAC + ADC LRCK on

    // ── I²S format: 16-bit standard (Philips) ─────────────────
    // Reg 0x14 bits[3:2] = word length (11 = 16-bit)
    //          bits[1:0] = format      (00 = I²S / Philips)
    // → 0x0C = 0b00001100
    es8311_write(0x14, 0x0C);

    // ── ADC: power down (microphone not needed) ───────────────
    es8311_write(0x16, 0x24);                   // ADC digital power off
    es8311_write(0x17, 0x00);                   // ADC analogue off

    // ── DAC: enable ───────────────────────────────────────────
    es8311_write(0x37, 0x08);                   // DAC OSR = 128
    es8311_write(0x32, 0x00);                   // DAC digital volume  = 0 dB

    // ── Analogue output ───────────────────────────────────────
    // 0x45 = DAC analogue volume; 0xBF ≈ 0 dB (full-scale)
    es8311_write(0x45, 0xBF);

    // ── Enable all active clocks ──────────────────────────────
    es8311_write(0x0F, 0xFF);

    // ── PA enable (if wired) ──────────────────────────────────
#if defined(I2S_PA_PIN) && I2S_PA_PIN >= 0
    gpio_config_t pa = {
        .pin_bit_mask  = (1ULL << I2S_PA_PIN),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
        .hys_ctrl_mode = GPIO_HYS_SOFT_ENABLE,
    };
    gpio_config(&pa);
    gpio_set_level((gpio_num_t)I2S_PA_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
#endif

    ESP_LOGI(TAG, "ES8311 DAC init OK");
    return ESP_OK;
}

// ── Pre-generate click PCM ────────────────────────────────────
// Cosine-envelope over a sine wave → soft attack/decay click sound.
static void gen_click_pcm(void)
{
    for (int i = 0; i < CLICK_SAMPLES; i++) {
        float phase = (float)i / (float)CLICK_SAMPLES;
        // Hann window (smooth bell envelope)
        float env    = 0.5f * (1.0f - cosf(phase * 2.0f * 3.14159265f));
        float sample = env * 24000.0f *
                       sinf(phase * (float)CLICK_SAMPLES * 2.0f * 3.14159265f
                            * (float)CLICK_FREQ_HZ / (float)CLICK_SAMPLE_RATE);
        int16_t s = (int16_t)sample;
        s_pcm[i * 2]     = s;   // L
        s_pcm[i * 2 + 1] = s;   // R
    }
}

// ── Playback task ─────────────────────────────────────────────
static void click_task(void *arg)
{
    uint8_t cmd;
    while (1) {
        if (xQueueReceive(s_play_q, &cmd, portMAX_DELAY) == pdTRUE && s_ready) {
            size_t written = 0;
            i2s_channel_write(s_tx_chan,
                              s_pcm, sizeof(s_pcm),
                              &written,
                              pdMS_TO_TICKS(200));
        }
    }
}

// ── Public API ────────────────────────────────────────────────
void click_sound_init(void)
{
    // Pre-generate PCM before starting I²S (fast, no allocations).
    gen_click_pcm();

    // ── I²S channel ───────────────────────────────────────────
    i2s_chan_config_t ch_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ch_cfg.auto_clear = true;
    if (i2s_new_channel(&ch_cfg, &s_tx_chan, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed");
        return;
    }

    // Standard Philips I²S: 16 kHz, 16-bit, stereo
    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(CLICK_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)I2S_MCLK_PIN,
            .bclk = (gpio_num_t)I2S_BCLK_PIN,
            .ws   = (gpio_num_t)I2S_WS_PIN,
            .dout = (gpio_num_t)I2S_DOUT_PIN,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {},  // no pin inversions
        },
    };
    // MCLK = 256 × Fs = 4.096 MHz (ES8311 configured to derive from BCLK
    // internally, but we still output it on the MCLK pin for completeness).
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    if (i2s_channel_init_std_mode(s_tx_chan, &std_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed");
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return;
    }
    if (i2s_channel_enable(s_tx_chan) != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed");
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return;
    }

    // ── ES8311 codec ──────────────────────────────────────────
    if (es8311_dac_init() != ESP_OK) {
        // No codec found — release I²S and stay silent.
        i2s_channel_disable(s_tx_chan);
        i2s_del_channel(s_tx_chan);
        s_tx_chan = NULL;
        return;
    }

    // ── Background playback task ───────────────────────────────
    s_play_q = xQueueCreate(1, sizeof(uint8_t));
    xTaskCreate(click_task, "click_snd", 2560, NULL, 3, NULL);

    s_ready = true;
    ESP_LOGI(TAG, "Click sound ready — %d Hz tone, %d ms, I2S_NUM_0",
             CLICK_FREQ_HZ, CLICK_DURATION_MS);
}

void click_sound_play_if_enabled(void)
{
    if (!s_ready || !g_store.clickSoundEnabled) return;
    uint8_t cmd = 1;
    // xQueueOverwrite: if a click is already queued, replace it
    // (don't stack up clicks faster than they can play).
    xQueueOverwrite(s_play_q, &cmd);
}
