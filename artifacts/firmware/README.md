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
# First checkout only; skip set-target when retaining an existing ESP32-P4 sdkconfig:
idf.py set-target esp32p4
idf.py build
# Before flashing an existing terminal, follow the mandatory migration steps below.
idf.py -p /dev/ttyUSB0 flash monitor
```

### First USB flash with the OTA-only partition table

**Synchronize all games to the portal and confirm there are no unacknowledged
games before the first flash with this table.** Stop play during migration.
The FAT partition contains the unsynced game journal, not just disposable cache.

The unused factory app has been removed. The only app slots are:

| Partition | Offset | Size |
|-----------|--------|------|
| `ota_0` | `0x20000` | `0x330000` |
| `ota_1` | `0x350000` | `0x330000` |

`nvs` (`0x9000` / `0x6000`), `otadata` (`0xf000` / `0x2000`),
`phy_init` (`0x11000` / `0x1000`) and **`storage` (`0x680000` /
`0x980000`) remain unchanged**. Neither app slot overlaps storage.
No factory app is required: erased OTA metadata selects the first OTA slot.

1. Export the pinned ESP-IDF 5.3.5 tools and open `artifacts/firmware`.
   For an existing ESP32-P4 checkout, keep its `sdkconfig`; do not reset it.
2. Run `idf.py reconfigure` and `idf.py build`. Check `menuconfig` if an old
   configuration enabled factory-reset/test-app boot: both must be disabled,
   with the custom `partitions.csv` selected at `0x8000`. Defaults disable both.
3. Run `python tests/test_partitions.py build` (use your `-B` directory instead
   of `build` if different). This checks the generated partition binary,
   flash file boundaries, boot settings and at least **500 KiB** app headroom.
4. Connect by **USB** and run `idf.py -p /dev/ttyUSB0 flash monitor`
   (Windows example: `idf.py -p COM5 flash monitor`). Use the full **`flash`**
   target, not `app-flash`: the partition table and initial OTA metadata must
   be written together with the app. The initial OTA metadata resets app-slot
   selection, so the new image boots from `ota_0` at `0x20000`.
5. Confirm boot and portal connectivity, journal status and saved settings
   before resuming play. Subsequent app-only OTA updates can use these slots.

**Never run `erase-flash` / `erase_flash`, erase or format FAT, or write a merged
whole-flash image for this migration.** Do not use OTA for the first update:
ordinary app OTA cannot migrate the table, and old app offsets are incompatible.
Only the bootloader, partition table, initial OTA metadata and app are flashed;
NVS, PHY data and the storage FAT contents must not be touched.
See [diagnostics/recovery](DIAGNOSTICS_RECOVERY.md) for the remaining acceptance
checks. No flashing is performed by the regression test.

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
This initialization fallback is disabled once the game-journal protection marker
exists: FAT then contains unsynced games as well as snapshots, and must not be
formatted. NVS settings and operational outboxes are not erased. An established
cache with normal capacity is not automatically reformatted merely because its
probe fails. Failed initialization is latched until reboot to avoid mount/format
loops. Partition-table migration never calls for a format or erase.

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
