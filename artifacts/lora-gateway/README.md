# TrapMaster LoRa Gateway

Firmware for a **Heltec Wireless Stick V3** acting as the WiFi-to-LoRa gateway.
It is intentionally separate from the ESP32-P4 terminal: the terminal sends local
HTTP requests and the gateway owns the SX1262 radio.

## Before first flash

### Private configuration

The Arduino sketch is in the nested folder
`TrapMasterGateway/TrapMasterGateway.ino`. Copy
`TrapMasterGateway/TrapMasterGateway.config.example.h` to
`TrapMasterGateway/TrapMasterGateway.local.h` beside the sketch, then enter the
private AES and terminal-to-gateway HMAC keys in that local file. The sketch
automatically loads it when present. Local configuration files are ignored by Git
and must never be committed or shared.

1. Install the official **Heltec ESP32 Dev-Boards** Arduino package/library and select
   **Wireless Stick V3**. The sketches use `LoRaWan_APP.h`, which supplies the board's
   built-in SX1262 pin mapping. Do not add manually guessed radio SPI/DIO pins.
2. Define `TM_PROTOCOL_KEY_BYTES` as the same unique 16-byte AES key for the gateway
   and every relay. A normal build deliberately fails until this value is provided.
   `TM_ALLOW_INSECURE_BENCH_KEY` is available only for a disconnected bench test.
3. Define `TM_GATEWAY_AUTH_KEY` as a long, unique HMAC key (at least 16 characters).
   Set the identical value in the terminal's **TrapMaster Gateway Auth Key** setting.
   The key is never sent over HTTP; the gateway deliberately fails to compile without it.
4. Confirm that 433 MHz operation, chosen channel, output power, duty cycle, and
   antenna use are permitted at the installation location.
5. Connect the board over USB-C and flash
   `TrapMasterGateway/TrapMasterGateway.ino`.

## Setup and use

- First boot opens `TrapMaster-Gateway-Setup`; use a phone to select venue WiFi.
- Hold the USER button during boot for about 1.5 seconds to erase saved WiFi and open
  setup again.
- The setup page includes a **Use fixed IPv4 address (DHCP off)** toggle. When enabled,
  enter the gateway's fixed IPv4 address and the venue router/gateway IPv4 address.
  The gateway uses a `255.255.255.0` subnet mask and the router for DNS. Both addresses
  must be on the same `/24` network; leave the toggle off to use DHCP. Saving the setup
  page restarts the gateway once so the selected address mode takes effect.
- The OLED shows the connected IP, last machine, and radio result.
- Set the terminal's **TrapMaster Gateway URL** to `http://<OLED-IP>`.
- Set the terminal's **TrapMaster Gateway Auth Key** to the exact private
  `TM_GATEWAY_AUTH_KEY` value used to build this sketch.

Endpoints:

```text
GET /nonce              # {"nonce":"<32 hex characters>","expiresInMs":10000}
GET /fire?machine=A&nonce=<32 hex characters>  # X-TrapMaster-Auth HMAC required
GET /fire-pair?first=D&second=F&delayMs=1000&nonce=<32 hex characters>
                                  # X-TrapMaster-Auth pair HMAC header required
GET /health             # X-TrapMaster-Auth health-check HMAC header required; never transmits
GET /status             # uptime, IP/RSSI, last result, ACK state
```

`/fire` returns `200` only after an authenticated relay ACK, or `202` when the packet
was transmitted but no ACK arrived. It returns an error when the request is invalid,
the radio cannot transmit, the gateway is busy, or the HMAC is invalid. The terminal
fetches a gateway-issued nonce immediately before each fire, then signs and sends
the command. Multiple terminals with the same configured HMAC key can share one
gateway without coordinating terminal counters. **Update terminal and gateway
firmware together**: sequence-based HTTP fire commands are no longer supported.

### Nonce lifetime, retries, and concurrency

- `/nonce` issues a hardware-random **16-byte** value encoded as 32 lowercase hex
  characters. It is valid for **10 seconds** until first use. This non-actuating
  endpoint does not require authentication; possessing a nonce is not permission
  to fire, because the command must still carry a valid HMAC.
- At most **8 unused nonces** are outstanding. Issuance less than **20 ms** after
  the previous issuance, or exhausted RAM capacity, returns `429`. Responses are
  `Cache-Control: no-store`.
- Authentication covers the original fields with the raw 16-byte nonce replacing
  the four sequence bytes. Single payload: `TM`, byte `01`, machine, nonce.
  Pair payload: `TM`, byte `01`, `P`, first, second, delay (uint32 little-endian),
  nonce, `DOU`. HMAC-SHA-256 and `X-TrapMaster-Auth` hex encoding are unchanged.
- A nonce is consumed **before** any radio action. Invalid authentication or an
  unknown/expired nonce returns `401`. Reusing a consumed nonce with different
  authenticated machine/pair/timing fields returns `409` (conflict).
- There is only **one active fire operation**, including the entire pair delay.
  A second terminal receives `409 {"ok":false,"error":"busy"}` promptly, not a
  queued late shot. That rejected request is also consumed and cached: retrying
  it after the first finishes cannot unexpectedly launch a trap.
- The exact final HTTP status and JSON body—including partial, busy, storage,
  and radio failures—are retained for **30 seconds after completion**. WiFi
  retries use the **same nonce, fields, and HMAC**, returning the same result
  without radio transmission. A same-command retry while it is still running
  receives `409 busy`. The terminal retries transport failures only, never a
  final HTTP rejection and never by automatically fetching a replacement nonce.
- RAM has 32 preallocated slots shared by outstanding and consumed nonces.
  Unexpired results are never evicted to admit new requests; `/nonce` returns
  `429` when full. After result expiry a replay returns `401`, never another fire.
- On reboot all HTTP nonces disappear, including any interrupted pair. A retry
  returns `401` and no fire is resumed. No HTTP sequence or request-progress NVS
  writes are required. **The LoRa counter NVS write before each radio transmission
  and the relay replay counter/AES-GCM protocol remain unchanged.** A counter-save
  failure stops transmission and its error result is cached without re-firing.

### `/health`

`/health` accepts only the separate authenticated health-check signature generated by
the terminal. It returns `200` when the gateway is reachable and the configured HMAC
key matches, returns `401` for an incorrect key, and does not access the LoRa radio.
Use **Gateway testen** in the terminal settings after saving the gateway URL and key.
The health HMAC payload remains `TM`, byte `01`, `CHECK`; no nonce is required,
allocated, or consumed. A successful response includes:

```json
{"ok":true,"auth":true,"ip":"192.168.1.50","uptimeMs":12345,
 "fireAuth":"nonce-v1","nonceTtlMs":10000,"resultTtlMs":30000,"busy":false}
```

`busy` reports an active single or pair operation. Health checks remain
non-actuating and responsive while a fire waits for its ACK or pair delay.

## Custom doublettes and H

- `/fire-pair` is only for two different A–G machines. It signs the ordered first
  machine, second machine, delay, and nonce together; its delay is limited to
  0–10,000 ms.
- The gateway consumes the pair nonce in RAM **before** the first radio transmission.
  With a positive delay, it sends the
  second machine only after the first relay ACK arrives. With a `0` ms delay, it
  skips the first ACK wait and transmits the second machine immediately after the
  first radio transmission completes. A duplicate request returns its cached final
  exact outcome, including a first-ACK timeout or second-send failure. A restart
  invalidates the nonce (`401`); a LoRa counter storage failure is cached (`503`).
  Neither case resumes the pair or permits automatic retry as a new FIRE command.
- H is deliberately not accepted by `/fire-pair`. A terminal H doublette calls
  `/fire?machine=H` once; the connected H machine creates H2 after H1, and the
  relay only supplies the single trigger pulse.

## Staged hardware checklist

1. Flash gateway only and confirm WiFi captive setup, OLED IP, `/status`, and that
    malformed, expired/unknown, or incorrectly authenticated `/fire` requests are rejected.
2. For the **radio-only bench stage**, compile both sketches with
   `TM_ENABLE_UNENCRYPTED_BENCH_TEST` and define `TM_BENCH_INTERLOCK_GPIO` as a
   verified local test-enable input on the gateway and relay. Keep the relay disconnected
   from the trap. `/bench-fire?machine=A` only transmits while the gateway's physical
   interlock is held low, and the relay only pulses while its own interlock is held low.
   Rebuild both sketches *without* that flag before connecting a real trap.
3. Flash one normal relay and, while disconnected from any trap, verify that a valid request
   produces a serial `FIRE` line and an ACK on the gateway.
4. Verify an altered ciphertext/tag and a repeated captured frame do not operate the
   relay.
5. Only then wire the relay's COM/NO dry contact to a verified trap trigger.
6. With all traps still disconnected, test one A–G custom pair with a positive
   delay: confirm the first relay ACKs before the second fires. Then test a `0`
   delay pair and confirm both commands transmit back-to-back without waiting for
   the first ACK. Retry each identical HTTP request and verify that neither relay
   pulses a second time.
7. With two terminals and traps disconnected, start a positive-delay pair on one
   terminal and fire on the other during the delay. Confirm the second reports
   **Gateway belegt oder Anfragekonflikt** (`409`), no extra radio packet is sent,
   and retrying that rejected nonce later still returns busy. Power-cycle during
   an active pair and confirm replay of its old nonce returns `401` without
   sending either machine again.

Host-side regression tests (mock radio/storage, no trap operation):

```bash
python artifacts/lora-gateway/tests/test_nonce.py
python artifacts/firmware/tests/test_gateway_nonce.py
python artifacts/firmware/tests/test_machine_batch.py
```

## Counter recovery

The gateway persists its transmit counter and each relay persists its last accepted
counter. Never reset either counter while retaining the same AES key: doing so is
intentionally fail-closed at the relay. For a coordinated device replacement or NVS
reset, disconnect every trap, install a new AES key on the gateway and all relays,
then perform the staged checklist again.