# TrapMaster ESP32-P4 Firmware

ESP-IDF 5.4 + LVGL v9 firmware for the **Guition JC8012P4A1C-I-W-Y** clay-shooting terminal.

## Hardware

| Component | Detail |
|-----------|--------|
| SoC | ESP32-P4NRW32 |
| Co-processor | ESP32-C6-MINI-1U-N4 (WiFi / BLE) |
| Display | 10.1″ MIPI-DSI, JD9365 controller, 1280×800 (rotated 90°) |
| Touch | GSL3680 capacitive, I²C |
| Storage | 32 MB PSRAM + NVS flash |

## Screens

| Screen | Nav key |
|--------|---------|
| Dashboard | boot / home |
| Spill starten | Start |
| Spill (active game) | auto after start |
| Resultater | auto after game end |
| Spillgeschicht | History |
| Kreditter | Credits |
| Spillerverwaltung | Players |
| Astellungen | Settings |
| WiFi | inside Settings |
| Bluetooth | inside Settings |

## Build

```bash
# install ESP-IDF 5.4+
. $IDF_PATH/export.sh
cd artifacts/firmware
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Project layout

```
main/
  app_config.h          — pin map, display geometry, build constants
  main.c                — app_main: init display → touch → LVGL → UI
  display/
    jd9365_panel.c/h    — MIPI-DSI init + JD9365 init sequence
    gsl3680_touch.c/h   — I²C touch read → LVGL indev
  store/
    game_store.c/h      — state machine (mirrors emulator gameStore.ts)
    nvm_storage.h       — NVS helpers
  net/
    coprocessor.c/h     — UART AT bridge to ESP32-C6 (WiFi + BLE HID)
    http_sync.c/h       — portal REST sync
  ui/
    ui_manager.c/h      — screen router
    screen_dashboard.c/h
    screen_start.c/h
    screen_spiel.c/h
    screen_resultate.c/h
    screen_geschichte.c/h
    screen_kredite.c/h
    screen_einstellungen.c/h
    screen_spiller.c/h
    screen_wifi.c/h
    screen_bluetooth.c/h
  lora_stub/
    lora_stub.c/h       — UART placeholder for phase-2 LoRa module
```

## Phase 2 (task #49)
Add LoRa machine control — replace the stub in `lora_stub/` with real
SX1276/RFM95 driver to fire clay machines A–H wirelessly.
