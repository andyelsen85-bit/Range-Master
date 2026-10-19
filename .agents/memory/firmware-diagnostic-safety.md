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
