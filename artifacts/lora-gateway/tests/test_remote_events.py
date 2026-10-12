#!/usr/bin/env python3
"""Actual gateway remote admission, replay and socket state machine (mock I/O)."""
from pathlib import Path
import importlib.util
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("fixture",ROOT/"tests/test_nonce.py")
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
STUBS=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#define TM_ALLOW_INSECURE_BENCH_KEY
#include "trapmaster_protocol.h"
#include "button_events.h"
constexpr char GATEWAY_AUTH_KEY[]="synthetic-private-auth-key";
struct String:std::string {
    using std::string::string;String(const std::string &s):std::string(s){}
    String(uint32_t n):std::string(std::to_string(n)){}
};
static uint32_t clockMs=1000,randomValue=1;
uint32_t millis(){return clockMs;}
uint32_t esp_random(){return randomValue++;}
constexpr int LED=35,HIGH=1;
void digitalWrite(int,int){}
struct SerialMock {void println(const char*){}template<class...T>void printf(const char*,T...){}} Serial;
enum WStype_t {WStype_DISCONNECTED,WStype_CONNECTED,WStype_PONG,WStype_TEXT,
    WStype_BIN,WStype_FRAGMENT_TEXT_START,WStype_FRAGMENT_BIN_START};
struct Value {
    bool numeric=false;uint32_t n=0;std::string s;
    template<class T>bool is()const{return numeric;}
    template<class T>T as()const{return (T)n;}
    operator const char*()const{return numeric?nullptr:s.c_str();}
};
using JsonVariantConst=Value;
struct JsonDocument {
    std::map<std::string,Value> values;
    const Value &operator[](const char *s)const {
        static const Value empty{};
        auto i=values.find(s);return i==values.end()?empty:i->second;
    }
};
static JsonDocument incoming;
bool deserializeJson(JsonDocument &j,uint8_t*,size_t){j=incoming;return false;}
void buttonDisconnect(uint8_t);
struct FastSocketMock {
    bool writable=true;
    std::vector<uint8_t> dropped;
    std::vector<std::string> messages;
    std::vector<uint8_t> opcodes;
    void drop(uint8_t n){dropped.push_back(n);buttonDisconnect(n);}
    void loop(){}
    bool frame(uint8_t n,const char *s,uint8_t opcode=1){
        if(!writable){drop(n);return false;}
        messages.emplace_back(s);opcodes.push_back(opcode);return true;
    }
} eventSocket;
static bool radioBusy=false;static void *pendingFire=nullptr;
static volatile bool txDone=false,txTimeout=false;
constexpr unsigned TX_TIMEOUT_MS=500;
static std::vector<std::string> order;
struct NvsMock {
    bool fails=false;unsigned writes=0;
    size_t putULong(const char*,uint32_t){
        ++writes;order.emplace_back("persist");return fails?0:4;
    }
} preferences;
struct RadioMock {
    unsigned sends=0,receives=0;
    void Rx(int){++receives;}
    void Send(uint8_t *bytes,size_t len){
        ++sends;order.emplace_back("ack");
        tm_protocol::DecodedPacket p{};assert(tm_protocol::decrypt(bytes,len,&p));
        assert(p.command==tm_protocol::CMD_BTN_ACK&&tm_protocol::valid_remote(p.machine));
    }
} Radio;
constexpr int ArialMT_Plain_10=1;
struct OledMock {
    std::vector<std::string> text;
    void clear(){text.clear();}void setFont(int){}
    void drawString(int,int,const std::string&s){text.push_back(s);}
    void drawLine(int,int,int,int){}
    void display(){}
} gatewayDisplay;
static bool healthOk=true;
bool health_request_is_authentic(){return healthOk;}
struct HttpMock {
    std::map<std::string,String> args;
    int status=0;std::string body;
    String arg(const char *name){return args[name];}
    void sendHeader(const char*,const char*){}
    void send(int n,const char*,const std::string &s){status=n;body=s;}
} server;
'''
MAIN=r'''
void buttonDisconnect(uint8_t n){if(n<4)buttonClients[n]=ButtonClient{};}
void remote(uint8_t sender,uint32_t counter,uint8_t btn=1){
    tm_protocol::DecodedPacket p{1,sender,(uint8_t)(tm_protocol::CMD_BTN_BASE+btn),counter};
    assert(buttonEnqueue(p));buttonRadioTick();
}
void finish(){txDone=true;buttonRadioTick();txDone=false;}
void authenticate(uint8_t n,uint32_t boot,uint32_t after,bool good=true){
    incoming={};incoming.values["boot"]={true,boot,{}};incoming.values["after"]={true,after,{}};
    const char *cn="00112233445566778899aabbccddeeff";char mac[65];
    assert(tm_buttons::auth_mac(GATEWAY_AUTH_KEY,buttonClients[n].server,cn,after,boot,mac));
    incoming.values["client_nonce"]={false,0,cn};
    incoming.values["auth"]={false,0,good?mac:"0000000000000000000000000000000000000000000000000000000000000000"};
    buttonSocketEvent(n,WStype_TEXT,nullptr,1);
}
int main(){
    buttonBoot=123;
    remote('1',10);assert(preferences.writes==1&&Radio.sends==1&&buttonRing.next==1);
    assert(order[0]=="persist"&&order[1]=="ack");
    assert(buttonRing.get(1)->src==1);finish();
    remote('1',10);assert(preferences.writes==1&&Radio.sends==2&&buttonRing.next==1);finish();
    remote('1',9);assert(Radio.sends==2&&buttonRing.next==1);
    preferences.fails=true;remote('1',11);
    assert(Radio.sends==2&&remoteCounters[0]==10&&buttonRing.next==1);
    preferences.fails=false;remote('2',10,6);
    assert(Radio.sends==3&&buttonRing.next==2&&remoteCounters[1]==10);finish();
    // Queued receive may not interrupt a FIRE/relay transaction.
    pendingFire=(void*)1;remote('3',10);assert(Radio.sends==3&&remoteCount==1);
    pendingFire=nullptr;radioBusy=true;buttonRadioTick();assert(Radio.sends==3);
    radioBusy=false;buttonRadioTick();assert(Radio.sends==4&&buttonRing.next==3);finish();

    buttonSocketEvent(0,WStype_CONNECTED,nullptr,0);
    buttonSocketEvent(1,WStype_CONNECTED,nullptr,0);
    buttonSocketEvent(2,WStype_CONNECTED,nullptr,0);assert(!buttonClients[2].present);
    authenticate(0,0,0,false);assert(!buttonClients[0].present);
    clockMs+=3000;buttonSocketTick();assert(!buttonClients[1].present);
    // Fresh boot => resync, never replay old RAM events.
    buttonSocketEvent(0,WStype_CONNECTED,nullptr,0);
    authenticate(0,0,0);assert(buttonClients[0].auth&&buttonClients[0].sent==3);
    assert(eventSocket.messages.back().find("resync")!=std::string::npos);
    auto sent=eventSocket.messages.size();buttonSocketTick();assert(eventSocket.messages.size()==sent);
    // Resume within the same boot delivers actual ages, bound to this session.
    buttonSocketEvent(1,WStype_CONNECTED,nullptr,0);authenticate(1,123,1);
    buttonSocketTick();assert(buttonClients[1].sent==2);
    assert(eventSocket.messages.back().find("\"src\":2")!=std::string::npos);
    buttonSocketTick();assert(buttonClients[1].sent==3);
    incoming={};incoming.values["ack"]={true,3,{}};buttonSocketEvent(1,WStype_TEXT,nullptr,1);
    assert(buttonClients[1].ack==3&&remoteDelivered);
    remoteLastMs=clockMs;
    assert(buttonRenderStatus()&&gatewayDisplay.text.back()=="OK");
    // Ring wrap sends a gap before surviving events, never indexes overwritten data.
    for(unsigned i=0;i<40;++i)assert(buttonRing.append(1,1,clockMs));
    buttonSocketTick();assert(buttonClients[0].sent==buttonRing.oldest());
    bool gap=false;for(const auto &s:eventSocket.messages)gap|=s=="{\"gap\":true}";
    assert(gap);
    // Two missed PONGs close; a PONG resets that count.
    clockMs+=10000;buttonSocketTick();assert(buttonClients[0].missed==1);
    buttonSocketEvent(0,WStype_PONG,nullptr,0);assert(buttonClients[0].missed==0);
    clockMs+=10000;buttonSocketTick();clockMs+=10000;buttonSocketTick();
    assert(buttonClients[0].missed==2);clockMs+=10000;buttonSocketTick();
    assert(!buttonClients[0].present);
    buttonSocketEvent(0,WStype_CONNECTED,nullptr,0);authenticate(0,123,buttonRing.next);
    eventSocket.writable=false;buttonRing.append(1,1,clockMs);buttonSocketTick();
    assert(!buttonClients[0].present);
    remoteLastMs=clockMs;remoteDelivered=false;
    assert(buttonRenderStatus()&&gatewayDisplay.text.back()=="getrennt");
    // HTTP fallback uses the existing health authentication and a request nonce.
    server.args={{"after","0"},{"boot","123"},{"client_nonce","00112233445566778899aabbccddeeff"}};
    healthOk=false;handleButtonEvents();assert(server.status==401);
    healthOk=true;handleButtonEvents();
    assert(server.status==200&&server.body.find("\"gap\":true")!=std::string::npos);
    unsigned count=0;size_t at=0;
    while((at=server.body.find("\"id\":",at))!=std::string::npos){++count;at+=5;}
    assert(count==8&&server.body.find("\"nonce\":")!=std::string::npos);
    server.args["boot"]="0";handleButtonEvents();
    assert(server.status==200&&server.body.find("\"events\":")==std::string::npos);
    server.args["client_nonce"]="bad";handleButtonEvents();assert(server.status==400);
    server.args["client_nonce"]="00112233445566778899aabbccddeeff";
    server.args["after"]="4294967296";handleButtonEvents();assert(server.status==400);
    puts("PASS actual gateway: per-sender replay, persist-before-ACK/failure, duplicate re-ACK, FIRE priority");
    puts("PASS auth/bad auth/unauth cap/timeout, resume/resync/gap, signed event ages, PONG timeout, slow client, OLED");
    puts("PASS HTTP authentication, nonce validation, max-eight/gap, boot change and cursor overflow");
}
'''
if __name__=="__main__":
    source=(ROOT/"remote_events.h").read_text()
    globals_=source[source.index("static tm_buttons::Ring"):source.index("static void buttonRandomHex")]
    funcs="\n".join(fixture.function(source,s) for s in [
        "static void buttonRandomHex","static bool buttonNumber","static bool buttonEventText",
        "static void buttonSocketEvent","static void buttonSocketTick","static bool buttonEnqueue",
        "static void buttonRadioTick","static bool buttonRenderStatus",
        "static bool buttonQueryNumber","static void handleButtonEvents"])
    flags=shlex.split(subprocess.check_output(["pkg-config","--cflags","--libs","mbedcrypto"],text=True))
    with tempfile.TemporaryDirectory(prefix="tm-gateway-buttons-") as directory:
        p=Path(directory);cpp=p/"test.cpp";cpp.write_text(STUBS+globals_+funcs+MAIN)
        subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",
            "-I",str(ROOT.parent/"lora-common"),str(cpp),*flags,"-o",str(p/"test")],check=True)
        subprocess.run([str(p/"test")],check=True)
