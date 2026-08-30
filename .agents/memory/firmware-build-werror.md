---
name: Firmware warning policy
description: Portability constraints from the Windows ESP-IDF firmware toolchain.
---

The target firmware build treats all warnings as errors, including GCC's restrict-overlap and format-truncation diagnostics. Formatting data copied from one member of a large shared store into another should use a separate temporary buffer, and fixed-format timestamps should be assembled with explicitly bounded pieces.

**Why:** The Windows ESP-IDF build rejected otherwise valid behavior because GCC could not prove that a store-member source and destination did not overlap, and could not prove that unconstrained integer format arguments fit the timestamp buffer.

**How to apply:** Preserve the strict warning policy; fix the proof boundary in code rather than weakening compiler flags. Re-run the Windows target build after firmware changes because the local environment may not contain `idf.py`.