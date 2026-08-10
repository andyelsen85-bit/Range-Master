---
name: SPIRAM internal RAM exhaustion pattern
description: How SPIRAM_MALLOC_ALWAYSINTERNAL drains internal RAM via small LVGL/FreeRTOS/esp_hosted allocations, and the fix.
---

## Rule
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=512` (the ESP-IDF default) sends every
allocation ≤512 B to internal RAM first. This silently consumes the entire
internal RAM budget when many small objects accumulate at startup.

## Why this matters on this board
- `lv_obj_t` = 52 B → internal RAM
- LVGL styles, event callbacks, timers → all ≤512 B → internal RAM
- FreeRTOS event groups, queues, TCBs → ≤512 B → internal RAM
- esp_hosted RPC sync semaphore (allocated per `esp_wifi_scan_start` call) → internal RAM
- Result: after LVGL builds 10 screens + SDIO init, internal RAM drops from ~93 KB to ~52 bytes.
  Any further small allocation (scan semaphore, task TCB) fails silently or panics.

## Symptoms
- `xEventGroupCreate` returns NULL → `cop_wifi_scan` returns ESP_ERR_NO_MEM silently
- `xTaskCreate` / `xTaskCreateWithCaps` returns pdFAIL (TCB needs internal RAM)
- `sem create failed` + `assert: hosted_destroy_semaphore(semaphore_handle=0x0)` → panic
- `free internal = 52 bytes` logged at scan time

## Fix applied
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0` in sdkconfig.defaults
Routes ALL plain `malloc()` calls to PSRAM. DMA/ISR buffers use
`heap_caps_malloc(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)` explicitly — unaffected.

**Requires deleting `build/sdkconfig` and rebuilding** after changing this setting,
as the cached sdkconfig won't pick up the new default.

## Also applied (belt-and-suspenders)
- `s_scan_done_group` event group: pre-allocated in `coprocessor_init()` (not per-tap)
- Scan + connect worker tasks: pre-allocated in `screen_wifi_create_workers()` called
  from `ui_manager_init()` BEFORE any `screen_*_create()` call
- `scan_cb`: queue-send only (no allocation); `s_scan_busy` debounce prevents re-entry

## How to apply
Any future component that creates FreeRTOS primitives or small heap objects
at runtime (button callbacks, event handlers) will suffer the same failure
if ALWAYSINTERNAL is raised again. Keep it at 0.
