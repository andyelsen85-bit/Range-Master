---
name: Non-blocking sync publication
description: UI responsiveness and shared-state safety rules for terminal synchronization.
---

Network sync must remain non-modal: HTTP requests, retries, JSON parsing, NVS commits, and FAT cache writes run while LVGL, touch, navigation, gameplay, and Catering continue normally. Only a bounded in-memory shared-dataset publication may request a brief UI acknowledgement and temporarily gate touch.

**Why:** Pausing the UI for the entire sync made the display and every terminal function appear frozen for the full duration of network retries and storage work. Removing the pause without a commit handshake would instead expose LVGL callbacks to torn shared-store data.

**How to apply:** Stage network results privately, request the UI commit acknowledgement immediately before replacing shared RAM, release it before any persistence, and publish one final generation after all commits. Audit helper side effects and outbox completion projections, not just direct assignments.

Sync must not dismiss open game results or reset the operator's selection and
scorecard scroll position. Opening another game or navigating away is a user
action; a reordered or evicted history-cache entry is not.

**Why:** The user reported that sync refreshes made results disappear in
Spielverlauf and explicitly said this must not happen.

**How to apply:** Keep the selected finished game independent of cache indices.
Refresh the list only when its data changes. Enrich missing clay data by stable
game identity without hiding the card, changing player rows, or losing scroll.