---
name: Gateway ACK validation
description: Hardware confirmation of the authenticated terminal-to-gateway-to-relay radio path
---

The authenticated FIRE request, encrypted LoRa transmission, relay response, and encrypted ACK have been confirmed working together on hardware.
The addressed relay has also been physically triggered successfully during the bench test.

**Why:** This establishes that the shared AES key, gateway HMAC key, machine addressing, LoRa parameters, and ACK timing are compatible in the real devices, not only in host-side checks.

**How to apply:** Treat a gateway result of `sent + ACK` as confirmation of the radio/authentication path. Continue physical relay-pulse and replay-safety tests with the trap disconnected before field use.