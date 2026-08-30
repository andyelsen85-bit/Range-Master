---
name: Non-blocking sync publication
description: UI responsiveness and shared-state safety rules for terminal synchronization.
---

Network sync must remain non-modal: HTTP requests, retries, JSON parsing, NVS commits, and FAT cache writes run while LVGL, touch, navigation, gameplay, and Catering continue normally. Only a bounded in-memory shared-dataset publication may request a brief UI acknowledgement and temporarily gate touch.

**Why:** Pausing the UI for the entire sync made the display and every terminal function appear frozen for the full duration of network retries and storage work. Removing the pause without a commit handshake would instead expose LVGL callbacks to torn shared-store data.

**How to apply:** Stage network results privately, request the UI commit acknowledgement immediately before replacing shared RAM, release it before any persistence, and publish one final generation after all commits. Audit helper side effects and outbox completion projections, not just direct assignments.