---
name: Daily backup and inventory policy
description: User-approved backup cap, empty-stock counts and automatic Gameday deployment migration
---

Keep a strict limit of one non-revoked configuration snapshot per terminal
per Luxembourg calendar day, even when that day's snapshot is restore-approved.
Do not replace the protected snapshot or create a second snapshot as an exception.
A blocked backup must not fail ordinary terminal sync.

**Why:** The user explicitly chose the strict daily limit when asked how to
handle restore-approved snapshots.

**How to apply:** Preserve this policy in upload, retention, migration and retry
changes; approvals are not permission to bypass the daily cap.

Opening and closing ammunition counts may be zero for empty stock. Counts use
the same unit as ammunition sales, not an independently inferred unit.

**Why:** The user explicitly approved zero opening/closing stock and specified
consistent inventory/sales units.

**How to apply:** Do not reintroduce positive-only validation or convert counts
to shells, boxes or cases without a corresponding product-unit decision.

Gameday schema changes must run automatically during the existing deployment
migration step, not be assigned to the operator as manual SQL and not run as
DDL in the API-server startup.

**Why:** The user explicitly requires automatic deployment migration and wants
conflicting restore approvals to stop the new server from starting, rather
than silently removing protected backups.

**How to apply:** Keep migration transaction ownership with the migrator and
retain the fail-closed backup checks. Document which obsolete approvals an
administrator must explicitly revoke before retrying, without weakening the
strict daily cap.
