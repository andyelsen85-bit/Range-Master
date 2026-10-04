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

// The machine identity is selected by the fixed Arduino project wrapper.
// Do not define TM_MACHINE_ID in this shared configuration file.

// SRD-05VDC-SL-C boards with a single NPN driver are active-HIGH:
// LOW = coil off / normally-closed contacts closed; HIGH = coil on / contacts open.
#define TM_RELAY_GPIO 4
#define TM_RELAY_ACTIVE_LEVEL HIGH