---
name: ES8311 audio codec — JC8012P4A1C
description: Audio hardware details and I2S/I2C wiring for click-sound on the Guition JC8012P4A1C board
---

## ES8311 presence confirmed
The JC8012P4A1C board has an ES8311 mono audio codec wired to speaker connector CN3.
Source: profi-max/JC8012P4A1_BSP_ESP32P4 README (board description + BSP topics listing es8311).

## I2C
- Address: **0x18** (ADDR pin low — standard for this board class)
- Port: **I2C_NUM_0**, shared with GSL3680 touch (GPIO7=SDA, GPIO8=SCL)
- Driver: legacy I2C API (`i2c_master_write_to_device`) — safe to share; internal mutex

## I2S pin assignment (BEST GUESS — unconfirmed from schematic)
| Signal | GPIO |
|--------|------|
| MCLK   | 13   |
| BCLK   | 12   |
| WS     | 11   |
| DOUT   | 5    |

These GPIOs are free (no conflict with display/touch/SDIO/backlight pins).
If there is no sound, check the PCB silkscreen near the ES8311 chip and compare with the
Guition schematic PDF or the p1ngb4ck/unofficial_guition_esp32p4_repo.

## PA enable pin
Unknown / assumed not present (I2S_PA_PIN = -1).
If the amplifier has a shutdown pin, set I2S_PA_PIN to its GPIO number in app_config.h.

## ES8311 init notes
- Use I2S slave mode on ES8311 (reg 0x01 = 0x30): ES8311 derives all clocks from BCLK.
- Register 0x14 = 0x0C: 16-bit I2S Philips format.
- ADC powered down (not needed for click playback): reg 0x16 = 0x24.
- `esp_codec_dev` component NOT used — it requires new-style I2C bus API which
  conflicts with the legacy driver already held by gsl3680_touch.cpp.
  Raw i2c_master_write_to_device() calls used instead.

**Why:** Touch driver uses legacy I2C API (i2c_param_config + i2c_driver_install on I2C_NUM_0).
New-style API (i2c_new_master_bus) on the same port would conflict. Legacy API is thread-safe via internal mutex.

## I2S driver
- IDF 5.x new API (`driver/i2s_std.h`): `i2s_channel_init_std_mode`
- Port: I2S_NUM_0 (no other I2S users in firmware)
- Format: Philips, 16-bit stereo, 16 kHz
- MCLK multiple: I2S_MCLK_MULTIPLE_256

## Implementation
- `main/audio/click_sound.h` / `click_sound.cpp`
- Tone: 900 Hz Hann-windowed sine, 35 ms, pre-generated at boot
- Non-blocking: background FreeRTOS task + xQueueOverwrite
- Graceful degradation: if ES8311 not detected, click sound silently disabled
- Toggle stored in NVS as "click_snd"; exposed in Settings → System tab
- Touch hook: RELEASED→PRESSED transition in gsl3680_read_cb()
