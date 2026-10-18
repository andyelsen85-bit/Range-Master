#pragma once
#include <stdint.h>
// Physical remote, touch controls and every scorecard share this contract.
constexpr uint32_t CLR_SCORE_0 = 0x000000;
constexpr uint32_t CLR_SCORE_1 = 0xFFD600;
constexpr uint32_t CLR_SCORE_2 = 0x22C55E;
constexpr uint32_t CLR_BTN_START = 0xDC2626;
constexpr uint32_t CLR_BTN_REPEAT = 0x2563EB;
constexpr uint32_t CLR_BTN_SKIP = 0xFFFFFF;
inline uint32_t score_color(int score) {
    return score == 2 ? CLR_SCORE_2 : score == 1 ? CLR_SCORE_1 : CLR_SCORE_0;
}
