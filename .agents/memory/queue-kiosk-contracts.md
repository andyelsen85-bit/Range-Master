---
name: Queue and kiosk contracts
description: Business isolation and decisions for contradictory live/kiosk requirements.
---

Queue takeover and game start are separate voluntary operator actions. A group is
not split by default. Hiding or correcting scores changes only display/statistics,
never billing, credits, ammunition counters, inventory, daily close or journal data.

**Why:** The requested kiosk feature explicitly separates spectator/operator views
from physical launches and immutable financial operations.

**How to apply:** Keep operational queries unfiltered when changing visible-results
logic; do not connect queue signup/takeover to automatic start, launch or credit use.

The live contract deliberately prefers full bounded snapshots over the contradictory
4-KiB limit. Its `null` response is authoritative: never retain a cached RUNNING panel
or infer a FINISHED transition from null, because stale/aborted also return null.
Only persistent sequence/start metadata is allowed, not a live-payload outbox.

**Why:** Six-player full snapshots cannot fit 4 KiB, and identical null responses
cannot distinguish finish from expiry. Both contradictions were documented rather
than silently truncating results or showing stale activity.

**How to apply:** A future “BEENDET” bridge needs an explicit changed response
contract. Live retries must coalesce to the newest state rather than replay history.

Do not increase binary journal record dimensions merely to increase live capacity.

**Why:** Existing FAT records may contain unsynchronised games; changing struct sizes
without a versioned reader/migration can make those records unreadable.

**How to apply:** Preserve compatibility or implement and test a versioned migration
before changing persisted capacities. Never format or erase storage to accommodate it.

Stand-PC access uses one PIN unlock per persistent browser profile and a device
cookie. There must be no legacy access fallback. Only a public response with
HTTP 401 and exact code `kiosk_locked` may show the PIN screen; network errors,
5xx, origin rejection and business conflicts must leave the live page unlocked.
Idle reset clears selections only. This PIN is separate from terminal API keys,
portal admin sessions, catering and firmware recovery authorization.

**Why:** The user explicitly replaced link-based access and required stand PCs to
survive browser restarts without transient failures repeatedly locking the screen.

**How to apply:** Keep access epochs/device revocation server-authoritative and
never clear browser access from a generic HTTP-error handler.

After successful PIN unlock, cancel old public fetches and reset public-query
errors before remounting live widgets; invalidation alone is not enough.

**Why:** Inactive cached `kiosk_locked` errors can fire widget mount effects and
lock the screen again even though the new cookie is accepted by the server.

**How to apply:** Keep this reset scoped to public queries and preserve unrelated
admin-session caches. Cover post-revocation/reset re-unlock without a page reload.

Track cookie renewal independently from frequent device-usage updates.

**Why:** A 30-second usage timestamp otherwise never becomes a day old on an
always-active device, defeating the required daily sliding cookie renewal.

**How to apply:** Preserve a separate renewal timestamp when changing device
activity throttling or cookie lifetime.

Development browser observations can differ from direct Express cookie attributes:
the PIN cookie was Lax directly but None through the preview. The intervening
rewrite mechanism was not established.

**Why:** Treating that discrepancy as an application bug would weaken the required
production cookie policy unnecessarily.

**How to apply:** Compare direct API and proxied Set-Cookie attributes before
changing cookie code; separately verify the actual production browser/proxy path.
