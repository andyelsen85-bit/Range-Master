# Real-terminal performance validation

## Scope and current evidence

These changes target the ESP32-P4 terminal and Heltec Wireless Stick V3 gateway.
No portal changes, emulator execution, device flash, NVS erase or FAT formatting
is part of this work. Native target compilation is not hardware validation.
There is no connected serial terminal/gateway in this workspace, so the runtime
acceptance checks below remain **unverified**. Do not replace them with host
timings or invented device logs.

## Behaviour

- Boot validates the entire filename registry without file I/O. Only the small
  volume write probe calls fsync before UI ready. Journal scanning/recovery is
  retained; the journal self-test and second volume write probe run after UI
  ready, at low priority, with no active game and writer idle for two seconds.
  Boot logs durations for settings, cache load, journal scan, restore,
  coprocessor and display, plus `write_probe_ms`.
  The full pattern probe's NVS version combines the pattern registry, filename
  helper version and firmware ELF identity. Unchanged boots skip full testing.
  Each successful pattern commits `progress_ver`/`next` in the `fat_probe` NVS
  namespace. Idle interruptions log their precise reason and resume in the same
  worker; reboot resumes at the committed index. A changed identity resets the
  index. Completion is marked only after every pattern passes. Flash failures
  retry later; no failed/aborted run is marked complete.
- Score entry changes RAM and widgets only. One low-priority writer coalesces
  checkpoints after 250 ms idle. A/B snapshots retain the last committed slot;
  each new snapshot uses one fsync and no backup rewrite. Identical payloads
  are skipped. A cold power cut can therefore lose scores entered during the
  coalescing/flash interval; it must never authorize an automatic repeat fire.
- FIRE reserves the current launch in RAM, rechecks cancellation/navigation
  before transmission (including after nonce preparation), transmits without
  waiting for FAT, then requests an immediate asynchronous checkpoint.
  `fire_ms` and `start_to_fire_ms` measure button press to admission reply;
  HTTP 202 remains admission only, not relay acknowledgement.
  Restored active games always treat the current unit as an uncertain launch,
  even when the saved v2 flag was false or an explicit repeat had been selected.
  WDH must first explicitly release that unit again; reboot never authorizes
  automatic fire. Recently uncheckpointed scores can still be lost on power cut.
- Final scoring queues a complete finished-game snapshot. The writer saves the
  journal (or emergency NVS fallback) before publishing results. A progress
  message is displayed while finishing; further scoring and cancellation are
  refused during that transition. Credit cancellation retains its existing
  synchronous durable VOID_USE boundary, never a compensating GRANT.
- Automatic ping tests are disabled. The settings button runs at most 10 seconds
  with one raw ICMP socket, at 1-second send intervals. Starting/continuing a
  game aborts the diagnostic. Fewer than four free sockets refuses admission;
  a socket-exhaustion failure stops the whole diagnostic.
- A single mutex-owned persistent gateway HTTP handle serves command, nonce,
  health, trace and fallback-event requests. Error paths destroy the handle.
  The gateway itself has two bounded keep-alive HTTP peer slots and retains the
  existing authenticated parser/handlers. The terminal socket limit is 24.
- Gateway HTTP/WebSocket FIRE admission returns 202 immediately. Transmission,
  optional relay ACK and doublette delays advance in the main loop without
  waiting loops. Remote frames can be received, durably deduplicated and ACKed
  while a FIRE waits for its relay ACK or pair delay (radio TX is half-duplex).
  **202 is acceptance, not proof of physical launch.** For game launches the
  terminal checks completion with the exact same signed nonce, without renewing
  it or issuing a new command. Only observed ACKs add confirmed-clay counts.
  Game completion waits for that bounded receipt check before journaling counts.
- The gateway WiFi setup page exposes **Relay ACK erforderlich**, persisted in
  NVS and defaulting OFF. Turn it ON when relays requiring ACKs are installed.
  Status exposes `relayAckRequired`. HTTP acceptance latency and radio completion
  are deliberately separate.
- C6 version RPC is attempted no more than twice per boot, with one warning.
  **The C6 slave still needs flashing with tools/flash_c6_slave.sh.**
- WDH at an unscored doublette first clears the launch flag without changing
  player/run/position. A second WDH undoes the previous complete launch unit.
  A pair with its first result already entered is not silently relaunched.

## Safe hardware procedure

### Socket configuration and panic recovery

An existing `sdkconfig` overrides `sdkconfig.defaults`. Defaults request
`CONFIG_LWIP_MAX_SOCKETS=24`; compiling with fewer than 16 now fails at a
static assertion. Back up the local configuration outside the project, preserve
board-specific overrides, then run from `artifacts/firmware`:

```sh
rm sdkconfig && idf.py reconfigure
idf.py build
```

This is build configuration only, never permission to erase NVS/FAT. Confirm
the actual serial boot line says `CONFIG_LWIP_MAX_SOCKETS=24`.

The census starts after `UI ready` on an 8192-byte PSRAM stack. Counts/heap and
each open descriptor are separate short lines; there is no aggregate peer
buffer on its stack. Its first run reports `task=socket_census stack_hwm=N bytes`.
The background FAT test uses an enlarged 8192-byte **internal** stack because
its NVS/FAT writes can enter cache-off flash paths.
After 60 seconds, expect one report each for `game_writer`, `fat_selftest`,
`network_test` and `gateway_http`. Finished FAT tasks retain a saved watermark
and report `state=finished`; deleted task handles are never inspected.
Any value below 1024 bytes must be WARN and investigated on the physical target.

Three consecutive `ESP_RST_PANIC` resets trigger a protective boot: census,
background FAT selftest and network diagnostic tasks are not created, including
when the manual test button is pressed. Essential game storage and gateway work
remain enabled. RTC NOINIT holds a checked counter without changing NVS.
After the UI stays up 60 seconds, the streak is cleared, but optional tasks stay
off for that boot. A non-panic reset starts a new consecutive-panic sequence.
Verify the single `boot` WARN and usable UI during controlled panic-recovery
testing on a safe, disconnected physical test rig.

### Flash and measure

1. Synchronize durable pending data. Keep storage partitions and NVS intact.
   Use the existing terminal USB `idf.py -p PORT flash` procedure: **never
   erase-flash**. This change does not alter the partition table.
2. Flash the matching gateway firmware separately; retain private provisioning.
   Initially disconnect physical traps and leave **Relay ACK erforderlich** OFF.
3. Capture serial logs from power-on through the whole scenario. Record firmware
   identities for terminal and gateway, boot type and WiFi conditions.
4. Measure the acceptance checks below. Repeat with an unchanged second boot
   (quick mode) and a changed build (full probe after the usable UI).
5. With traps still disconnected, inject a failed checkpoint, retry it, cancel
   a game and complete a game. Verify no duplicate launch, no lost finished
   record, no re-created GRANT, and no result screen before durable persistence.
6. Only after disconnected checks pass, validate configured real relay ACKs
   under the range's normal physical safety procedure.

## Required evidence (paste actual lines, not examples with fabricated values)

| Scenario | Required log / acceptance |
| --- | --- |
| Cold boot | `boot ready in N ms`, N < 12000 on an unchanged build; blocking-item duration lines |
| Name probe | I/O-free `mode=quick`, and, on changed builds only, `mode=full complete=1` after UI ready; `paused reason=... next=N/37` then resume without restarting completed patterns |
| 10 touch scores | Ten `score_ui_ms=... source=touch queue_depth=...` entries; verify visible post highlight and next state <100 ms |
| 10 remote scores | Ten `score_ui_ms=... source=remote queue_depth=...` entries, including event-queue residence |
| Checkpoints | `checkpoint_ms=... nvs_ms=0 fat_ms=... queue_depth=... ok=1`; settings NVS is logged separately as `settings_nvs_ms` |
| Coalescing | Rapid inputs may produce fewer checkpoints than scores; wait >250 ms after isolated scores to get one checkpoint per score |
| Complete game | No `ping_sock`, `create socket failed`, TLS socket errors or errno=23; finish journal/receipt counts correct |
| Five disconnected FIREs | Five `fire_ms=... http=202 ack=0` and `start_to_fire_ms=...` entries, each <300 ms; no checkpoint wait before transmission |
| Socket census | `sockets: open=... max=24 free=...`; gateway WebSocket, HTTP server listener/client, httpd UDP control and other bound UDP ports labelled; no connection attempts until WiFi is connected, retries 1/2/5/10 seconds |
| C6 | At most two version RPC attempts per boot; one mismatch/unknown warning |
| Doublette WDH | First press retains player/run/clay and clears launch; second press returns to previous complete unit, including first unit/no previous result |

`score_ui_ms` measures callback-to-widget update for touch and received-event to
widget update for remote input; it is not a panel scan-out measurement. Verify
visible response separately on the physical screen. Warning thresholds are
150 ms for score widget updates and 2000 ms for checkpoint writes.
