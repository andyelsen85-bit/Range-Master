# TrapMaster ESP32-P4 Firmware

ESP-IDF 5.3.x + LVGL v9 firmware for the **Guition JC8012P4A1C-I-W-Y** clay-shooting terminal.

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
# install ESP-IDF 5.3.x (5.3.5 recommended for this board)
. $IDF_PATH/export.sh
cd artifacts/firmware
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Project layout

### All-machine test

In **Einstellungen → Maschinen**, **Testauslösung Alle Maschinen** asks for
confirmation and then sends one authenticated FIRE per enabled machine, in A–H
order. Disabled machines are excluded; H receives one FIRE, not separate H1/H2
commands. This is not a simultaneous radio broadcast and does not record a game,
consume credits, or add game clay totals. Individual results distinguish ACK
success, a sent command without ACK, and errors. An HTTP failure stops the
remaining commands. Keep all trap danger areas clear before confirming.

Host regression checks (mock HTTP/LVGL/NVS; no traps are actuated):

```bash
python artifacts/firmware/tests/test_machine_batch.py
```

### Offline-cache recovery

The `storage` FAT partition starts at **0x680000** and has size **0x980000**
(9.5 MiB). The cache uses **4096-byte allocation units**, with a default
4096-byte wear-levelling sector. The firmware also checks the actual WL sector
size at mount time; 4096-byte clusters are compatible with either 512-byte or
4096-byte WL sectors.

After mounting, logs show total/free bytes and a 16-byte self-test, with separate
return values and immediately captured `errno` for open, write, fsync and close.
A zero/tiny capacity or a failed probe on a fresh/empty cache triggers one
explicitly logged **unmount → config-preserving format → remount** fallback.
This erases only the rebuildable FAT snapshots; NVS settings and operational
outboxes are not erased. An established cache with normal capacity is not
automatically reformatted merely because its probe fails. Failed initialization
is latched until reboot to avoid mount/format loops.

Cache failures do not invalidate successful server pulls. The terminal keeps
the pulled data in RAM, performs its existing NVS operational saves, and reports
successful sync with the small **CACHE NICHT VERFÜGBAR** warning. Failed
snapshots cannot advance their durable manifest tokens; degraded caches are
retried by subsequent pulls. Large portal snapshots are not moved into the
small NVS partition as a fallback.

Fault-injected host regression checks (no real flash, server or NVS):

```bash
python artifacts/firmware/tests/test_offline_cache.py
```

### Source layout

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
