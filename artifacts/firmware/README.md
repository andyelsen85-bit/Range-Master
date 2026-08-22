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
    lora_stub.c/h       — persistent HTTP worker for the local LoRa gateway
```

## Local LoRa gateway

The terminal does **not** contain or wire a LoRa radio. When a machine fires, the
game screen queues a short HTTP request to the configured local gateway URL. The
persistent worker keeps all network I/O off the LVGL task and retries at most twice.

Set **TrapMaster Gateway URL** in Einstellungen to the IP shown on the gateway OLED,
for example `http://192.168.1.50`, and set **TrapMaster Gateway Auth Key** to the matching
private HMAC key used to build the gateway. The key is never sent over HTTP. An empty or
invalid URL/key shows an explicit gateway error on the active game screen instead of
silently doing nothing. Each fire uses a persisted, authenticated sequence, so captured
requests are rejected and a WiFi retry never creates a second trap pulse.

For safety, the gateway URL and authentication key are intentionally available only on
the terminal's physical Einstellungen screen; they are never displayed or accepted by
the terminal's LAN configuration page.

The corresponding Heltec Wireless Stick V3 sketches are in:

- `artifacts/lora-gateway/` — WiFiManager setup, HTTP API, SX1262 TX, OLED status
- `artifacts/lora-relay/` — WiFi-free authenticated receiver and relay pulse
- `artifacts/lora-common/` — AES-128-GCM frame, authentication, nonce, and replay format

Read their hardware and safety checklists before connecting a relay to a trap.
