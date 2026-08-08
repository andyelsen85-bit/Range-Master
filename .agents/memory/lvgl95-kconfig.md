---
name: LVGL 9.5.0 Kconfig symbols (ESP-IDF component manager)
description: Confirmed symbol names from managed_components/lvgl__lvgl/Kconfig — use these, not LVGL 8 names
---

Malloc backend is a choice menu titled "Malloc functions source" (Kconfig line 45):
- `CONFIG_LV_USE_BUILTIN_MALLOC=y` — default; compiles `static uint8_t work_mem_int[LV_MEM_SIZE]` into BSS (~64 KB by default)
- `CONFIG_LV_USE_CLIB_MALLOC=y` — "Standard C functions malloc/realloc/free"; eliminates work_mem_int from BSS entirely
- `CONFIG_LV_USE_MICROPYTHON_MALLOC=y`, `CONFIG_LV_USE_RTTHREAD_MALLOC=y`, `CONFIG_LV_USE_CUSTOM_MALLOC=y` also exist

Pool size (only applies when BUILTIN is selected):
- `CONFIG_LV_MEM_SIZE_KILOBYTES=64` — default 64 KB (Kconfig line 96, depends on LV_USE_BUILTIN_MALLOC)
- `CONFIG_LV_MEM_POOL_EXPAND_SIZE_KILOBYTES=0` — expansion

**Wrong names (silently ignored with a warning):**
- `CONFIG_LV_MEM_CUSTOM` — LVGL 8 name, does not exist in LVGL 9.5.0
- `CONFIG_LV_STDLIB_MALLOC_CLIB` — does not exist
- `CONFIG_LV_USE_STDLIB_MALLOC_CLIB` — does not exist

**Why:** LVGL 9 renamed the memory backend from LV_MEM_CUSTOM to a choice under "Standard library" → "Malloc functions".

**PSRAM note:** To make lv_malloc() (via CLIB path) draw from PSRAM, use `CONFIG_SPIRAM_USE_MALLOC=y` + `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=512`. CAPS_ALLOC mode does NOT redirect plain malloc() to PSRAM.
