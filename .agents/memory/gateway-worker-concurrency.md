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

A gateway is shared by multiple terminals; terminal HTTP fire authorization must
not require those terminals to coordinate a monotonic sequence counter. After an
ambiguous fire timeout, retry the original authorization only—never automatically
create a newly authorized fire. Preserve the separate LoRa counter and AES-GCM
security model when changing terminal-to-gateway authorization.

**Why:** The user requires simultaneous control from multiple terminals and
at-most-once trap actuation, including lost responses and interrupted pairs.

**How to apply:** Keep this constraint in gateway transport/auth changes and
configuration restore work. Busy feedback must be explicit; it must not queue an
unexpected later launch or be treated as a stale terminal counter.