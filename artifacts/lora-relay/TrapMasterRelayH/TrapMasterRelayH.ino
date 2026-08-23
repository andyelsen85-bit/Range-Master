// Fixed Arduino IDE project for machine H (one command, two local pulses).
#if __has_include("../TrapMasterRelay.local.h")
#include "../TrapMasterRelay.local.h"
#elif __has_include("../TrapMasterRelay/TrapMasterRelay.local.h")
#include "../TrapMasterRelay/TrapMasterRelay.local.h"
#endif
#ifdef TM_MACHINE_ID
#undef TM_MACHINE_ID
#endif
#define TM_MACHINE_ID 'H'
// System-controlled H1→H2 interval. This is intentionally not an operator
// custom-pair setting; change only when the physical H trap requires it.
#ifndef TM_H_DOUBLE_DELAY_MS
#define TM_H_DOUBLE_DELAY_MS 1000
#endif
#include "../TrapMasterRelay.ino"