---
name: ESP32-P4 PPA rotation for LVGL flush
description: PPA SRM engine availability and semantics for hardware display rotation on IDF 5.3.5
---

- `esp_driver_ppa` IS available in IDF 5.3.x (present since v5.3.0) — no need for 5.4 (which hangs in DSI init on this board).
- `ppa_srm_oper_config_t.rotation_angle` is COUNTER-clockwise. The panel's portrait→landscape mapping (logical (lx,ly) → physical (H-1-ly, lx)) is a visually clockwise 90°, so use `PPA_SRM_ROTATION_ANGLE_270`. If the image ever appears 180°-flipped, the fix is ANGLE_90.
- The PPA driver handles cache write-back of the input and invalidation of the output buffer internally — no manual `esp_cache_msync` needed before `esp_lcd_panel_draw_bitmap` (DMA2D) reads the rotated buffer.
- Buffers still must be 64-byte aligned and `out.buffer_size` a multiple of the cache line (existing `heap_caps_aligned_alloc(64, ...)` LVGL buffers satisfy this).

**How to apply:** any future flush-path or rotation change in the firmware — keep rotation on PPA, don't reintroduce CPU transpose loops.
