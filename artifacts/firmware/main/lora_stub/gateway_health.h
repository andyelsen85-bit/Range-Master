#pragma once
#include <stdint.h>

// Worker-owned, RAM-only check history. Retries within one check do not count
// as separate failures; scope changes reset the streak, never the boot total.
struct GatewayHealthHistory {
    uint32_t consecutive = 0;
    uint32_t failed_since_boot = 0;
    void success() { consecutive = 0; }
    bool failure() {
        if (failed_since_boot != UINT32_MAX) ++failed_since_boot;
        if (consecutive != UINT32_MAX) ++consecutive;
        return consecutive >= 3;
    }
};