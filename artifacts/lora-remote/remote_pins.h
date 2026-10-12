#pragma once
#ifndef TM_REMOTE_MATRIX
#define TM_REMOTE_MATRIX 0
#endif
#ifndef TM_REMOTE_SLEEP_MODE
#define TM_REMOTE_SLEEP_MODE 0
#endif
#if TM_REMOTE_SLEEP_MODE != 0 && TM_REMOTE_SLEEP_MODE != 1
#error "TM_REMOTE_SLEEP_MODE must be 0 (always awake) or 1 (ULP GPIO wake)"
#endif
#ifndef TM_KEY_FIRE_GPIO
#error "Define TM_KEY_FIRE_GPIO to a verified, free header GPIO"
#endif
#ifndef TM_KEY_SCORE_GPIO
#error "Define TM_KEY_SCORE_GPIO to a verified, free header GPIO"
#endif
#if TM_REMOTE_MATRIX
#if !defined(TM_ROW0_GPIO) || !defined(TM_ROW1_GPIO) || !defined(TM_ROW2_GPIO) || !defined(TM_COL0_GPIO) || !defined(TM_COL1_GPIO)
#error "Matrix requires TM_ROW0/1/2_GPIO and TM_COL0/1_GPIO (no default pin assignments)"
#endif
static constexpr int inputPins[]={TM_ROW0_GPIO,TM_ROW1_GPIO,TM_ROW2_GPIO,
    TM_COL0_GPIO,TM_COL1_GPIO,TM_KEY_FIRE_GPIO,TM_KEY_SCORE_GPIO};
static constexpr int rows[]={TM_ROW0_GPIO,TM_ROW1_GPIO,TM_ROW2_GPIO};
static constexpr int cols[]={TM_COL0_GPIO,TM_COL1_GPIO};
#else
#if !defined(TM_BTN_FIRE_GPIO) || !defined(TM_BTN_REPEAT_GPIO) || !defined(TM_BTN_SKIP_GPIO) || !defined(TM_BTN_P0_GPIO) || !defined(TM_BTN_P1_GPIO) || !defined(TM_BTN_P2_GPIO)
#error "Define all six TM_BTN_*_GPIO values to verified, free header GPIOs"
#endif
static constexpr int buttons[]={TM_BTN_FIRE_GPIO,TM_BTN_REPEAT_GPIO,TM_BTN_SKIP_GPIO,
    TM_BTN_P0_GPIO,TM_BTN_P1_GPIO,TM_BTN_P2_GPIO};
static constexpr int inputPins[]={TM_BTN_FIRE_GPIO,TM_BTN_REPEAT_GPIO,TM_BTN_SKIP_GPIO,
    TM_BTN_P0_GPIO,TM_BTN_P1_GPIO,TM_BTN_P2_GPIO,TM_KEY_FIRE_GPIO,TM_KEY_SCORE_GPIO};
#endif
// Official Wireless Stick V3 assignments: radio 8..14, OLED 17/18/21,
// battery ADC 1, LED 35, OLED supply 36. Reject flash/PSRAM as well.
constexpr bool safeRemotePin(int pin) {
    return pin>=0 && pin<=48 && pin!=0 && pin!=1 && pin!=3 &&
        !(pin>=8&&pin<=14) && pin!=17&&pin!=18&&pin!=19&&pin!=20&&pin!=21 &&
        !(pin>=22&&pin<=32) && pin!=35&&pin!=36&&pin!=43&&pin!=44&&pin!=45&&pin!=46&&pin!=48;
}
constexpr bool validRemotePins(size_t i=0,size_t j=0) {
    return i==sizeof(inputPins)/sizeof(inputPins[0]) ? true :
        !safeRemotePin(inputPins[i]) ? false :
        j<i ? (inputPins[i]!=inputPins[j] && validRemotePins(i,j+1)) :
        validRemotePins(i+1,0);
}
static_assert(validRemotePins(),"Remote GPIO conflict: unsafe, reserved or duplicate pin");
#if TM_REMOTE_SLEEP_MODE == 1
constexpr bool rtcRemotePins(size_t i=0) {
    return i==sizeof(inputPins)/sizeof(inputPins[0]) ||
        (inputPins[i]<=21 && rtcRemotePins(i+1));
}
static_assert(rtcRemotePins(),"Sleep mode 1 requires RTC GPIOs; use the seven-wire matrix if needed");
#endif
