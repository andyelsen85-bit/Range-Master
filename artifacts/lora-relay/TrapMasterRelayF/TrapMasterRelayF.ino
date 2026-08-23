// Fixed Arduino IDE project for machine F.
#if __has_include("../TrapMasterRelay.local.h")
#include "../TrapMasterRelay.local.h"
#elif __has_include("../TrapMasterRelay/TrapMasterRelay.local.h")
#include "../TrapMasterRelay/TrapMasterRelay.local.h"
#endif
#ifdef TM_MACHINE_ID
#undef TM_MACHINE_ID
#endif
#define TM_MACHINE_ID 'F'
// The shared sketch shows this fixed ID on the onboard OLED at boot.
#include "../TrapMasterRelay.ino"