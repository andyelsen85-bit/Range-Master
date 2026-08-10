---
name: esp-hosted Kconfig structure (2.12.12)
description: Verified Kconfig symbol names and structure for espressif/esp_hosted 2.12.12 host-side SDIO config on ESP32-P4
---

## Rule
esp_hosted 2.12.12 has no top-level SDIO pin Kconfig symbols — `grep "^config"` only finds `ESP_HOSTED_ENABLED`. All transport/pin symbols are nested inside menus/choices and only appear when their dependencies are satisfied. Do not grep with `^config` — use `grep -rn "config ESP_HOSTED"` instead.

**Why:** The first attempt at `grep -rn "^config "` missed all nested symbols, leading to multiple iterations of wrong symbol names.

## Verified symbol names (SDIO, host side)

| Purpose | Kconfig symbol | Notes |
|---|---|---|
| Enable | `ESP_HOSTED_ENABLED` | auto-set when `ESP_WIFI_REMOTE_LIBRARY_HOSTED=y` |
| Slave target | `SLAVE_IDF_TARGET_ESP32C6` | set by esp_wifi_remote; explicit in sdkconfig.defaults is safe |
| Transport choice | `ESP_HOSTED_SDIO_HOST_INTERFACE` | valid nested choice item |
| Slot selection | `ESP_HOSTED_SDIO_SLOT_1` | Slot 0 has fixed IOMUX on P4 |
| CLK pin | `ESP_HOSTED_PRIV_SDIO_PIN_CLK_SLOT_1` | int, range-guarded |
| CMD pin | `ESP_HOSTED_PRIV_SDIO_PIN_CMD_SLOT_1` | int, range-guarded |
| D0 pin | `ESP_HOSTED_PRIV_SDIO_PIN_D0_SLOT_1` | int, range-guarded |
| D1 pin | `ESP_HOSTED_PRIV_SDIO_PIN_D1_4BIT_BUS_SLOT_1` | int, 4-bit mode |
| D2 pin | `ESP_HOSTED_PRIV_SDIO_PIN_D2_4BIT_BUS_SLOT_1` | int, 4-bit mode |
| D3 pin | `ESP_HOSTED_PRIV_SDIO_PIN_D3_4BIT_BUS_SLOT_1` | int, 4-bit mode |
| Reset GPIO | `ESP_HOSTED_SDIO_GPIO_RESET_SLAVE` | int |
| Clock freq | `ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ` | default 40000; use 20000 to start |

## P4 Slot 1 defaults match JC8012P4A1C board wiring

All ESP32-P4 Slot 1 defaults in esp_hosted 2.12.12 match the Guition board:
- CLK=18, CMD=19, D0=14, D1=15, D2=16, D3=17, RST=54

**No pin overrides needed in sdkconfig.defaults.**

**How to apply:** If a future board revision changes any SDIO pin, use `CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_<X>_SLOT_1=<gpio>` in sdkconfig.defaults. Slot 1 range validators are set by the slave target choice, so setting them outside the valid range causes a Kconfig range error.

## CMakeLists.txt requirement
`esp_wifi`, `esp_netif`, `esp_event` must be in `PRIV_REQUIRES` of the `main` component. They are not auto-propagated through esp_wifi_remote.
