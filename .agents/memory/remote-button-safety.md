---
name: Wireless remote safety contract
description: User-approved counter/ACK and connection-signature rules for the six-button wireless handheld.
---

Gateway remote counters must be durable in NVS **before** sending an ACK.
Equality is a retry: re-ACK without another event or NVS write. If flash
misses the 150 ms ACK window, the handheld retries its same counter/frame.

**Why:** The user explicitly approved this correction to prevent a reboot
between ACK and persistence from accepting the same button again.

**How to apply:** Preserve this ordering and duplicate behavior in any
gateway/handheld transport change; never "improve" retries by allocating
a new counter for an ambiguous result.

Event HMACs must bind the fresh client nonce, server hello nonce and age.
HTTP fallback also needs a fresh client nonce per request and signatures
bound to that request. Retain ID deduplication and the 3-second stale rule.

**Why:** The user explicitly approved connection binding so captured events
from an older connection cannot operate a terminal after reconnect.

**How to apply:** Change all three firmware peers together if encoding changes,
and preserve compatibility for existing relay FIRE/ACK frames.

All handheld GPIOs remain mandatory verified build-time definitions,
without guessed production defaults. Keys are independent authorization
inputs, not switches in the common ground bus. Remote and touch must use
one firing guard; reconnect must never authorize another shot. Reception
defaults off, and every enabled terminal on a shared gateway reacts.

**Why:** The user's self-contained hardware specification requires these
constraints and says no other protocol changes beyond the approved fixes.

**How to apply:** Keep physical wiring and trap-disconnected commissioning
explicit; a radio ACK proves receipt, not terminal action or trap operation.
