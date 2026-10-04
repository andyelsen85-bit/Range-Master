---
name: Relay output polarity
description: Required GPIO polarity and contact behavior for the TrapMaster relay boards.
---

The relay modules in use are SRD-05VDC-SL-C boards driven through a single NPN
transistor. They are active-HIGH. GPIO LOW means the coil is de-energized and the
normally-closed contact pair is closed. A fire pulse drives GPIO HIGH, energizes
the coil, opens the contact pair, and then returns GPIO LOW.

**Why:** The prior active-LOW assumption was incorrect for the installed relay
boards and would invert the intended idle and fire behavior.

The user confirmed “All receivers working” after applying the active-HIGH
correction. Treat this polarity as hardware-confirmed, not an assumption.

**How to apply:** Keep the fixed and generic receiver firmware, local/example
configuration, generated configuration, and all relay documentation aligned to
active-HIGH with an explicit LOW idle state. Do not alter authentication, replay
protection, or counter persistence when changing this polarity.