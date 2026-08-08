---
name: ESP32-P4 IDF version compatibility
description: IDF 5.5.x is incompatible with ESP32-P4 ECO2 (chip rev v1.3); must use IDF 5.4.x
---

## Rule
Always build the TrapMaster firmware with **IDF 5.3.x** (specifically 5.3.2). Do not use IDF 5.4.x or later.

**Why:** The Guition JC8012P4A1C board carries an ESP32-P4 revision v1.3 (ROM string: `esp32p4-eco2-20240710`, ECO2 silicon).

- **IDF 5.5.x+**: Compiles bootloader targeting ECO3 (v3.1+). Crashes immediately at `call_start_cpu0` with "Illegal instruction" on ECO2 silicon. Cannot be worked around.
- **IDF 5.4.2+**: Flashes and boots OK, but `esp_lcd_new_dsi_bus()` hangs forever — the DSI PHY PLL initialization sequence was changed in 5.4.2 in a way that breaks ECO2's D-PHY. This is confirmed by IDF GitHub issue #17778 (filed for V5.4.2 and V5.5). `CONFIG_ESP32P4_REV_MIN_FULL=100`, `use_dma2d=false`, lane rate changes (500/1000 Mbps) — none of these fix the hang.
- **IDF 5.3.x**: ECO2 was the ONLY available ESP32-P4 silicon when 5.3 was developed. All DSI PHY code was written and tested against ECO2. This is the correct version.

**How to apply:** Pin the firmware build to IDF 5.3.x. The Windows installer creates a separate `ESP-IDF 5.3 CMD` shortcut that coexists with other versions. Also revert any dma2d/lane_rate diagnostic changes before the final build.

## Root cause of esp_lcd_new_dsi_bus() hang — MISSING LDO

**This is the real fix.** On ESP32-P4, the MIPI DSI D-PHY sits behind a dedicated internal 2.5V LDO (channel 3) that is OFF by default. Without powering it on first, the PLL has no supply and can never lock — `esp_lcd_new_dsi_bus()` spins forever regardless of IDF version, lane rate, or clock config.

Add this immediately before `esp_lcd_new_dsi_bus()`:
```cpp
#include "esp_private/esp_ldo.h"
esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
esp_ldo_channel_config_t ldo_cfg = {};
ldo_cfg.chan_id    = 3;
ldo_cfg.voltage_mv = 2500;
ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi_phy));
```
Keep the handle alive for the lifetime of the driver — never release it. Every reference (CelliesProjects, profi-max, Espressif esp_lcd_jd9365, Waveshare) does this.

**Why:** `CONFIG_PM_ENABLE=y` and IDF version switching were red herrings — neither addresses the missing power rail. The `esp_perip_clk_init()` stub warning is unrelated noise.

## Additional required sdkconfig setting

`CONFIG_PM_ENABLE=y` must be set in `sdkconfig.defaults`. Without it, `esp_perip_clk_init()` is a no-op stub on ESP32-P4/IDF 5.3.x (warning: "has not been implemented yet"). The stub means the MIPI DSI D-PHY PLL reference clock gate (`MIPI_DSI_DPHY_PLL_REFCLK_EN` in `HP_SYS_CLKRST.PERI_CLK_CTRL03`) is never opened, so `esp_lcd_new_dsi_bus()` hangs forever waiting for PLL lock. Enabling PM triggers the full clock-init path. Also set `CONFIG_FREERTOS_USE_TICKLESS_IDLE=0` to prevent the scheduler from sleeping under the always-running LVGL display task.
