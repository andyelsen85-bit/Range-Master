#pragma once
#include "sdkconfig.h"

static_assert(CONFIG_LWIP_MAX_SOCKETS >= 16,
    "Run: rm sdkconfig && idf.py reconfigure (sdkconfig.defaults was not applied)");
