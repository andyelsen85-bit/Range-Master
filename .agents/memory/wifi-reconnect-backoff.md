---
name: WiFi reconnect backoff
description: Safe retry behavior for the P4 supervisor controlling WiFi through the ESP-Hosted C6
---

Remote WiFi disconnect events must wake the supervisor so an established connection can recover, but the wake itself must not start a connection attempt. It only transitions the supervisor into its timed exponential-backoff path. Explicit operator connection requests may bypass the wait.

**Why:** Treating a disconnect wake like a normal queued command bypasses the timer and creates a rapid retry loop when an access point is unavailable. Removing the wake entirely leaves a supervisor blocked forever after an established connection drops.

**How to apply:** Keep disconnect notifications distinct from manual credential commands. After a notification, wait for the current backoff interval before reconnecting; reset backoff only for a successful connection or an explicit operator request. Do not issue a redundant disconnect when the station is already offline.