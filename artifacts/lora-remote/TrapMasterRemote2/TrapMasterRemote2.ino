// Arduino entry point for the SECOND physical handheld. Shared implementation;
// never flash this ID onto another handheld already using ID '2'.
#define TM_REMOTE_ID '2'
#if __has_include("TrapMasterRemote.local.h")
#include "TrapMasterRemote.local.h"
#elif __has_include("../TrapMasterRemote.local.h")
#include "../TrapMasterRemote.local.h"
#endif
static_assert(TM_REMOTE_ID=='2',"Remote2 must use sender ID '2'");
#include "../TrapMasterRemote.ino"
