# Terminal controls and day-player synchronization

The authoritative palette is `main/ui/score_colors.h`; the game buttons and
the shared live/history/result scorecard use these constants:

| Action | Colour | Label |
|---|---|---|
| 0 | black `#000000` | white, light border |
| 1 | yellow | black |
| 2 | green | black |
| START | red | white |
| WDH | blue | white |
| SKIP | white | black |

Use the same colours when labelling the six-button wireless remote. A doublette
is two separate 0/2 scores, maximum four points. WDH first releases an unscored
launch without changing position or scores; a second WDH undoes the previous
complete unit. A half-scored pair must be completed before undoing both results.
The explicit stage survives reboot. Legacy or damaged checkpoints conservatively
require an operator decision before another launch. Do not downgrade firmware
with an unfinished game; older firmware cannot read the new active-checkpoint
version. Finished-game journal records and partition layout have not changed.

Day-player ADD/REMOVE events use a bounded, checksummed two-slot FAT outbox
(`dayp0.bin`, `dayp1.bin`, 128 events, under 24 KiB total), not a rebuildable cache.
Never delete it to free space. Local negative player IDs wait for portal identity
assignment. Delivery is in full and automatic billing syncs; rejected 4xx events
remain stored and require an explicit manual sync after correcting the cause.
Reconciliation only removes registrations owned by this terminal.

Deploy the API/portal update before flashing terminals. If a terminal already
received a 404 for the new endpoint, run manual synchronization once after the
API update; automatic retries intentionally do not repeat rejected events.

Portal connections are retained between syncs, including optional log/manifest
requests. For an existing local `sdkconfig`, enable
`CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS=y` for TLS session resumption after a
reconnect; new configurations inherit this from `sdkconfig.defaults`.
The boot cycle has a 19.5-second network deadline and leaves a short publication
allowance. DNS ownership and socket waits obey that deadline; large rebuildable
cache writes are deferred to a later worker sync. Live UI does not wait for
network completion. Flash, radio, real-network timing and layout still require
device verification.

# TrapMaster ESP32-P4 Firmware

## Inventar & Tag: Oberfläche und Schriftprüfung

Der Bildschirm zeigt Tagesbeginn und Tagesschluss gleichzeitig. Beide Spalten
sind bei eingeblendeter Tastatur separat scrollbar. Maîtres werden unter den
aktiven, im Portal markierten Mitgliedern gesucht und als entfernbare Chips
ausgewählt (höchstens vier, ohne Duplikate). Ein bestätigter letzter
Schlussbestand kann übernommen werden; dann sind die Anfangsfelder gesperrt.
Gastvereine müssen neu zählen. Ohne bestätigten Schluss ist keine Übernahme
möglich. Differenz bedeutet wie im Portal **erwartet minus gezählt**:
positive Werte sind Fehlmengen, negative Werte Mehrbestand.

Entwürfe, Fokus und Scrollposition bleiben bei Sync-Aktualisierungen erhalten.
Die unveränderliche NVS-Warteschlange und der normale Sync-Worker bleiben in
Verwendung. Das bei Öffnung erfasste Datum wird zusätzlich innerhalb des
gesperrten Queue-Schreibpfads geprüft, damit auch ein Tageswechsel zwischen
den zwei Buchungen keinen Bestand dem falschen Tag zuordnet.
„Ohne Inventar beenden“ erscheint ausschließlich im Ausschaltablauf; eine
Eingabeänderung setzt dessen Zwei-Tipp-Bestätigung zurück.

Alle zehn Schriftgrößen behalten Montserrat als erste Schrift einschließlich
ASCII-Metriken und LV_SYMBOL. Die Fallback-Schriften decken U+00A0–U+017F und
die zusätzlichen Satz-/UI-Zeichen ab. Screen-Roots **und** die globale
LVGL-Theme-Schrift verwenden die UI_FONT-Wrapper. Portal-Namen, Produktnamen,
angezeigte Notizen und der konfigurierte Vereinsname verwenden den gemeinsamen
UTF-8-Filter: unterstützte Zeichen bleiben erhalten; unbekannte/fehlerhafte
Zeichen werden sichtbar zu `?`. `unsupported glyph`-Diagnosemeldungen enthalten
den kumulierten Ersetzungszähler seit Start und sind auf eine pro Stunde begrenzt.
Bestehende unveränderliche Ereignis-Notizen werden nicht umgeschrieben.

Prüfungen und reproduzierbare Render-Captures (Linux, aufgelöste LVGL-Abhängigkeit):

```bash
# Kein ESP-Toolchain-/Font-Konverter-Download für die CI-Schriftprüfung:
python3 artifacts/firmware/tests/test_font_coverage.py
# Tatsächliche LVGL-Widgets und Projekt-Schriften/-Styles, nur Daten/Netz als Test-Fixture:
python3 artifacts/firmware/tests/render_inventory.py
# Optional: Schriften neu erzeugen; benötigt DejaVuSans.ttf und pnpm:
python3 artifacts/firmware/tools/generate_fonts.py
```

Der Renderer prüft Suche/Auswahl, Übernahme, Tastatur, Queue-Reihenfolge,
Sync-Entwürfe/Scrollposition, Tageswechsel und Ausschaltbestätigung. Er erzeugt
unter `generated-artifacts/inventory/` vier PNGs (leer, ausgewählte Maîtres,
Übernahme, Tagesschluss mit Differenz) in 1280×800. Das sind echte LVGL-Renderings,
keine Fotos vom Terminal. Die Schriftprüfung scannt alle C++-Stringliterale
(einschließlich Raw-Strings), sämtliche Font-Cmaps und die Textkonvertierung;
sie läuft auch im GitHub-Workflow `firmware-text.yml`.

Vollständiger ESP-IDF-5.3.5/P4-Build geprüft: Firmware `0x27acc0`
(2.600.128 Byte), **742.208 Byte frei je 0x330000-OTA-Slot**, über der
500-KiB-Untergrenze. Partitionen, bestehende NVS-Daten und FAT-Daten bleiben erhalten.

### Harakiri: eigene Reihenfolge pro Spieler

Harakiri und Harakiri Custom mischen für jeden Spieler unabhängig die aktiven
A–G-Einzelmaschinen und die gesamte Doubletten-Einheit. H1/H2 bzw. das
konfigurierte Paar bleiben aufeinanderfolgend beim selben Spieler; erst dann
ist der nächste Spieler an der Reihe. Beide Läufe verwenden dieselbe gespeicherte
Spielerreihenfolge. Normal und Custom 1–4 behalten ihre gemeinsame Sequenz.
Die Fisher-Yates-Mischung nutzt `esp_random()` mit unverzerrter Bereichsauswahl;
ein `srand()` ist nicht erforderlich. Bei sehr wenigen Einheiten können zwei
unabhängig gemischte Reihenfolgen natürlich identisch sein.

Die sechs Index-Maps brauchen zusammen **54 Byte RAM**. Aktive Harakiri-Spiele
(Spieler, Reihenfolgen, Positionen, Ergebnisse und Kreditereignis-IDs) werden in
zwei versionierten, prüfsummengeschützten FAT-Slots `active0.bin`/`active1.bin`
gespeichert: **8.084 Byte je Datensatz, 16.384 Byte zugeordnete Cluster insgesamt**.
Es gibt keinen zusätzlichen NVS-Blob und keine anwachsende temporäre Dateiliste.
Die Slots sind **keine löschbaren Cache-Dateien**. Das Journalbudget reserviert
konservativ 24 KiB dafür innerhalb seiner 50-%-Grenze. Ein Speicherfehler sperrt
weitere Wertung/Starts und wird angezeigt; Speichern wird erneut versucht.

Nach Neustart wird die letzte gültige Reihenfolge geladen, niemals neu gemischt.
Die letzte möglicherweise ausgelöste Einheit muss bewusst geprüft, gewertet,
übersprungen oder wiederholt werden; der Neustart autorisiert keinen zweiten
Schuss. Abbruch/Abschluss schreibt einen dauerhaften Abschlussmarker. Ein bereits
fertig gespeichertes Spiel im Journal hat Vorrang vor einem älteren aktiven
Spielstand. Beschädigte, nicht prüfbare Daten werden nicht überschrieben.
Konfigurations-Restore ist während eines offenen Harakiri-Spiels gesperrt.
Lokale Spieler-IDs werden in beiden Slots vor Portal-Bestätigung umgeschrieben;
gleichzeitige Snapshots mit noch alten RAM-IDs werden ebenfalls übersetzt.

Terminal und Portal ordnen Harakiri-Ergebnisse nach Einzelmaschinen und danach
den beiden Doubletten-Partnern an (H1/H2 oder das konfigurierte Paar). Einzel-A
und ein A innerhalb einer Custom-Doublette bleiben getrennte Spalten. Die
übertragene `taube` bleibt die eigene Schussposition des Spielers; die Maschine
kommt aus seiner gespeicherten Reihenfolge. Historische gemeinsame Reihenfolgen
bleiben lesbar, ohne ihre Punkte umzuschreiben.

```bash
python3 artifacts/firmware/tests/test_harakiri_players.py
python3 artifacts/firmware/tests/test_harakiri_custom.py
python3 artifacts/firmware/tests/test_remote_actions.py
python3 artifacts/firmware/tests/test_history_scorecard.py
node --experimental-strip-types --test lib/scorecard/tests/harakiri.test.mjs
```

### Bildschirmwechsel während der Synchronisation

Die UI behält keine ungeprüften LVGL-Objektzeiger über eine
Bildschirm-Aktualisierung. Temporäre Snapshot-Referenzen verfolgen
`LV_EVENT_DELETE`; beim Löschen werden sie **vor** der Freigabe genullt.
Überlebende Eingabefelder/Scrollcontainer behalten Text und Position;
gelöschte Listeneinträge erhalten weder ihren Text noch ihren Fokus zurück.
`lv_obj_is_valid()` ist kein sicherer Lebenszeit-Test für bereits gelöschte
Objekte. Cached Zeilenreferenzen in Kredite/Catering werden vor dem Leeren
der Listen zurückgesetzt. Aktives Tippen und Scrollen bleibt ununterbrochen.

Host-Regression (benötigt die aufgelöste LVGL-Komponente, nicht das Endgerät):

```bash
python3 artifacts/firmware/tests/test_view_snapshot.py
```

Der Test kompiliert den echten Router-/Publikationspfad mit LVGL unter
AddressSanitizer/UBSan. Er öffnet alle elf Bildschirm-Routen mit ausstehender
Publikation, löscht und erstellt Listeneinträge während Snapshots bestehen,
und prüft Fokus-, Text-, Scroll- und Callback-Lebenszeit. Die Bildschirm-Inhalte
sind deterministische Test-Fixtures; eine physische Touch-/Sync-Probe ersetzt
dieser Host-Test nicht. Der GSL3680-Treiber protokolliert nur noch den ersten
gültigen Punkt pro Berührungsgeste auf DEBUG, nicht mehr 50 INFO-Zeilen nach
Start. Ein kurzzeitiger I²C-Fehler zählt nicht als neue Geste.

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
Every mount logs a once-per-boot file census and explicit journal/cache/free-space
budgets. A full/unwritable volume receives cache-only cleanup and a non-formatting
remount only after proving zero pending games in both RAM and the on-disk journal.
Unacknowledged, corrupt or unverified journal records refuse cleanup; the mount
stays readable for recovery/upload. Unknown files, all journal records and NVS
are untouched by cache cleanup. Only initial provisioning of a never-used
partition may initialize FAT; repair of a journal-bearing volume never formats.

**Einstellungen → System → Speicher bereinigen** displays the pending-game
count and asks for confirmation. It refuses pending/uncertain games and a
running sync, then rechecks under the storage locks in a background worker.
It needs neither erase-flash nor an NVS reset. Detailed budgets, root-cause
analysis and recovery rules are in
[DIAGNOSTICS_RECOVERY.md](DIAGNOSTICS_RECOVERY.md#full-fat-diagnosis-and-safe-recovery).

Cache failures do not invalidate successful server pulls. The terminal keeps
the pulled data in RAM, performs its existing NVS operational saves, and reports
successful sync with the small **CACHE NICHT VERFÜGBAR** warning. Failed
snapshots cannot advance their durable manifest tokens; degraded caches are
retried by subsequent pulls. Large portal snapshots are not moved into the
small NVS partition as a fallback.

Fault-injected host regression checks (no real flash, server or NVS):

```bash
python artifacts/firmware/tests/test_offline_cache.py
python artifacts/firmware/tests/test_game_journal.py
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

### Full storage despite a small file census

Use **Einstellungen > System > Speicher reparieren** for orphaned FAT clusters,
not `erase-flash`. Formatting is allowed only after verified absence of pending
games, an active game, queue reservations/events and other outstanding actions;
unknown/corrupt evidence refuses repair. The confirmation shows pending status.
Accepted local archives/cache may be rebuilt from the portal; settings and NVS
are preserved. **Speicher bereinigen** remains cache-only. See
[DIAGNOSTICS_RECOVERY.md](DIAGNOSTICS_RECOVERY.md#full-fat-with-few-visible-files-orphan-chain-recovery)
for safeguards, serial diagnostics, HTTP-400 backoff and hardware acceptance.
