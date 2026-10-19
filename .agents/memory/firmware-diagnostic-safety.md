---
name: Firmware diagnostic safety
description: Why optional diagnostics need independent stack budgets, post-UI startup, and a recovery boundary that preserves essential networking.
---

Treat diagnostic logging as a potentially deep call path, not a lightweight
operation. Keep diagnostic workers optional and outside the boot-critical path.

**Why:** Physical ESP32-P4 testing showed a census task overflowing its small
stack in the UART/console logging path, before the UI could become usable.
Native compilation and passing logical regressions did not expose this failure.

**How to apply:** Use bounded short log lines and explicit stack budgets, then
measure high-water marks on the real target. Capture short-lived workers'
watermarks before deletion; a delayed monitor must never inspect a deleted
task handle. Panic-loop recovery may disable optional probes, but it must
retain game persistence and the shared DNS/networking services used by normal
portal and FIRE traffic. Do not make an optional test's initializer the owner
of those required services.

Background flash probes must resume from committed pattern progress after idle
interruptions rather than restart or silently exit. Log the exact pause reason,
and require two seconds of checkpoint-writer idle time between active work and
diagnostic writes. Keep filename validation I/O-free and avoid repeating the
volume/journal write probes before the first usable UI.

**Why:** Physical logs showed a nearly 20-second boot and a full FAT test that
never completed because ordinary game/checkpoint activity aborted its worker.

**How to apply:** Retain journal recovery scans and pending-data safeguards;
defer diagnostic round trips, not essential recovery, and distinguish measured
device timing from native compilation or deterministic host tests.
