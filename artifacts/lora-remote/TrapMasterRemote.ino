// Six-button remote, Heltec Wireless Stick V3 / SX1262, 433 MHz.
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <esp_random.h>
#include "LoRaWan_APP.h"
#include "HT_SSD1306Wire.h"
#if __has_include("TrapMasterRemote.local.h")
#include "TrapMasterRemote.local.h"
#endif
#ifndef TM_REMOTE_ID
#error "Define TM_REMOTE_ID to a unique ASCII character '1' through '8'"
#endif
static_assert(TM_REMOTE_ID>='1'&&TM_REMOTE_ID<='8',"Invalid remote ID");
#if __has_include("../lora-common/trapmaster_protocol.h")
#include "../lora-common/trapmaster_protocol.h"
#include "../lora-common/button_events.h"
#else
#include "../../lora-common/trapmaster_protocol.h"
#include "../../lora-common/button_events.h"
#endif
#if __has_include("remote_pins.h")
#include "remote_pins.h"
#include "remote_sleep.h"
#else
#include "../remote_pins.h"
#include "../remote_sleep.h"
#endif

SSD1306Wire remoteDisplay(0x3c,500000,SDA_OLED,SCL_OLED,GEOMETRY_64_32,RST_OLED);
Preferences remoteNvs;
RadioEvents_t remoteRadio;
tm_buttons::Input buttonInputs[6],keyInputs[2];
static bool armed[2]{},pending=false,waiting=false,transmitting=false;
static bool dimmed=false;
static uint32_t counter=0,activeCounter=0,deadline=0,nextAttempt=0,lastActivity=0;
static uint32_t feedbackUntil=0,ledAt=0;
static uint8_t activeButton=0,retries=0,ledEdges=0;
static const char *feedback="";
static volatile bool txDone=false,txFailed=false,acked=false;
static uint8_t activeFrame[tm_protocol::FRAME_LEN];
static const char *buttonNames[]={"","SCHUSS","WIEDERH.","SKIP","0 PKT","1 PKT","2 PKT"};
#if TM_REMOTE_SLEEP_MODE == 1
RTC_DATA_ATTR static bool sleepArmed[2];
#endif

// Compact 5x7 glyphs keep both group names/states readable on a 64x32 OLED.
static void statusText(int x,int y,const char *s) {
    static const char letters[]="SCHUPNKTREFIG";
    static const uint8_t glyphs[][5]={
        {0x46,0x49,0x49,0x49,0x31},{0x3e,0x41,0x41,0x41,0x22},
        {0x7f,8,8,8,0x7f},{0x3f,0x40,0x40,0x40,0x3f},
        {0x7f,9,9,9,6},{0x7f,2,4,8,0x7f},{0x7f,8,0x14,0x22,0x41},
        {1,1,0x7f,1,1},{0x7f,9,0x19,0x29,0x46},
        {0x7f,0x49,0x49,0x49,0x41},{0x7f,9,9,9,1},
        {0,0x41,0x7f,0x41,0},{0x3e,0x41,0x49,0x49,0x7a}
    };
    while(*s) {
        const char *p=strchr(letters,*s++);
        if(p) for(int col=0;col<5;++col) for(int row=0;row<7;++row)
            if(glyphs[p-letters][col]&(1<<row)) remoteDisplay.setPixel(x+col,y+row);
        x+=6;
    }
}

static void renderRemote() {
    remoteDisplay.clear();remoteDisplay.setFont(ArialMT_Plain_10);
    remoteDisplay.setTextAlignment(TEXT_ALIGN_LEFT);
    const char *groups[]={"SCHUSS","PUNKTE"};
    for(unsigned i=0;i<2;++i) {
        int y=i*16;
        if(armed[i]) { remoteDisplay.setColor(WHITE);remoteDisplay.fillRect(0,y,64,16);remoteDisplay.setColor(BLACK); }
        else remoteDisplay.setColor(WHITE);
        // Two lines per group fit the narrow 64px display.
        statusText(1,y,groups[i]);
        statusText(1,y+8,armed[i]?"FREI":"GESPERRT");
    }
    remoteDisplay.setColor(WHITE);
    if((int32_t)(feedbackUntil-millis())>0) {
        remoteDisplay.setColor(BLACK);remoteDisplay.fillRect(0,8,64,16);
        remoteDisplay.setColor(WHITE);remoteDisplay.drawString(0,10,feedback);
    }
    remoteDisplay.display();
}
static void feedbackRemote(bool ok) {
    static char text[32];
    if(ok) snprintf(text,sizeof(text),"OK %s",buttonNames[activeButton]);
    else snprintf(text,sizeof(text),"keine Antwort");
    feedback=text;feedbackUntil=millis()+1000;ledEdges=ok?2:6;ledAt=millis();
    Serial.printf("Button %u %s counter=%lu %s\n",activeButton,buttonNames[activeButton],
        (unsigned long)activeCounter,ok?"ACK":"keine Antwort");
    pending=waiting=transmitting=false;Radio.Sleep();renderRemote();
}
static void onRemoteTxDone() { txDone=true; }
static void onRemoteTxTimeout() { txFailed=true;Radio.Sleep(); }
static void onRemoteRxTimeout() { Radio.Sleep(); }
static void onRemoteRx(uint8_t *data,uint16_t len,int16_t,int8_t) {
    tm_protocol::DecodedPacket p{};
    if(tm_protocol::decrypt(data,len,&p)&&p.machine==TM_REMOTE_ID&&
       p.command==tm_protocol::CMD_BTN_ACK&&p.counter==activeCounter) acked=true;
    Radio.Sleep();
    if(waiting&&!acked) Radio.Rx(0);
}
static void initRemoteRadio() {
    Mcu.begin(HELTEC_BOARD,SLOW_CLK_TPYE);
    remoteRadio.TxDone=onRemoteTxDone;remoteRadio.TxTimeout=onRemoteTxTimeout;
    remoteRadio.RxDone=onRemoteRx;remoteRadio.RxTimeout=onRemoteRxTimeout;
    Radio.Init(&remoteRadio);Radio.SetChannel(433000000);
    Radio.SetTxConfig(MODEM_LORA,14,0,0,7,1,8,false,true,0,0,false,3000);
    Radio.SetRxConfig(MODEM_LORA,0,7,1,0,8,0,false,0,true,0,0,false,true);
}
static void readButtons(bool down[6]) {
#if TM_REMOTE_MATRIX
    // Inactive rows are high impedance, never driven HIGH against another row.
    // Multiple closures are suppressed; diodes are recommended against ghosts.
    unsigned pressed=0;
    static bool multipleHeld=false;
    for(unsigned row=0;row<3;++row) {
        digitalWrite(rows[row],LOW);pinMode(rows[row],OUTPUT);delayMicroseconds(10);
        for(unsigned col=0;col<2;++col) {
            down[row*2+col]=digitalRead(cols[col])==LOW;
            pressed+=down[row*2+col];
        }
        pinMode(rows[row],INPUT);
    }
    if(pressed>1) multipleHeld=true;
    if(!pressed) multipleHeld=false;
    if(multipleHeld) for(unsigned i=0;i<6;++i) down[i]=false;
#else
    for(unsigned i=0;i<6;++i) down[i]=digitalRead(buttons[i])==LOW;
#endif
}
static void startButton(uint8_t btn) {
    if(pending||counter==UINT32_MAX) return;
    uint32_t next=counter+1;
    if(remoteNvs.putULong("counter",next)!=sizeof(uint32_t)) {
        feedback="NVS Fehler";feedbackUntil=millis()+1000;renderRemote();return;
    }
    counter=activeCounter=next;activeButton=btn;retries=0;
    if(!tm_protocol::encrypt(TM_REMOTE_ID,tm_protocol::CMD_BTN_BASE+btn,
        activeCounter,activeFrame)) return;
    pending=true;waiting=transmitting=false;nextAttempt=millis();acked=false;
}
static void retryTick() {
    if(!pending) return;
    const uint32_t now=millis();
    if(!armed[activeButton<=3?0:1]) {
        pending=waiting=transmitting=false;Radio.Sleep();
        Serial.println("Key locked: retries cancelled");return;
    }
    if(acked) { feedbackRemote(true);return; }
    if(transmitting) {
        if(txDone&&!txFailed) {
            transmitting=false;waiting=true;deadline=now+150;Radio.Rx(0);
        } else if(txFailed||(int32_t)(now-deadline)>=0) {
            transmitting=false;waiting=true;deadline=now;
        }
    }
    if(waiting&&(int32_t)(now-deadline)>=0) {
        waiting=false;Radio.Sleep();
        if(retries>=5) { feedbackRemote(false);return; }
        ++retries;nextAttempt=now+30+esp_random()%121;
    }
    if(!waiting&&!transmitting&&(int32_t)(now-nextAttempt)>=0) {
        // Every retransmission uses the exact saved frame/counter, never a new
        // nonce or a fresh logical button press.
        txDone=txFailed=false;transmitting=true;deadline=now+500;
        Radio.Send(activeFrame,sizeof(activeFrame));
    }
}
void setup() {
    Serial.begin(115200);WiFi.mode(WIFI_OFF);btStop();
#if TM_REMOTE_SLEEP_MODE == 1
    ulp_timer_stop();
    for(int pin:inputPins) rtc_gpio_deinit((gpio_num_t)pin);
#endif
    pinMode(LED,OUTPUT);digitalWrite(LED,LOW);
    pinMode(Vext,OUTPUT);digitalWrite(Vext,LOW);delay(100);remoteDisplay.init();
    for(int pin:inputPins) pinMode(pin,INPUT_PULLUP);
#if TM_REMOTE_MATRIX
    for(int row:rows) pinMode(row,INPUT);
#endif
    uint32_t now=millis();lastActivity=now;
    keyInputs[0].init(digitalRead(TM_KEY_FIRE_GPIO)==LOW,now);
    keyInputs[1].init(digitalRead(TM_KEY_SCORE_GPIO)==LOW,now);
    armed[0]=keyInputs[0].stable;armed[1]=keyInputs[1].stable;
    bool down[6];readButtons(down);
    for(unsigned i=0;i<6;++i) {
        bool coldHeld=down[i];
#if TM_REMOTE_SLEEP_MODE == 1
        if(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_ULP &&
           sleepArmed[i/3] && armed[i/3]) coldHeld=false;
#endif
        buttonInputs[i].init(coldHeld,now);
    }
    renderRemote();
    if(!remoteNvs.begin("tm_remote",false)) {
        feedback="NVS Fehler";feedbackUntil=UINT32_MAX;renderRemote();
        while(true) delay(1000);
    }
    // Binding the durable identity prevents accidental nonce reuse by changing
    // a build-time sender ID on a previously provisioned handheld.
    uint8_t savedId=remoteNvs.getUChar("sender",0);
    if((savedId && savedId!=TM_REMOTE_ID)||
       (!savedId && remoteNvs.putUChar("sender",TM_REMOTE_ID)!=1)) {
        Serial.println("Sender identity changed: provision safely with a NEW AES key");
        while(true) delay(1000);
    }
    counter=remoteNvs.getULong("counter",0);initRemoteRadio();
}
void loop() {
    uint32_t now=millis();
    bool keyRaw[]={digitalRead(TM_KEY_FIRE_GPIO)==LOW,digitalRead(TM_KEY_SCORE_GPIO)==LOW};
    bool redraw=false;
    for(unsigned group=0;group<2;++group) {
        // Key levels are debounced for 50 ms; key Input's press event is unused.
        keyInputs[group].sample(keyRaw[group],now,50,true);
        bool next=keyInputs[group].stable;
        if(next!=armed[group]) {
            armed[group]=next;lastActivity=now;redraw=true;
            // Arming never authorizes a button that was already held.
            for(unsigned i=group*3;i<group*3+3;++i) buttonInputs[i].released=false;
        }
    }
    bool down[6];readButtons(down);
    for(unsigned i=0;i<6;++i) {
        if(down[i]!=buttonInputs[i].raw) lastActivity=now;
        if(buttonInputs[i].sample(down[i],now,30,armed[i/3])) startButton(i+1);
    }
    retryTick();Radio.IrqProcess();
    if(ledEdges && (int32_t)(now-ledAt)>=0) {
        digitalWrite(LED,(ledEdges%2)==0?HIGH:LOW);--ledEdges;ledAt=now+100;
    }
    if(feedbackUntil && (int32_t)(now-feedbackUntil)>=0) { feedbackUntil=0;redraw=true; }
    bool wantDim=now-lastActivity>=20000;
    if(wantDim!=dimmed) { dimmed=wantDim;remoteDisplay.setContrast(dimmed?20:255);redraw=true; }
    if(redraw) renderRemote();
#if TM_REMOTE_SLEEP_MODE == 1
    bool held=false;for(bool value:down) held|=value;
    if(wantDim&&!pending&&!held&&!ledEdges&&!feedbackUntil) {
        sleepArmed[0]=armed[0];sleepArmed[1]=armed[1];
        Radio.Sleep();remoteDisplay.displayOff();
        if(!remoteSleep()) {
            Serial.println("ULP unavailable: remain awake");
            remoteDisplay.displayOn();
            for(int pin:inputPins) { rtc_gpio_deinit((gpio_num_t)pin);pinMode(pin,INPUT_PULLUP); }
#if TM_REMOTE_MATRIX
            for(int row:rows) pinMode(row,INPUT);
#endif
            lastActivity=now;
        }
    }
#endif
    delay(1);
}
