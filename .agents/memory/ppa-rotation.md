---
name: ESP32-P4 PPA rotation for LVGL flush
description: PPA SRM engine availability and semantics for hardware display rotation on IDF 5.3.5
---

- `esp_driver_ppa` IS available in IDF 5.3.x (present since v5.3.0) — no need for 5.4 (which hangs in DSI init on this board).
- `ppa_srm_oper_config_t.rotation_angle` is COUNTER-clockwise. The panel's portrait→landscape mapping (logical (lx,ly) → physical (H-1-ly, lx)) is a visually clockwise 90°, so use `PPA_SRM_ROTATION_ANGLE_270`. If the image ever appears 180°-flipped, the fix is ANGLE_90.
- The PPA driver handles cache write-back of the input and invalidation of the output buffer internally — no manual `esp_cache_msync` needed before `esp_lcd_panel_draw_bitmap` (DMA2D) reads the rotated buffer.
- Buffers must match the configured L2 cache-line alignment. With `CONFIG_CACHE_L2_CACHE_LINE_128B`, 64-byte-aligned PSRAM addresses can pass allocation but PPA rejects the first flush; allocate all LVGL/PPA buffers at 128-byte alignment and keep `out.buffer_size` a multiple of 128.
- Keep LVGL in partial render mode but allocate full-screen render and rotation buffers. Full-screen invalidations must reach the live panel framebuffer in one flush; smaller strip buffers make navigation visibly repaint in bands.

**Why:** The DPI panel currently uses one live framebuffer. Splitting a screen transition across multiple partial flushes exposes each strip as it is copied and looks like random flashing during navigation or large sync-driven redraws.

**How to apply:** For future flush-path or rotation changes, keep rotation on PPA, retain full-screen-capacity PSRAM buffers in partial mode, and do not reintroduce CPU transpose loops.
