// Arduino IDE entry point for the nested TrapMaster relay sketch.
//
// Keep this file in a folder with the same name as the sketch. Load the
// private local header here so it is available to the shared implementation.
// Accept both the recommended nested location and the legacy parent location.
#if __has_include("TrapMasterRelay.local.h")
#include "TrapMasterRelay.local.h"
#elif __has_include("../TrapMasterRelay.local.h")
#include "../TrapMasterRelay.local.h"
#endif

#include "../TrapMasterRelay.ino"