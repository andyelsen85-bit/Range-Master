---
name: Gateway worker concurrency
description: Safe handoff rules between the terminal's LVGL task and its asynchronous gateway HTTP worker
---

Terminal gateway requests must copy the URL and HMAC key when the operator initiates an action. Any worker-produced status must be synchronized and copied into caller-owned storage before the LVGL task displays it.

**Why:** The UI and HTTP worker can run independently; reading mutable settings late can redirect a queued command or authenticate it with a different key, while a shared mutable status buffer can yield torn operator feedback.

**How to apply:** When adding gateway actions, queue an immutable request snapshot and use the established synchronized status/result handoff. Do not perform network I/O from LVGL callbacks.