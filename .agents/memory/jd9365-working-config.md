---
name: JD9365 working display config
description: Confirmed-working DPI timing, lane rate, init sequence, and GPIO for Guition JC8012P4A1C on ESP32-P4 (IDF 5.3.5)
---

## Confirmed working — colour cycling visible on screen (Aug 2026)

### Source of truth
All values taken verbatim from the **official Guition demo zip**:
`JC8012P4A1C_I_W_Y_NewPanel/esp32p4_lvgl_v8/src/lcd/esp_lcd_jd9365.h` and `esp_lcd_jd9365.c`

### DPI timing (JD9365_800_1280_PANEL_60HZ_DPI_CONFIG)
- `dpi_clock_freq_mhz = 80`
- `hsync_pulse_width = 20`
- `hsync_back_porch = 20`   ← was 120, completely wrong
- `hsync_front_porch = 40`
- `vsync_pulse_width = 4`
- `vsync_back_porch = 10`
- `vsync_front_porch = 30`

### DSI bus
- `num_data_lanes = 2`
- `lane_bit_rate_mbps = 1500`

### PSRAM
- `CONFIG_IDF_EXPERIMENTAL_FEATURES=y` + `CONFIG_SPIRAM_SPEED_200M=y` required for 200 MHz
- IDF 5.3.5 only; 5.4.x hangs in `esp_lcd_new_dsi_bus()` on ECO2 silicon

### GPIO (from Guition pins_config.h)
- `LCD_RST = GPIO27`
- `LCD_LED = GPIO23` (backlight, active HIGH)
- `TP_SDA = GPIO7`, `TP_SCL = GPIO8`, `TP_RST = GPIO22`, `TP_INT = GPIO21`

### Init sequence
- Unlock: `0xE0=0x00, 0xE1=0x93, 0xE2=0x65, 0xE3=0xF8`
- Lane register: `0x80=0x01` (2-lane; 0x03 is 4-lane — wrong)
- Pages 1, 2, 4, 5 all required with full gamma tables
- Sleep Out (`0x11`) needs 120 ms before Display ON (`0x29`)
- `0x29` must be sent **after** `esp_lcd_panel_init()` starts the DPI video stream

### CMakeLists.txt
- `esp_mm` must be in `PRIV_REQUIRES` (needed by `esp_cache.h` / `esp_cache_msync()`)

**Why:** The HBP of 120 (vs correct 20) was the primary failure — it made every horizontal line 480 ns too long, preventing DPI sync lock. The wrong init sequence (different panel variant, 4-lane register) compounded it.

---

## flush_cb — manual transpose is required (confirmed Aug 2026)

`lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90)` + `LV_DISPLAY_RENDER_MODE_PARTIAL`:
- LVGL adjusts **clip-area coordinates** so widgets render into physically-oriented positions
- LVGL does **NOT** reorder pixel data inside `px_map` — the buffer arrives at flush_cb in **logical row-major order**
- The manual 90° CCW pixel transpose loop in `lvgl_flush_cb` is therefore **required** and must not be removed
- Removing it causes a rotated / corrupted display; adding it back restores correct output

**msync rule:** always `esp_cache_msync(s_rot_buf, s_rot_buf_bytes, ...)` — the full rotation buffer, never a dirty-rect sub-slice. Sub-slices are not guaranteed 64-byte aligned and cause DMA corruption (stripes).
