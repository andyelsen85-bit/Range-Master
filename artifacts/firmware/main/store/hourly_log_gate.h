#pragma once
#include <stdint.h>

// Owner holds the diagnostics mutex. Monotonic time ignores SNTP/timezone jumps.
struct HourlyLogGate {
    bool seen=false;
    int64_t last=0;
    bool admit(int64_t now) {
        if(seen && now>=last && now-last<3600LL*1000000) return false;
        seen=true;last=now;return true;
    }
};
