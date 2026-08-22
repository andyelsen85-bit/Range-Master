#pragma once

// Copy this file to TrapMasterGateway.local.h in this same folder.
// TrapMasterGateway.local.h is ignored by Git and must never be committed.

// Use the exact same 16 bytes in every relay's local configuration.
#define TM_PROTOCOL_KEY_BYTES { \
    0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00  \
}

// This must match the value entered in the terminal's physical settings.
#define TM_GATEWAY_AUTH_KEY "replace-with-a-long-private-hmac-key"