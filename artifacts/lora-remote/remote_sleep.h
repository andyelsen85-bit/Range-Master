#pragma once
#if TM_REMOTE_SLEEP_MODE == 1
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "esp32s3/ulp.h"
#include "ulp_common.h"
#include "soc/rtc_io_reg.h"
// S3 EXT1 cannot monitor mixed-polarity key changes plus active-low buttons.
// The ULP compares each RTC input to its current level every 10 ms. A change
// (either edge) wakes the CPU. There is no timer wake or periodic CPU polling.
static bool remoteSleep() {
    ulp_timer_stop();
    int wakePins[8];size_t count=0;
#if TM_REMOTE_MATRIX
    for(int row:rows) {
        rtc_gpio_init((gpio_num_t)row);
        rtc_gpio_set_direction((gpio_num_t)row,RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_level((gpio_num_t)row,0);
    }
    for(int col:cols) wakePins[count++]=col;
#else
    for(int button:buttons) wakePins[count++]=button;
#endif
    wakePins[count++]=TM_KEY_FIRE_GPIO;wakePins[count++]=TM_KEY_SCORE_GPIO;
    // Seven instructions per input plus halt fit in 256 bytes for <=8 inputs.
    ulp_insn_t program[57];size_t used=0;
    for(size_t i=0;i<count;++i) {
        gpio_num_t pin=(gpio_num_t)wakePins[i];
        rtc_gpio_init(pin);rtc_gpio_set_direction(pin,RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pullup_en(pin);rtc_gpio_pulldown_dis(pin);
        const uint32_t level=rtc_gpio_get_level(pin);
        const ulp_insn_t part[]={
            I_RD_REG(RTC_GPIO_IN_REG,RTC_GPIO_IN_NEXT_S+wakePins[i],RTC_GPIO_IN_NEXT_S+wakePins[i]),
            I_MOVI(R1,level), I_SUBR(R0,R0,R1),
            I_BXZI(used+6), I_WAKE(), I_HALT()
        };
        memcpy(program+used,part,sizeof(part));used+=6;
    }
    program[used++]=(ulp_insn_t)I_HALT();
    // Check the reserved slow memory before loading; fail awake, never claim
    // sleeping is safe when the selected core has no ULP support/reservation.
    if(ulp_process_macros_and_load(0,program,&used)!=ESP_OK ||
       ulp_set_wakeup_period(0,10000)!=ESP_OK ||
       esp_sleep_enable_ulp_wakeup()!=ESP_OK || ulp_run(0)!=ESP_OK) return false;
    Serial.flush();esp_deep_sleep_start();return true;
}
#endif
