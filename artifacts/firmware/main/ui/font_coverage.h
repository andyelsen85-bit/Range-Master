#pragma once
#include <stdint.h>

// Keep in sync with tools/generate_fonts.py and its compiled-font coverage test.
constexpr bool ui_codepoint_supported(uint32_t cp) {
    return (cp>=0x20 && cp<=0x7e) || (cp>=0xa0 && cp<=0x17f) ||
        cp==0x20ac || (cp>=0x2013 && cp<=0x2014) ||
        cp==0x2018 || cp==0x2019 || cp==0x201c || cp==0x201d ||
        cp==0x201e || cp==0x2026 || cp==0x2022 || cp==0x2192 ||
        cp==0x2248 || cp==0x2500 || cp==0x26a0;
}
