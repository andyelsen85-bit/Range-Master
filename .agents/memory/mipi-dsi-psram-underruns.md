---
name: MIPI DSI PSRAM underruns
description: How to recognize and prevent ESP32-P4 MIPI framebuffer starvation during storage activity.
---

A brief full-screen blue or cyan frame on the ESP32-P4 MIPI display is a DPI DMA underrun: the controller could not fetch the scanout framebuffer from PSRAM fast enough. Do not treat this symptom as ordinary LVGL invalidation or backlight flicker.

**Why:** The ESP-IDF 5.3 MIPI driver explicitly changes to a blue frame after an external-memory underrun. Durable FAT cache writes introduced enough flash/PSRAM bus contention during sync to expose it.

**How to apply:** Keep RGB565 and the confirmed panel timings. Enable Espressif's recommended PSRAM XIP, 256 KB L2 cache, 128-byte cache lines, and performance compiler optimization before attempting framebuffer or UI architecture changes. Confirm with the driver's underrun log during hardware tests.