#pragma once

// Copy this file to TrapMasterRelay.local.h.
// TrapMasterRelay.local.h is ignored by Git and must never be committed.

// Use the exact same 16 bytes in TrapMasterGateway.local.h and every relay.
#define TM_PROTOCOL_KEY_BYTES { \
    0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00  \
}

// Flash the same sketch once for each machine, changing only this value:
// A, B, C, D, E, F, G, or H.
#define TM_MACHINE_ID 'A'

// Current planned test wiring for all machines. Verify before field use.
#define TM_RELAY_GPIO 4
#define TM_RELAY_ACTIVE_LEVEL LOW