// Arduino IDE entry point for the nested TrapMaster gateway sketch.
//
// Keep this file in a folder with the same name as the sketch. Load the
// private local header here so it is available to the shared implementation.
// Accept both the recommended nested location and the legacy parent location.
#if __has_include("TrapMasterGateway.local.h")
#include "TrapMasterGateway.local.h"
#elif __has_include("../TrapMasterGateway.local.h")
#include "../TrapMasterGateway.local.h"
#endif

#include "../TrapMasterGateway.ino"