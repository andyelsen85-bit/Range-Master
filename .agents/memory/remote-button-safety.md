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
constraints. Later phone-mode additions preserve the physical-input rules.

**How to apply:** Keep physical wiring and trap-disconnected commissioning
explicit; a radio ACK proves receipt, not terminal action or trap operation.

Phone controls have two independent authorization toggles for the three
shooting buttons and the three score buttons. The user explicitly chose
phone toggles independent of the physical key switches; physical buttons
still require their physical switch. Phone permissions are session-bound.

**Why:** The user explicitly confirmed independent phone authorization rather
than requiring the physical keys to be enabled for phone use.

**How to apply:** Never replace the existing physical gate with the phone gate
or require both. Both input sources must share the durable sender counter and
exact-frame retry pipeline; revocation must not authorize a later retry.

Phone machine testing is a separate page and must use the selected terminal's
configured machine-test function. It does not need to work with the terminal
off. Address tests to one stable terminal identity rather than broadcasting
them to every enabled terminal on a shared gateway.

**Why:** The user explicitly chose terminal-mediated tests, independent of a
game; shared-gateway normal-button broadcast must not accidentally test traps
at several terminals.

**How to apply:** Keep normal six-button compatibility, reject disabled
machines and active games, and never claim a radio ACK proves a trap fired.

For duplicate-handheld identity recovery, the user reports that only gateway
and remotes are accessible, not relays. Preserve the relay AES key and use a
fresh, separate remote-to-gateway key; do not assume all boards can be reflashed.
Remote identity changes require explicit migration with a changed key and
must preserve counters. New remote-key replay state must not delete old ledgers.

**Why:** Two physical remotes were configured with the same sender identity.
Full shared-key rotation cannot currently include the inaccessible relays.

**How to apply:** Confirm access before proposing relay-key rotation. Explain
that key-domain separation prevents future remote collisions but cannot undo
historical nonce reuse; relay-key rotation remains a later security cleanup.
