// Arduino entry point: ID is fixed for this handheld. Clone only with another
// unique ID and a counter/key provisioning plan; never deploy two ID '1's.
#define TM_REMOTE_ID '1'
#if __has_include("TrapMasterRemote.local.h")
#include "TrapMasterRemote.local.h"
#elif __has_include("../TrapMasterRemote.local.h")
#include "../TrapMasterRemote.local.h"
#endif
#include "../TrapMasterRemote.ino"
