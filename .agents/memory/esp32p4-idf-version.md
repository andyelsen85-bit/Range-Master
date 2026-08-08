---
name: ESP32-P4 IDF version constraints and linker bug
description: IDF version rules for ECO2, DSI hang fix, LDO power-up, and the sections.ld .sbss.* linker bug workaround
---

## IDF Version Rules (Guition JC8012P4A1C / ESP32-P4 rev v1.3 ECO2)

- **IDF 5.5.x** — refuses ECO2 entirely (requires v3.1+). Do not use.
- **IDF 5.4.x** — `esp_lcd_new_dsi_bus()` hangs on ECO2 D-PHY (IDF issue #17778). Do not use.
- **IDF 5.3.5** — USE THIS. Boots, DSI bus works after LDO fix below.

## D-PHY LDO (root cause of original DSI hang)

The MIPI DSI D-PHY has a dedicated internal 2.5 V LDO (channel 3) that is OFF by default. Without it the PLL has no supply and `esp_lcd_new_dsi_bus()` spins forever.

```cpp
#include "esp_ldo_regulator.h"   // IDF 5.3.x path (NOT esp_private/esp_ldo.h, NOT esp_ldo.h)
esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
esp_ldo_channel_config_t ldo_cfg = { .chan_id = 3, .voltage_mv = 2500 };
ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi_phy));
// keep ldo_mipi_phy alive for driver lifetime
```

Must be called BEFORE `esp_lcd_new_dsi_bus()`.

## DPI Panel API gotchas

- `esp_lcd_panel_reset(dpi_handle)` → ESP_ERR_NOT_SUPPORTED. Reset JD9365 via GPIO directly (GPIO 27 on Guition board).
- `esp_lcd_panel_mirror(dpi_handle, ...)` → ESP_ERR_NOT_SUPPORTED. Rotation handled by LVGL.
- `esp_lcd_panel_disp_on_off(dpi_handle, ...)` → ESP_ERR_NOT_SUPPORTED. Display-on handled by backlight PWM.

## LVGL init order

`lv_init()` must be called before `lv_display_create()` / any LVGL API. Missing this causes a load access fault in LVGL's TLSF allocator.

## sdkconfig.defaults for ESP32-P4

- `CONFIG_SPIRAM_MODE_OCT` and `CONFIG_SPIRAM_SPEED_80M` are **not valid Kconfig symbols** on ESP32-P4. IDF auto-detects HEX PSRAM. Remove them.
- `CONFIG_FREERTOS_USE_TICKLESS_IDLE` is a bool — use `=n` not `=0`.
- `CONFIG_SPIRAM_FETCH_INSTRUCTIONS` / `CONFIG_SPIRAM_RODATA` do not exist for ESP32-P4; setting them has no effect.

## --enable-non-contiguous-regions linker bug (IDF 5.3.x)

**Root cause:** On ESP32-P4+PSRAM, IDF unconditionally injects `-Wl,--enable-non-contiguous-regions`. Under this flag the linker discards any section not explicitly placed in the linker script. IDF 5.3.x `sections.ld.in` for ESP32-P4 is missing `*(.sbss.*)` and `*(.bss.*)` wildcard entries (fixed in IDF 5.4.x).

**Symptom:** Link fails with ~70 KB of `error: --enable-non-contiguous-regions discards section .sbss.XXX` from every IDF component.

**What does NOT work:**
- `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=n` / `CONFIG_SPIRAM_RODATA=n` in sdkconfig.defaults (symbols don't exist for P4)
- CMake `get_target_property` / `set_target_properties` to strip the flag from `esp_psram` (flag is injected differently in this IDF version)
- Linker fragment `.lf` files — the IDF 5.3.x fragment parser on this toolchain rejects `[sections:]` blocks

**What works:** A Python `PRE_LINK` hook (`tools/patch_sections_ld.py`) that patches the generated `sections.ld` immediately before the link step, inserting `*(.sbss.*)` after `*(.sbss )` and `*(.bss.*)` after `*(.bss )`. Registered in top-level `CMakeLists.txt` using `add_custom_command(TARGET ${CMAKE_PROJECT_NAME}.elf PRE_LINK ...)`.
