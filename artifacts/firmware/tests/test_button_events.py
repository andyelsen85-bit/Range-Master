#!/usr/bin/env python3
"""Actual terminal event validation/admission/handshake; real HMAC, mocked SDK/UI queue."""
from pathlib import Path
import importlib.util
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("fixture",ROOT.parent/"lora-gateway/tests/test_nonce.py")
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
STUBS=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <map>
#include <vector>
#include "button_events.h"
#include "trapmaster_auth.h"
#include "native_button_events.h"
struct cJSON {
    bool string=false,number=false;
    double valuedouble=0;
    char *valuestring=nullptr;
    std::string value;
    std::map<std::string,cJSON> fields;
};
const cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *j,const char *name){
    auto i=j->fields.find(name);return i==j->fields.end()?nullptr:&i->second;
}
bool cJSON_IsNumber(const cJSON *j){return j&&j->number;}
bool cJSON_IsString(const cJSON *j){return j&&j->string;}
bool cJSON_HasObjectItem(const cJSON *j,const char *name){return j->fields.count(name);}
static cJSON parsed;
cJSON *cJSON_ParseWithLength(const char*,size_t){return &parsed;}
void cJSON_Delete(cJSON*){}
void num(cJSON &j,const char *name,uint32_t n){auto &v=j.fields[name];v.number=true;v.valuedouble=n;}
void str(cJSON &j,const char *name,const char *s){
    auto &v=j.fields[name];v.string=true;v.value=s;v.valuestring=v.value.data();
}
struct Shared {ButtonEventsConfig config{};uint32_t generation=0,lastMs=0;bool connected=false;};
struct WireMessage {size_t len;char text[512];};
Shared shared;
tm_buttons::Cursor cursor{123,0};
char clientNonce[33]="00112233445566778899aabbccddeeff";
char serverNonce[17]="0011223344556677";
uint32_t helloBoot=123;
bool authenticated=false;
static unsigned clockMs=10000;
uint32_t nowMs(){return clockMs;}
static unsigned commands=1,mutex=2,capacity=8;
static std::vector<RemoteButtonEvent> queued;
static std::vector<std::string> transmitted,order,logs;
constexpr int pdPASS=1,portMAX_DELAY=0;
int xQueueSend(unsigned,const RemoteButtonEvent *e,int){
    if(queued.size()>=capacity) return 0;
    queued.push_back(*e);order.emplace_back("queue");return pdPASS;
}
void xSemaphoreTake(unsigned,int){}void xSemaphoreGive(unsigned){}
void connection(bool b){shared.connected=b;}
static bool sendOk=true;
bool sendText(const char *s){transmitted.emplace_back(s);order.emplace_back("ack/auth");return sendOk;}
#define ESP_LOGW(tag,fmt,...) logs.emplace_back(fmt)
'''
MAIN=r'''
cJSON event(const Shared &s,uint32_t id,uint32_t age=0,const char *cn=clientNonce,const char *sn=serverNonce) {
    cJSON j;num(j,"id",id);num(j,"src",1);num(j,"btn",1);num(j,"age_ms",age);
    tm_buttons::Event e{id,1,1,0};char sig[17];
    assert(tm_buttons::event_mac(s.config.key,sn,cn,123,e,age,sig));str(j,"sig",sig);return j;
}
int main(){
    Shared s;strcpy(s.config.key,"synthetic-private-auth-key");s.generation=7;
    auto j=event(s,1,200);
    assert(admit(&j,123,serverNonce,clientNonce,s,true));
    assert(queued.size()==1&&queued[0].generation==7&&queued[0].receivedMs==clockMs-200);
    assert(cursor.after==1&&order[0]=="queue"&&order[1]=="ack/auth");
    assert(admit(&j,123,serverNonce,clientNonce,s,true)&&queued.size()==1); // dedupe
    auto next=event(s,2);capacity=1;
    const auto sent=transmitted.size();assert(!admit(&next,123,serverNonce,clientNonce,s,true));
    assert(cursor.after==1&&transmitted.size()==sent); // no ACK before queue admission
    capacity=8;
    auto stale=event(s,2,3001);
    assert(admit(&stale,123,serverNonce,clientNonce,s,true));
    assert(queued.size()==1&&cursor.after==2&&transmitted.size()==sent);
    auto oldSession=event(s,3,0,"ffeeddccbbaa99887766554433221100");
    assert(!admit(&oldSession,123,serverNonce,clientNonce,s,true)&&cursor.after==2);
    auto badAge=event(s,3);num(badAge,"age_ms",500);
    assert(!admit(&badAge,123,serverNonce,clientNonce,s,true));
    auto http=event(s,3,0,"ffeeddccbbaa99887766554433221100","ffeeddccbbaa9988");
    assert(admit(&http,123,"ffeeddccbbaa9988","ffeeddccbbaa99887766554433221100",s,false));
    assert(transmitted.size()==sent&&queued.size()==2&&cursor.after==3); // no WS ACK for HTTP
    // Accepted queue command cannot be delivered again if its ACK write failed.
    sendOk=false;auto failedAck=event(s,4);
    assert(!admit(&failedAck,123,serverNonce,clientNonce,s,true)&&cursor.after==4);
    auto count=queued.size();sendOk=true;
    assert(admit(&failedAck,123,serverNonce,clientNonce,s,true)&&queued.size()==count);
    // Hello signs both nonces and the exact existing boot/cursor.
    parsed={};num(parsed,"hello",1);num(parsed,"boot",999);str(parsed,"nonce","ffeeddccbbaa9988");
    WireMessage wire{};assert(process(wire,s)&&authenticated&&helloBoot==999);
    char expected[65];
    assert(tm_buttons::auth_mac(s.config.key,serverNonce,clientNonce,cursor.after,cursor.boot,expected));
    assert(transmitted.back().find(expected)!=std::string::npos);
    parsed={};num(parsed,"resync",1);num(parsed,"boot",999);num(parsed,"next",10);
    assert(process(wire,s)&&cursor.boot==999&&cursor.after==10&&queued.size()==count);
    parsed={};num(parsed,"resync",1);num(parsed,"boot",1000);num(parsed,"next",20);
    assert(!process(wire,s)&&cursor.boot==999&&cursor.after==10);
    puts("PASS actual terminal: signature/session/age validation, queue-before-ACK, no ACK on overflow/stale");
    puts("PASS failed ACK dedupe, signed HTTP nonce binding, hello/auth and no-replay boot resync");
}
'''
if __name__=="__main__":
    source=(ROOT/"main/lora_stub/button_events.cpp").read_text()
    # Workers must never bypass the UI boundary.
    assert "g_store" not in source and "lv_" not in source
    functions="\n".join(fixture.function(source,sig) for sig in [
        "bool number(","const char *text(","bool admit(","bool process("])
    flags=shlex.split(subprocess.check_output(["pkg-config","--cflags","--libs","mbedcrypto"],text=True))
    with tempfile.TemporaryDirectory(prefix="tm-terminal-events-") as directory:
        p=Path(directory)
        (p/"native_button_events.h").write_text((ROOT/"main/lora_stub/button_events.h").read_text())
        cpp=p/"test.cpp";cpp.write_text(STUBS+functions+MAIN)
        subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",
            "-I",str(p),"-I",str(ROOT.parent/"lora-common"),str(cpp),*flags,"-o",str(p/"test")],check=True)
        subprocess.run([str(p/"test")],check=True)
