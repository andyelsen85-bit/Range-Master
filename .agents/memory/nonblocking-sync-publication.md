---
name: Non-blocking sync publication
description: UI responsiveness and shared-state safety rules for terminal synchronization.
---

Network sync must remain non-modal: HTTP requests, retries, JSON parsing, NVS commits, and FAT cache writes run while LVGL, touch, navigation, gameplay, and Catering continue normally. Only a bounded in-memory shared-dataset publication may briefly share the LVGL frame lock. Never disable/reset touch or add an input-intercepting overlay; defer publication during active touch/scrolling and while a shot is being handled.

**Why:** The user reported lost score taps and interrupted interaction during sync. Pausing input, even with a transparent guard, loses gestures. Removing synchronization altogether would instead expose LVGL callbacks to torn shared-store data.

**How to apply:** Stage network results privately, acquire the same short lock used by the LVGL frame immediately before replacing shared RAM, release it before any sync persistence, and publish one final generation. Preserve filters, forms, keyboard/focus and stable selections; skip unchanged rebuilds. Audit helper side effects and outbox completion projections, not just direct assignments.

Sync must not dismiss open game results or reset the operator's selection and
scorecard scroll position. Opening another game or navigating away is a user
action; a reordered or evicted history-cache entry is not.

**Why:** The user reported that sync refreshes made results disappear in
Spielverlauf and explicitly said this must not happen.

**How to apply:** Keep the selected finished game independent of cache indices.
Refresh the list only when its data changes. Enrich missing clay data by stable
game identity without hiding the card, changing player rows, or losing scroll.

LVGL's `lv_obj_is_valid()` is not a safe lifetime probe for a captured widget
pointer after a screen refresh. Deleted objects must invalidate any outstanding
snapshot references before their storage is released; a recreated row is not
necessarily the same player, even if it occupies the same position or address.

**Why:** A real terminal crashed with a load-access fault while opening Spieler
des Tages after boot sync; the freed pointer contained a `0x55` poison pattern.
Deferring sync during active input prevented interrupted touches but did not
make retained pointers safe across later rebuilds.

**How to apply:** Track deletion under the shared LVGL lock; restore only
surviving objects. Drop focus for deleted rows rather than guessing which new
row should inherit it. Test both deletion and survival under the host LVGL
AddressSanitizer build, then verify on hardware when available.

## Successful flash writes must not be on the scoring path

Keep per-score durability out of touch and remote handlers; successful FAT
writes must be evaluated for latency, not just checked for correctness.

**Why:** Fixing invalid FAT names turned instant EINVAL failures into successful
fsyncs taking about 600 ms on the real terminal. This exposed previously hidden
input latency and made exhaustive boot probes take roughly two minutes.

**How to apply:** Coalesce active checkpoints off the UI thread, preserve
explicit durability boundaries for physical FIRE and game completion, and
measure boot/input/flash times on the real target. Host filesystem timing and
emulator responsiveness cannot establish those hardware acceptance criteria.

Native target compilation and CI regression checks are separate acceptance
boundaries; neither substitutes for physical performance measurements.

**Why:** The target build passed after background checkpointing was introduced,
but the extracted CI harnesses still assumed synchronous writes and one-stage
doublette undo, and lacked a newly required SDK-mock include.

**How to apply:** When modifying those boundaries, keep harness dependencies
and fault injection consistent with the intended contract, while asserting
the new no-I/O and two-stage behaviour explicitly. Run existing CI checks as
CI checks; never present their results as hardware timing proof.