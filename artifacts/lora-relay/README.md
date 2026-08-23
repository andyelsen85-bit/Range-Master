# TrapMaster LoRa Relay Unit

Firmware for one **Heltec Wireless Stick V3** attached to one trap machine. It has no
WiFi code and never contacts the portal or internet.

## Required configuration

Before compiling a fixed relay project:

### Private configuration and machine assignment

Copy `TrapMasterRelay/TrapMasterRelay.config.example.h` to
`TrapMasterRelay/TrapMasterRelay.local.h` beside the sketch. The sketch automatically
loads that local file when present. It is ignored by Git and must never be committed
or shared.

For a fresh setup from a GitHub clone, run this from the repository root instead:

```bash
bash artifacts/lora-relay/create-local-config.sh
```

The script uses OpenSSL to generate a new AES key and HMAC key locally, creates both
ignored configuration files, and does not print the key values. Run it only once per
gateway/relay group. The HMAC value can be read from the local gateway file and
entered into the terminal's physical settings screen.

For the current test setup, every relay is configured with the same planned output
mapping. Confirm the physical wiring before field use:

```cpp
#define TM_RELAY_GPIO 4
#define TM_RELAY_ACTIVE_LEVEL LOW
```

Open the matching fixed Arduino project and upload it to that relay. The fixed
project overrides any legacy `TM_MACHINE_ID` in the private file, so no machine
letter must be edited or copied between uploads:

```text
Machine A → TrapMasterRelayA/TrapMasterRelayA.ino
Machine B → TrapMasterRelayB/TrapMasterRelayB.ino
Machine C → TrapMasterRelayC/TrapMasterRelayC.ino
Machine D → TrapMasterRelayD/TrapMasterRelayD.ino
Machine E → TrapMasterRelayE/TrapMasterRelayE.ino
Machine F → TrapMasterRelayF/TrapMasterRelayF.ino
Machine G → TrapMasterRelayG/TrapMasterRelayG.ino
Machine H → TrapMasterRelayH/TrapMasterRelayH.ino
```

1. Define `TM_RELAY_GPIO` to the **verified** output pin used by your relay board.
   There is no default deliberately: the Wireless Stick V3 pinout and actual wiring
   must be checked against Heltec's official diagram before energizing a relay.
3. Confirm whether the relay is low-level or high-level triggered. The default
   `TM_RELAY_ACTIVE_LEVEL` is `LOW` for common low-level trigger modules.
4. Confirm relay supply voltage under load with a multimeter. Do not assume the board's
   5V pin can power the module; use a separate verified 5V supply or a 3.3V-compatible
   relay when appropriate, with grounds tied together.
5. Define `TM_PROTOCOL_KEY_BYTES` as the same private 16-byte key used by the gateway.
   Normal builds deliberately fail without it. `TM_ALLOW_INSECURE_BENCH_KEY` is only
   permitted for a disconnected radio bench test.

Example private compile-time settings:

```cpp
#define TM_RELAY_GPIO 4 // only after physical pin verification
```

## Safety behavior

- Relay output starts inactive at boot.
- Only a valid AES-128-GCM packet addressed to this relay can schedule a pulse.
- Counters must strictly increase; replayed frames are rejected before any GPIO change.
- Invalid authentication, wrong machine ID, malformed packets, and old counters never
  pulse the relay.
- The latest accepted counter is saved in NVS before each pulse, so a relay reboot
  does not make a captured command replayable.
- Machines A–G accept a packet, pulse for 300 ms by default, then send an
  authenticated ACK back to the gateway.
- Machine H accepts exactly one authenticated packet and closes its dry contact
  once. The connected H machine is responsible for launching H2 after H1; the
  relay never adds an H2 pulse or timing of its own.

If a relay must be reset or replaced, do not erase its replay state while keeping the
same AES key. Re-key the gateway and all relays together first, while every trap is
disconnected. A fresh relay with the current key can safely learn the gateway's next
counter; an old relay will intentionally reject a gateway whose counter was reset.

## Radio-only bench stage

`TM_ENABLE_UNENCRYPTED_BENCH_TEST` is an explicit, compile-time-only test switch.
When enabled on both gateway and relay, `/bench-fire?machine=<ID>` transmits a simple
unencrypted three-byte packet to validate range and GPIO behavior. Keep the relay
disconnected from every real trap in this mode. This mode additionally requires
`TM_BENCH_INTERLOCK_GPIO` on both boards: wire a verified, normally-open local
test-enable switch/jumper that pulls this input low only during the bench test. The relay
rejects plaintext bench packets unless that physical interlock is asserted. Rebuild
without the switch and without `TM_ENABLE_UNENCRYPTED_BENCH_TEST` before field use.
Normal builds do not include this endpoint or accept this packet type.