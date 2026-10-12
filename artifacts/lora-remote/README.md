# TrapMaster six-button wireless remote

Heltec **Wireless Stick V3, 433 MHz**, powered through USB-C. This is an
operating remote, **not an emergency stop or safety interlock**.

## Provisioning and building

1. Install the official **Heltec ESP32 Dev-Boards** board package and library,
   select **Heltec Wireless Stick (V3)**. The Heltec radio library owns the
   SX1262 pins. This handheld does not need WiFiManager or WebSockets.
2. Copy `TrapMasterRemote.config.example.h` to
   `TrapMasterRemote1/TrapMasterRemote.local.h`. Private local files are ignored
   by Git. Supply the same private `TM_PROTOCOL_KEY_BYTES` as the gateway and
   relays. There is deliberately **no default key or GPIO assignment**.
3. Verify every GPIO against the actual **Wireless Stick V3 board revision,
   schematic and exposed headers**, then define all inputs below. The firmware
   rejects missing, duplicate and known reserved GPIOs.
4. Open `TrapMasterRemote1/TrapMasterRemote1.ino`. This wrapper fixes sender
   ID to ASCII `'1'`. Another handheld needs its own unique ID `'2'`…`'8'`
   and its own NVS counter. Never deploy two handhelds with the same ID.
5. Fit a suitable antenna before transmitting. Confirm the site's 433 MHz
   channel, power and duty-cycle regulations.

The remote matches existing relay parameters: 433000000 Hz, bandwidth setting
0 (125 kHz), SF7, coding-rate setting 1, preamble 8, variable payload,
CRC enabled, normal IQ and the same Heltec LoRa sync-word default.

### Wiring: direct buttons

| Input | Mandatory build define | Connection |
|---|---|---|
| Schuss | `TM_BTN_FIRE_GPIO` | verified GPIO ↔ button ↔ GND |
| Wiederholen | `TM_BTN_REPEAT_GPIO` | verified GPIO ↔ button ↔ GND |
| Skip | `TM_BTN_SKIP_GPIO` | verified GPIO ↔ button ↔ GND |
| Punkt 0 | `TM_BTN_P0_GPIO` | verified GPIO ↔ button ↔ GND |
| Punkt 1 | `TM_BTN_P1_GPIO` | verified GPIO ↔ button ↔ GND |
| Punkt 2 | `TM_BTN_P2_GPIO` | verified GPIO ↔ button ↔ GND |
| Schuss key | `TM_KEY_FIRE_GPIO` | verified GPIO ↔ key contact ↔ GND |
| Punkte key | `TM_KEY_SCORE_GPIO` | verified GPIO ↔ key contact ↔ GND |

All eight inputs use internal pull-ups. LOW/closed means pressed or armed.
Both key switches are **independent inputs**: Schuss releases Schuss,
Wiederholen and Skip; Punkte releases Punkt 0/1/2. Connect every GND wire
to one common terminal-strip GND bus and a board GND pin. **Never switch
this common bus with either key.** No external LED is required.

Excluded: boot/strapping 0/3/45/46, USB 19/20, radio 8…14, OLED 17/18/21,
battery measurement GPIO1, LED35, OLED rail36, flash/PSRAM 22…32,
serial 43/44 and RGB48. These exclusions use the official V3 variant;
they do not certify another GPIO as exposed or free on your board.
Candidates **to verify**, not wiring defaults: 2/4/5/6/7 and other
unoccupied header GPIOs. Do not use an input that is already wired onboard.

### Seven-wire alternative: 3×2 button matrix plus two keys

If eight verified inputs are unavailable, set `TM_REMOTE_MATRIX=1`.
Define `TM_ROW0_GPIO`, `TM_ROW1_GPIO`, `TM_ROW2_GPIO`,
`TM_COL0_GPIO`, `TM_COL1_GPIO` and both separate key GPIOs.

| Row | Column 0 | Column 1 |
|---|---|---|
| Row 0 | Schuss (1) | Wiederholen (2) |
| Row 1 | Skip (3) | Punkt 0 (4) |
| Row 2 | Punkt 1 (5) | Punkt 2 (6) |

Each button connects its row to its column, **not directly to GND**.
Inactive rows are high impedance, the scanned row is LOW and columns use
pull-ups. The two key contacts still connect to the common GND bus.
Use one button at a time; simultaneous detected closures are suppressed.
Individual switch diodes are recommended to prevent matrix ghosting.

## Input, radio and display behavior

- Keys debounce for 50 ms; buttons for 30 ms, with a 250 ms per-button
  cooldown. Holding does not repeat.
- Cold-boot held buttons and buttons held while their group is armed are
  ignored until released. Locked buttons consume neither a counter nor TX.
- The 64×32 OLED permanently shows **SCHUSS** and **PUNKTE**, each with
  FREI/GESPERRT. Armed groups have inverted filled banners. Brief overlays
  show `OK <button>` or `keine Antwort` for one second.
- The counter is persisted **before** initial TX. ACK receive window is
  150 ms after TX completion. Up to five retries use the **exact same
  counter and encrypted frame**, with randomized 30–150 ms backoff.
- Locking the relevant key cancels a pending retry. It cannot retract a
  frame that has already been accepted by the gateway.
- Onboard LED: one short blink for ACK, three for no response.
- Serial at 115200 prints the button code/name, counter and outcome.
  An ACK means the gateway durably received the button, **not** that a
  terminal executed it or that a clay was launched.
- Counter exhaustion or NVS failure cannot transmit. Do not erase NVS,
  clone its identity or change sender ID while retaining the same AES key.
  Reprovision with a new shared key when identity/counter history is lost.

## Power bank and sleep

`TM_REMOTE_SLEEP_MODE=0` is the default. WiFi and Bluetooth are off;
the CPU stays awake, the radio idles and the OLED dims after 20 seconds
but **never turns off while awake**.

Some banks still cut power below approximately 50–100 mA. Use a bank
with **low-current / always-on mode**, or a suitably rated dummy load.
Do not assume ordinary deep sleep will keep a bank on.

`TM_REMOTE_SLEEP_MODE=1` is optional and requires an always-on bank,
RTC-capable GPIOs, and a core built with ULP FSM support and at least
256 bytes of reserved RTC slow memory (official Heltec 3.3.8 reserves 512).
Use the matrix if eight safe RTC inputs are unavailable. After inactivity
and release of all buttons the CPU enters **deep sleep**, not light sleep.
The ULP samples input levels every 10 ms and wakes on either key edge
or a button press; no periodic CPU wake is used. In matrix sleep, all rows
are LOW and the two columns are monitored. A wake press in a previously
armed group is debounced and sent; cold-boot held buttons remain ignored.
If ULP setup fails, the remote logs the failure and remains awake.
The OLED is turned off only during deep sleep.

## Gateway and terminal

Update the gateway firmware too; existing relay firmware remains compatible.
The gateway needs **WebSockets 2.6.1** (`arduinoWebSockets`) and
**ArduinoJson 7.4.2**, in addition to Heltec and WiFiManager.
It exposes authenticated event streaming on port 81 and signed `/events`
fallback on port 80. HTTP fire and relay ACKs retain priority.

On the physical terminal, configure the existing gateway URL and HMAC key,
then enable **Fernbedienung empfangen** next to **Gateway testen**.
The setting defaults OFF and persists in NVS. Every enabled terminal
sharing this gateway reacts; disable reception on terminals that must
not act. There is no terminal URL or gateway-to-terminal HTTP push.

Touch and remote use the same game action functions. Outside a running
game, buttons are ignored. An accepted remote action starts a 500 ms
remote-wide lockout. An existing shot cannot fire again before score,
Skip or Wiederholen. P1 is ignored for every doublette; each clay is 0/2,
maximum 4 per pair.

## Safe commissioning

**First test with every relay disconnected from its trap.**

1. Check all four key combinations, status banners, locked-button silence,
   held-at-boot behavior, held-while-arming behavior and retry cancellation.
2. Confirm all six codes in serial logs and one/three LED feedback blinks.
3. Test no terminal, wrong HMAC key, an idle terminal and a running game.
4. Test two handheld IDs plus touch: a second FIRE must not launch the
   current target again. Check P1 rejection for H and every custom pair.
5. Interrupt WiFi at terminal and gateway, reconnect and check fresh events
   resume once. Events older than 3 s are intentionally discarded.
   A gateway reboot or a 32-event buffer overrun can lose events by design;
   the terminal resynchronizes or logs a gap rather than inventing actions.
6. Power-cycle the handheld/gateway and confirm counters do not reset.
   Measure gateway receive→persist→ACK timing on hardware, including a
   lost ACK/duplicate: duplicates must re-ACK without another NVS write.
7. Verify the selected power bank stays on, and test mode 1 wake on every
   button and both edges of both keys if using deep sleep.

Host tests (C++ compiler, Python and `pkg-config mbedcrypto` required):

```sh
python3 artifacts/lora-common/tests/test_button_protocol.py
python3 artifacts/lora-remote/tests/test_remote.py
python3 artifacts/lora-gateway/tests/test_remote_events.py
python3 artifacts/firmware/tests/test_button_events.py
python3 artifacts/firmware/tests/test_remote_actions.py
python3 artifacts/lora-gateway/tests/test_nonce.py
```

Host tests mock board I/O and cannot certify pin exposure, radio timing,
deep-sleep wake, WiFi reconnect or physical trap behavior.

Board-build checks used isolated copies with **bench keys and synthetic test
pin assignments**, never production provisioning: official Heltec core 3.3.8,
Heltec library 2.1.6, gateway and handheld modes 0/1. The native terminal
transport was separately compiled with SDK headers and WebSocket component
1.4.0. A complete **ESP32-P4 / ESP-IDF 5.3.5** application build, partition-size
check and the physical commissioning above remain required; an S3 transport
compile is not a substitute for that target build.
