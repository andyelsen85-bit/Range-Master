---
name: Gateway worker concurrency
description: Safe handoff rules between the terminal's LVGL task and its asynchronous gateway HTTP worker
---

Terminal gateway requests must copy the URL and HMAC key when the operator initiates an action. Any worker-produced status must be synchronized and copied into caller-owned storage before the LVGL task displays it.

**Why:** The UI and HTTP worker can run independently; reading mutable settings late can redirect a queued command or authenticate it with a different key, while a shared mutable status buffer can yield torn operator feedback.

**How to apply:** When adding gateway actions, queue an immutable request snapshot and use the established synchronized status/result handoff. Do not perform network I/O from LVGL callbacks.

Health-check throttle timestamps are purpose-specific: protect each timestamp with the gateway state mutex, and keep operator-triggered checks independent from autonomous polling.

**Why:** Manual and background checks have different user-visible purposes; sharing a timestamp can make an invisible background poll suppress an explicit operator action, while unsynchronized access creates a cross-task data race.

**How to apply:** Use separate manual and automatic throttle state and access it only through synchronized helpers. Record the timestamp when the corresponding request is admitted.