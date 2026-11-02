#pragma once

// Copy this file to TrapMasterGateway.local.h.
// TrapMasterGateway.local.h is ignored by Git and must never be committed.

// Use the exact same 16 bytes in TrapMasterRelay.local.h and every relay.
#define TM_PROTOCOL_KEY_BYTES { \
    0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00  \
}

// Optional separate remote-to-gateway AES key. Use the exact same FRESH 16
// bytes on all handhelds; DIFFERENT from TM_PROTOCOL_KEY_BYTES.
// Leave TM_PROTOCOL_KEY_BYTES unchanged when relays cannot be reflashed.
// #define TM_REMOTE_KEY_BYTES { sixteen fresh private byte values }

// This must match the value entered in the terminal's physical settings.
#define TM_GATEWAY_AUTH_KEY "replace-with-a-long-private-hmac-key"