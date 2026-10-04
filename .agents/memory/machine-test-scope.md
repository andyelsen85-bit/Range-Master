---
name: All-machine test scope
description: User-approved safety scope for the Settings all-machine test.
---

“Testauslösung Alle Maschinen” means only machines currently enabled in Settings,
not all A–H regardless of their enabled state.

**Why:** The user explicitly selected “Only enabled machines” when asked whether
the all-machine test should include disabled machines.

**How to apply:** Keep bulk machine tests enabled-only and do not bypass disabled
machines. The operator was informed that one press sends sequential radio
commands, not exactly simultaneous launches.

Bulk tests must pause for two seconds after the previous machine's completed
gateway response/ACK wait, before requesting the next machine's nonce and fire.
This spacing applies only to the all-machine test, not normal game launches.

**Why:** The user observed conflicts and unreliable ACK reception when the bulk
test fired machines too quickly, and explicitly requested two-second pauses.

**How to apply:** Keep the pause on the HTTP worker, never the LVGL task. Preserve
enabled-only selection and stop-on-error behavior; do not delay the first shot or
add a trailing pause after the last shot.