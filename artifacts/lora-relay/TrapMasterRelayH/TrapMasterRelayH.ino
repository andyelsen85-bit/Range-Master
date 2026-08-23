// Fixed Arduino IDE project for machine H. It sends one dry-contact trigger;
// the connected H machine controls its own H1/H2 sequence.
#if __has_include("../TrapMasterRelay.local.h")
#include "../TrapMasterRelay.local.h"
#elif __has_include("../TrapMasterRelay/TrapMasterRelay.local.h")
#include "../TrapMasterRelay/TrapMasterRelay.local.h"
#endif
#ifdef TM_MACHINE_ID
#undef TM_MACHINE_ID
#endif
#define TM_MACHINE_ID 'H'
#include "../TrapMasterRelay.ino"