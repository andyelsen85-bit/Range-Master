---
name: ADC init order vs SDIO/esp_hosted
description: adc_oneshot_new_unit() releases GPIO pad holds, breaking SDIO CMD pullup set by esp_hosted — must init ADC after coprocessor_init()
---

## Rule
Always call `battery_init()` (or any `adc_oneshot_new_unit()` call) **after** `coprocessor_init()` on ESP32-P4 boards that use esp_hosted over SDIO.

## Why
`adc_oneshot_new_unit(ADC_UNIT_1)` acquires the ADC1 power domain. As a side effect it releases GPIO pad holds across the chip. esp_hosted's `add_esp_wifi_remote_channels()` sets GPIO19 (SDIO CMD) with `Pullup: 1` using a pad hold. When ADC1 unit init releases that hold, GPIO19 reverts to its default (Pullup: 0), making the C6 slave unresponsive to `sdmmc_card_init` — 15× retries then abort.

Observable symptom in the monitor:
```
gpio: GPIO[19]| Pullup: 1   ← set by H_API (correct)
...
gpio: GPIO[19]| Pullup: 0   ← cleared by adc_oneshot_new_unit (bad)
battery: Battery ADC ready
...
sdmmc_io_reset: unexpected return: 0x108  (×15)
H_SDIO_DRV: card init failed
abort() ... esp_wifi_init
```

## How to apply
In `main.cpp` app_main():
1. `coprocessor_init()` first — SDIO negotiated, WiFi stack up
2. `battery_init()` after — ADC1 power domain safe to acquire
