---
name: Gateway WiFiManager addressing pitfalls
description: WiFiManager static-field visibility, live DHCP clearing and Wi-Fi-only reset boundaries
---

With WiFiManager 2.x, `setShowStaticFields(false)` means “show only when a static
address is already configured,” not “never show.” Native IP fields must not be
exposed alongside independently persisted custom IP fields.

**Why:** The two sets can disagree: the native values take effect during setup
but the separately saved values return after reboot, reproducing the user's
old-network static-IP problem.

**How to apply:** Maintain a single source of truth for address inputs and
verify actual portal visibility rather than relying on the boolean API name.

DHCP selection must clear both the WiFiManager instance's address state and the
station's live configuration before the next connection. Wi-Fi reset may erase
SSID/address settings but never the gateway's replay-protection counters.

**Why:** Clearing credentials alone leaves custom address state behind;
clearing the entire shared NVS namespace would instead weaken radio security.

**How to apply:** Test static-to-DHCP transitions before and after reboot and
test that a Wi-Fi-only reset preserves the anti-replay state.
