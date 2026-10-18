---
name: Storage recovery boundaries
description: Why journal filenames stay 8.3 and dangerous offline recovery uses a separate physically provisioned admin PIN.
---

## Whole-volume filename constraint

Keep every terminal FAT filename 8.3, not just journal records: active-game
checkpoints, cache snapshots, queue files, probes and scratch files too.
Replace extensions; never append `.tmp` or `.bak` to an existing extension.
Keep game UUIDs inside records rather than filenames.
Do not solve a future naming problem by silently enabling long filenames or
reformatting the volume.

**Why:** The user required this compatibility boundary after the terminal
reported EINVAL for UUID-based filenames despite having ample free FAT space.
Unsynced games must survive firmware changes and filesystem recovery.
The user repeated the audit requirement after a missed active-checkpoint path
failed on hardware. POSIX permits second-dot/long names, so ordinary host
filesystem success is not proof that a path works with FatFs LFN disabled.

**How to apply:** New journal write/recovery paths must obey the short-name
contract. Legacy data may be retired only after a validated replacement or
explicit, exported, PIN-authorized discard.

## Recovery administrator is separate from catering

Do not reuse the Catering PIN or require an online portal login for destructive
pending-game recovery. Initial provisioning of the separate recovery-admin PIN
is physical-only; a local web request must not be able to claim an unset PIN.

**Why:** Offline rescue must work when the portal is unreachable. Permission to
operate catering is not permission to discard unsynced game data.

**How to apply:** Enforce authorization below both touch and web interfaces,
require a complete unchanged export and two confirmations for discard, and
never treat export alone as portal acknowledgement. The device's plain-HTTP
configuration interface belongs on a trusted local network.
