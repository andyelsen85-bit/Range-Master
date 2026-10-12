#!/usr/bin/env python3
"""Actual handheld sketch with mocked board I/O, real AES-GCM, both power modes."""
from pathlib import Path
import shlex
import subprocess
import tempfile

REMOTE=Path(__file__).resolve().parents[1]
COMMON=REMOTE.parent/"lora-common"
ARDUINO=r'''
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <map>
#include <string>
#include <vector>
#define RTC_DATA_ATTR
constexpr int LOW=0,HIGH=1,INPUT_PULLUP=2,INPUT=3,OUTPUT=4;
constexpr int LED=35,Vext=36,SDA_OLED=17,SCL_OLED=18,RST_OLED=21;
static unsigned clockMs=0;static std::map<int,int> levels,modes;
static std::vector<std::string> operations;
inline uint32_t millis(){return clockMs;}
inline void delay(unsigned ms){clockMs+=ms;}
inline void delayMicroseconds(unsigned){}
inline int digitalRead(int p){return levels.count(p)?levels[p]:HIGH;}
inline void digitalWrite(int p,int v){levels[p]=v;}
inline void pinMode(int p,int mode){modes[p]=mode;}
inline bool btStop(){return true;}
struct SerialMock {
    void begin(int){}void println(const char*){}void flush(){}
    template<class... T>void printf(const char*,T...){}
};static SerialMock Serial;
using String=std::string;
'''
PREF=r'''
#pragma once
#include "Arduino.h"
static std::map<std::string,uint32_t> nv;
static bool failSave=false;
struct Preferences {
    bool begin(const char*,bool){return true;}
    uint32_t getULong(const char *key,uint32_t def){return nv.count(key)?nv[key]:def;}
    uint8_t getUChar(const char *key,uint8_t def){return nv.count(key)?nv[key]:def;}
    size_t putULong(const char *key,uint32_t value){
        operations.emplace_back("persist");if(failSave)return 0;nv[key]=value;return 4;
    }
    size_t putUChar(const char *key,uint8_t value){nv[key]=value;return 1;}
};
'''
RADIO=r'''
#pragma once
#include "Arduino.h"
constexpr int HELTEC_BOARD=1,SLOW_CLK_TPYE=1,MODEM_LORA=1;
struct McuMock {void begin(int,int){}};static McuMock Mcu;
struct RadioEvents_t {
    void (*TxDone)();void (*TxTimeout)();
    void (*RxDone)(uint8_t*,uint16_t,int16_t,int8_t);void (*RxTimeout)();
};
static std::vector<std::vector<uint8_t>> frames;
struct RadioMock {
    RadioEvents_t *events=nullptr;
    void Init(RadioEvents_t *e){events=e;}void SetChannel(int){}
    template<class... T>void SetTxConfig(T...){}
    template<class... T>void SetRxConfig(T...){}
    void Sleep(){}void Rx(int){}void IrqProcess(){}
    void Send(uint8_t *data,size_t len){
        operations.emplace_back("tx");frames.emplace_back(data,data+len);
        assert(nv["counter"]>0);events->TxDone();
    }
};static RadioMock Radio;
'''
OLED=r'''
#pragma once
#include "Arduino.h"
constexpr int GEOMETRY_64_32=1,ArialMT_Plain_10=1,TEXT_ALIGN_LEFT=1,WHITE=1,BLACK=0;
static int oledOff=0,contrast=255,inversions=0;
struct SSD1306Wire {
    SSD1306Wire(int,int,int,int,int,int){}void init(){}void clear(){inversions=0;}
    void setFont(int){}void setTextAlignment(int){}void setColor(int){}
    void fillRect(int,int,int,int){++inversions;}void setPixel(int,int){}
    void drawString(int,int,const std::string&){}void display(){}
    void setContrast(int n){contrast=n;}void displayOff(){++oledOff;}void displayOn(){--oledOff;}
};
'''
ULP=r'''
#pragma once
#include <cstdint>
using esp_err_t=int;constexpr int ESP_OK=0,R0=0,R1=1;
struct ulp_insn_t {uint32_t value;};
#define I_RD_REG(a,b,c) {0u}
#define I_MOVI(a,b) {(uint32_t)(b)}
#define I_SUBR(a,b,c) {0u}
#define I_BXZI(a) {(uint32_t)(a)}
#define I_WAKE() {0u}
#define I_HALT() {0u}
inline int ulp_process_macros_and_load(int,const ulp_insn_t*,size_t*){return -1;}
inline int ulp_set_wakeup_period(int,int){return 0;}
inline int ulp_run(int){return 0;}
'''
MAIN=r'''
void tick(unsigned ms){const unsigned end=clockMs+ms;while(clockMs<end) loop();}
void release(unsigned i){levels[buttons[i]]=HIGH;tick(35);}
void press(unsigned i){levels[buttons[i]]=LOW;tick(35);}
void key(unsigned group,bool on){levels[group?TM_KEY_SCORE_GPIO:TM_KEY_FIRE_GPIO]=on?LOW:HIGH;tick(55);}
void ack(){
    uint8_t bytes[tm_protocol::FRAME_LEN];
    assert(tm_protocol::encrypt(TM_REMOTE_ID,tm_protocol::CMD_BTN_ACK,activeCounter,bytes));
    onRemoteRx(bytes,sizeof(bytes),0,0);tick(1);
}
int main(){
    // Held at cold boot must be ignored even when its key is closed.
    levels[TM_KEY_FIRE_GPIO]=LOW;levels[buttons[0]]=LOW;setup();tick(100);
    assert(counter==0&&frames.empty());release(0);
    key(0,false);press(0);assert(counter==0&&frames.empty());
    key(0,true);tick(100);assert(counter==0);release(0);press(0);
    assert(counter==1&&frames.size()==1&&operations[0]=="persist"&&operations[1]=="tx");
    ack();assert(!pending);tick(250);release(0);
    // FIRE armed, SCORE locked -> nothing for P0; then inverse combination.
    press(3);assert(counter==1);release(3);key(0,false);key(1,true);
    press(1);assert(counter==1);release(1);
    press(3);assert(counter==2);ack();release(3);tick(250);
    assert(!armed[0]&&armed[1]);key(0,true);assert(armed[0]&&armed[1]);
    // Same frame/counter on every retry. Initial attempt plus five retries.
    frames.clear();press(2);const auto original=frames.at(0);uint32_t saved=counter;
    tick(2500);assert(!pending&&frames.size()==6&&counter==saved);
    for(const auto &frame:frames)assert(frame==original);
    release(2);tick(250);
    // Turning off the relevant key aborts retries and never queues a late press.
    frames.clear();press(1);assert(pending&&frames.size()==1);
    key(0,false);assert(!pending);tick(1000);assert(frames.size()==1);
    key(0,true);tick(100);assert(frames.size()==1);release(1);tick(250);
    // NVS failure prevents any TX.
    failSave=true;frames.clear();press(0);assert(frames.empty());failSave=false;release(0);
    key(0,false);key(1,false);assert(!armed[0]&&!armed[1]);
    tick(21000);assert(contrast==20&&oledOff==0);
    puts("PASS actual remote: held boot/rearm, independent keys, NVS-before-TX/failure, exact retries, abort, OLED dim");
}
'''

if __name__=="__main__":
    flags=shlex.split(subprocess.check_output(["pkg-config","--cflags","--libs","mbedcrypto"],text=True))
    for mode in (0,1):
        with tempfile.TemporaryDirectory(prefix="tm-remote-") as directory:
            p=Path(directory);(p/"esp32s3").mkdir();(p/"driver").mkdir();(p/"soc").mkdir()
            for name,body in {
                "Arduino.h":ARDUINO,"Preferences.h":PREF,"LoRaWan_APP.h":RADIO,
                "HT_SSD1306Wire.h":OLED,
                "WiFi.h":"#pragma once\nconstexpr int WIFI_OFF=0;struct WifiMock{void mode(int){}};static WifiMock WiFi;\n",
                "esp_random.h":"#pragma once\ninline uint32_t esp_random(){return 42;}\n",
                "esp_sleep.h":"#pragma once\nconstexpr int ESP_SLEEP_WAKEUP_ULP=1;inline int esp_sleep_get_wakeup_cause(){return 0;}inline int esp_sleep_enable_ulp_wakeup(){return 0;}inline void esp_deep_sleep_start(){}\n",
                "esp32s3/ulp.h":ULP,
                "ulp_common.h":"#pragma once\ninline void ulp_timer_stop(){}\n",
                "driver/rtc_io.h":"#pragma once\nusing gpio_num_t=int;constexpr int RTC_GPIO_MODE_OUTPUT_ONLY=1,RTC_GPIO_MODE_INPUT_ONLY=2;inline void rtc_gpio_init(int){}inline void rtc_gpio_deinit(int){}inline void rtc_gpio_set_direction(int,int){}inline void rtc_gpio_set_level(int,int){}inline void rtc_gpio_pullup_en(int){}inline void rtc_gpio_pulldown_dis(int){}inline uint32_t rtc_gpio_get_level(int p){return digitalRead(p);}\n",
                "soc/rtc_io_reg.h":"#pragma once\n#define RTC_GPIO_IN_REG 0\n#define RTC_GPIO_IN_NEXT_S 10\n",
            }.items():(p/name).write_text(body)
            defines={"TM_ALLOW_INSECURE_BENCH_KEY":"1","TM_REMOTE_ID":"'1'",
                "TM_REMOTE_SLEEP_MODE":str(mode),"TM_KEY_FIRE_GPIO":"16","TM_KEY_SCORE_GPIO":"40",
                "TM_BTN_FIRE_GPIO":"2","TM_BTN_REPEAT_GPIO":"4","TM_BTN_SKIP_GPIO":"5",
                "TM_BTN_P0_GPIO":"6","TM_BTN_P1_GPIO":"7","TM_BTN_P2_GPIO":"15"}
            if mode:
                # Only syntax-check the full direct sketch on RTC GPIOs here;
                # duplicate artificial pins are not permitted, so compile the
                # matrix variant with seven distinct RTC pins instead.
                defines.update({"TM_REMOTE_MATRIX":"1","TM_ROW0_GPIO":"2","TM_ROW1_GPIO":"4",
                    "TM_ROW2_GPIO":"5","TM_COL0_GPIO":"6","TM_COL1_GPIO":"7",
                    "TM_KEY_FIRE_GPIO":"15","TM_KEY_SCORE_GPIO":"16"})
            cpp=p/"test.cpp"
            cpp.write_text(f'#include "{REMOTE / "TrapMasterRemote.ino"}"\n'+
                (MAIN if mode==0 else 'int main(){setup();loop();assert(TM_REMOTE_SLEEP_MODE==1);puts("PASS mode 1 full sketch compiles with matrix/ULP wake path (hardware mocked)");}\n'))
            command=["g++","-std=c++17","-Wall","-Wextra","-Werror",
                "-Wno-missing-field-initializers","-I",str(p),"-I",str(REMOTE),"-I",str(COMMON)]
            command += [f"-D{k}={v}" for k,v in defines.items()]
            command += [str(cpp),*flags,"-o",str(p/"test")]
            subprocess.run(command,check=True);subprocess.run([str(p/"test")],check=True)
