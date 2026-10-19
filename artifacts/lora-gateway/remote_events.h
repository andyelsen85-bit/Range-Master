#pragma once
// Included after the gateway radio/server globals. All work runs in loop();
// RxDone only admits a small decoded packet to this bounded mailbox.
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#if __has_include("../lora-common/fire_link.h")
#include "../lora-common/fire_link.h"
#else
#include "../../lora-common/fire_link.h"
#endif
#include <sys/socket.h>
#if __has_include("../lora-common/button_events.h")
#include "../lora-common/button_events.h"
#else
#include "../../lora-common/button_events.h"
#endif

class FastEventSocket : public WebSocketsServer {
public:
    FastEventSocket():WebSocketsServer(81) {}
    void drop(uint8_t n) { clientDisconnect(&_clients[n]); }
    bool frame(uint8_t n,const char *text,uint8_t opcode=1) {
        auto *tcp=_clients[n].tcp;
        if(!tcp || !tcp->connected()) return false;
        const size_t len=strlen(text);
        if(len>500) return false;
        uint8_t bytes[504]; size_t h=2;
        bytes[0]=0x80|opcode;
        if(len<126) bytes[1]=len;
        else { bytes[1]=126;bytes[2]=len>>8;bytes[3]=len;h=4; }
        memcpy(bytes+h,text,len);
        // Never wait on a slow terminal. Partial writes also close the stream,
        // so no subsequent frame can be misinterpreted as a continuation.
        if(::send(tcp->fd(),bytes,h+len,MSG_DONTWAIT)!=(int)(h+len)) {
            drop(n); return false;
        }
        return true;
    }
};
static FastEventSocket eventSocket;
static tm_buttons::Ring buttonRing;
struct ButtonClient {
    bool present=false,auth=false;
    char server[17]{},client[33]{};
    uint32_t opened=0,sent=0,ack=0,ping=0;
    uint8_t missed=0;
};
static ButtonClient buttonClients[4];
static void handleSocketFire(uint8_t n,JsonVariantConst json);
static bool socketFireReply(uint8_t n,const char *nonce,int status,const char *body) {
    if(n>=4||!buttonClients[n].auth)return false;
    const auto &c=buttonClients[n];
    char signature[65];
    if(!tm_fire_link::reply_mac(GATEWAY_AUTH_KEY,c.server,c.client,nonce,status,body,signature))return false;
    JsonDocument doc;doc["fire"]=nonce;doc["status"]=status;doc["body"]=body;doc["sig"]=signature;
    char output[500];size_t count=serializeJson(doc,output,sizeof(output));
    return count<sizeof(output)&&eventSocket.frame(n,output);
}
static uint32_t buttonBoot=0,remoteCounters[8]{},remoteLastMs=0,remoteLastId=0;
static uint8_t remoteLastSrc=0,remoteLastBtn=0;
static bool remoteDelivered=false;
static tm_protocol::DecodedPacket remoteMailbox[8];
static uint8_t remoteHead=0,remoteCount=0;
static bool remoteTx=false;
static uint32_t remoteTxStarted=0;
static void buttonRandomHex(char *out,size_t bytes) {
    for(size_t i=0;i<bytes;++i) {
        uint8_t b=(uint8_t)esp_random();
        out[2*i]=tm_auth::hex_digit(b>>4);out[2*i+1]=tm_auth::hex_digit(b&15);
    }
    out[bytes*2]=0;
}
static bool buttonNumber(JsonVariantConst v,uint32_t &out) {
    if(!v.is<uint32_t>()) return false;
    out=v.as<uint32_t>();return true;
}
static bool buttonEventText(const tm_buttons::Event &e,uint32_t age,
                            const char *sn,const char *cn,char *out,size_t cap) {
    char sig[17];
    if(!tm_buttons::event_mac(GATEWAY_AUTH_KEY,sn,cn,buttonBoot,e,age,sig)) return false;
    int n=snprintf(out,cap,"{\"id\":%lu,\"src\":%u,\"btn\":%u,\"age_ms\":%lu,\"sig\":\"%s\"}",
        (unsigned long)e.id,e.src,e.btn,(unsigned long)age,sig);
    return n>0 && (size_t)n<cap;
}
static void buttonSocketEvent(uint8_t n,WStype_t type,uint8_t *payload,size_t len) {
    if(n>=4) { if(type==WStype_CONNECTED) eventSocket.drop(n);return; }
    auto &c=buttonClients[n];
    if(type==WStype_DISCONNECTED) { c=ButtonClient{};return; }
    if(type==WStype_CONNECTED) {
        unsigned unauth=0;
        for(const auto &x:buttonClients) if(x.present&&!x.auth) ++unauth;
        if(unauth>=2) { eventSocket.drop(n);return; }
        c=ButtonClient{}; c.present=true;c.opened=c.ping=millis();
        buttonRandomHex(c.server,8);
        char hello[100];
        snprintf(hello,sizeof(hello),"{\"hello\":1,\"boot\":%lu,\"nonce\":\"%s\"}",
            (unsigned long)buttonBoot,c.server);
        eventSocket.frame(n,hello);return;
    }
    if(type==WStype_PONG) { c.missed=0;return; }
    if(type!=WStype_TEXT || !c.present || len>384) {
        if(type==WStype_BIN || type==WStype_FRAGMENT_TEXT_START ||
           type==WStype_FRAGMENT_BIN_START) eventSocket.drop(n);
        return;
    }
    JsonDocument json;
    if(deserializeJson(json,payload,len)) { eventSocket.drop(n);return; }
    if(!c.auth) {
        uint32_t boot=0,after=0;
        const char *client=json["client_nonce"], *signature=json["auth"];
        char expected[65];
        if(!buttonNumber(json["boot"],boot)||!buttonNumber(json["after"],after)||
            !tm_buttons::hex(client,32)||
            !tm_buttons::auth_mac(GATEWAY_AUTH_KEY,c.server,client,after,boot,expected)||
            !tm_buttons::equal(expected,signature,64)) { eventSocket.drop(n);return; }
        memcpy(c.client,client,33);c.auth=true;c.ack=c.sent=after;
        if(boot!=buttonBoot || after>buttonRing.next) {
            c.ack=c.sent=buttonRing.next;
            char resync[100];
            snprintf(resync,sizeof(resync),"{\"resync\":1,\"boot\":%lu,\"next\":%lu}",
                (unsigned long)buttonBoot,(unsigned long)buttonRing.next);
            eventSocket.frame(n,resync);
        } else if(buttonRing.gap(after)) {
            // Recover surviving events; signal the missing range, never invent it.
            c.sent=c.ack=buttonRing.oldest()-1;
            eventSocket.frame(n,"{\"gap\":true}");
        }
        return;
    }
    uint32_t ack=0;
    if(json["fire"].is<const char*>()) {handleSocketFire(n,json.as<JsonVariantConst>());return;}
    if(buttonNumber(json["ack"],ack) && ack<=c.sent && ack>c.ack) {
        c.ack=ack;
        if(ack>=remoteLastId && remoteLastId) remoteDelivered=true;
    }
}
static void buttonSocketTick() {
    eventSocket.loop();
    const uint32_t now=millis();
    for(uint8_t n=0;n<4;++n) {
        auto &c=buttonClients[n];
        if(!c.present) continue;
        if(!c.auth) { if(now-c.opened>=3000) eventSocket.drop(n);continue; }
        if(now-c.ping>=10000) {
            if(c.missed>=2) { eventSocket.drop(n);continue; }
            c.ping=now;++c.missed;
            if(!eventSocket.frame(n,"",9)) continue;
        }
        if(buttonRing.gap(c.sent)) {
            c.sent=buttonRing.oldest()-1;
            if(!eventSocket.frame(n,"{\"gap\":true}")) continue;
        }
        const auto *e=buttonRing.get(c.sent+1);
        if(e) {
            char text[192];
            if(buttonEventText(*e,now-e->t_ms,c.server,c.client,text,sizeof(text)) &&
                eventSocket.frame(n,text)) c.sent=e->id;
        }
    }
}
static bool buttonEnqueue(const tm_protocol::DecodedPacket &p) {
    if(!tm_protocol::valid_remote(p.machine)||!tm_protocol::button_command(p.command)||remoteCount>=8) return false;
    remoteMailbox[(remoteHead+remoteCount)%8]=p;++remoteCount;return true;
}
static void buttonRadioTick() {
    // FIRE owns only the actual TX. While awaiting ACK or pair delay, receive,
    // persist and ACK remote presses normally (same durable counter rules).
    if(radioBusy) return;
    if(remoteTx) {
        if(txDone||txTimeout||millis()-remoteTxStarted>TX_TIMEOUT_MS) {
            remoteTx=false;Radio.Rx(0);
        }
        return;
    }
    if(!remoteCount) return;
    auto p=remoteMailbox[remoteHead];
    remoteHead=(remoteHead+1)%8;--remoteCount;
    auto &last=remoteCounters[p.machine-'1'];
    if(p.counter<last) { Radio.Rx(0);return; }
    if(p.counter>last) {
        if(buttonRing.next==UINT32_MAX) { Radio.Rx(0);return; }
        char key[8];snprintf(key,sizeof(key),"rem_%c",p.machine);
        const uint32_t started=millis();
        if(preferences.putULong(key,p.counter)!=sizeof(uint32_t)) {
            Serial.println("Remote counter NVS failed: NO ACK/event");
            Radio.Rx(0);return;
        }
        last=p.counter; // durable before ACK; duplicates do not write NVS again
        buttonRing.append(p.machine-'0',p.command-tm_protocol::CMD_BTN_BASE,millis());
        remoteLastId=buttonRing.next;remoteLastMs=millis();
        remoteLastSrc=p.machine-'0';remoteLastBtn=p.command-tm_protocol::CMD_BTN_BASE;
        remoteDelivered=false;
        digitalWrite(LED,HIGH);
        Serial.printf("Remote %c button %u id=%lu persistMs=%lu\n",p.machine,
            remoteLastBtn,(unsigned long)remoteLastId,(unsigned long)(millis()-started));
    }
    uint8_t frame[tm_protocol::FRAME_LEN];
    if(tm_protocol::encrypt(p.machine,tm_protocol::CMD_BTN_ACK,p.counter,frame)) {
        txDone=txTimeout=false;remoteTx=true;remoteTxStarted=millis();
        Radio.Send(frame,sizeof(frame));
    } else Radio.Rx(0);
}
static bool buttonQueryNumber(const char *name,uint32_t &out) {
    String text=server.arg(name);
    if(text.length()<1||text.length()>10) return false;
    uint64_t n=0;
    for(size_t i=0;i<text.length();++i) {
        if(text[i]<'0'||text[i]>'9') return false;
        n=n*10+text[i]-'0';if(n>UINT32_MAX) return false;
    }
    out=(uint32_t)n;return true;
}
static void handleButtonEvents() {
    if(!health_request_is_authentic()) {
        server.send(401,"application/json","{\"error\":\"unauthorized\"}");return;
    }
    uint32_t after=0,boot=0;
    String cn=server.arg("client_nonce");char sn[17];
    if(!buttonQueryNumber("after",after)||!buttonQueryNumber("boot",boot)||
       !tm_buttons::hex(cn.c_str(),32)) {
        server.send(400,"application/json","{\"error\":\"invalid cursor/nonce\"}");return;
    }
    buttonRandomHex(sn,8);
    String body="{\"boot\":"+String(buttonBoot)+",\"next\":"+String(buttonRing.next)+
        ",\"nonce\":\""+sn+"\",\"client_nonce\":\""+cn+"\"";
    if(boot==buttonBoot && after<=buttonRing.next) {
        body+=",\"gap\":"+String(buttonRing.gap(after)?"true":"false")+",\"events\":[";
        uint32_t id=after+1;
        if(id<buttonRing.oldest()) id=buttonRing.oldest();
        unsigned count=0;
        while(count<8) {
            const auto *e=buttonRing.get(id);if(!e) break;
            char text[192];
            if(!buttonEventText(*e,millis()-e->t_ms,sn,cn.c_str(),text,sizeof(text))) break;
            if(count++) body+=",";
            body+=text;++id;
        }
        body+="]";
    }
    body+="}";
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",body);
}
static bool buttonRenderStatus() {
    if(!remoteLastId||millis()-remoteLastMs>1500) return false;
    bool connected=false;for(const auto &c:buttonClients) connected|=c.auth;
    gatewayDisplay.clear();gatewayDisplay.setFont(ArialMT_Plain_10);
    gatewayDisplay.drawString(0,0,String("REMOTE ")+String(remoteLastSrc)+" B"+String(remoteLastBtn));
    gatewayDisplay.drawString(0,16,remoteDelivered?"OK":
        !connected?"getrennt":millis()-remoteLastMs>1000?"keine Antwort":"gesendet");
    if(remoteDelivered) {
        gatewayDisplay.drawLine(48,23,52,27);
        gatewayDisplay.drawLine(52,27,60,17);
    }
    gatewayDisplay.display();return true;
}
static void buttonEventsInit() {
    do { buttonBoot=esp_random(); } while(!buttonBoot);
    for(uint8_t i=0;i<8;++i) {
        char key[8];snprintf(key,sizeof(key),"rem_%c",'1'+i);
        remoteCounters[i]=preferences.getULong(key,0);
    }
    pinMode(LED,OUTPUT);digitalWrite(LED,LOW);
    eventSocket.begin();eventSocket.onEvent(buttonSocketEvent);
}
