#pragma once

// Copy this file to TrapMasterRelay.local.h in this same folder.
// TrapMasterRelay.local.h is ignored by Git and must never be committed.

// Use the exact same 16 bytes in TrapMasterGateway.local.h and every relay.
#define TM_PROTOCOL_KEY_BYTES { \
    0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00  \
}

// Flash the same sketch once for each machine, changing only this value.
#define TM_MACHINE_ID 'A'
#define TM_RELAY_GPIO 4
#define TM_RELAY_ACTIVE_LEVEL LOW