---
name: Terminal shutdown behavior
description: Confirmed product decision for the on-screen terminal shutdown flow.
---

The terminal's Shutdown action enters ESP32 deep sleep, not a hardware power-cut GPIO path. It wakes only by reset or power cycle.

**Why:** The operator explicitly chose deep sleep for the terminal hardware currently in use.

**How to apply:** Keep shutdown gated by a completed successful portal sync. If WiFi is unavailable, sync cannot start, or sync fails, leave the terminal awake and make the failure visible; do not add a force-shutdown bypass without a new operator decision.