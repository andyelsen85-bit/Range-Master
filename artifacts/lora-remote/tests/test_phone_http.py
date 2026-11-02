#!/usr/bin/env python3
"""Execute real HTTP route callbacks/session checks with real PBKDF2, mocked SDK."""
from pathlib import Path
import importlib.util
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("extract", ROOT.parent / "lora-gateway/tests/test_nonce.py")
extract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extract)
STUBS = r'''
#include <atomic>
#include <cassert>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "phone_policy.h"
struct String:std::string {
    using std::string::string;
    String(const std::string &s):std::string(s){}
    String(unsigned n):std::string(std::to_string(n)){}
    int indexOf(char c,int start=0)const{auto p=find(c,start);return p==npos?-1:(int)p;}
    String substring(int start,int end=-1)const{return substr(start,end<0?npos:end-start);}
    void trim(){while(!empty()&&front()==' ')erase(begin());while(!empty()&&back()==' ')pop_back();}
    bool startsWith(const char *p)const{return rfind(p,0)==0;}
    friend String operator+(const String &a,unsigned n){return std::string(a)+std::to_string(n);}
};
constexpr int HTTP_GET=0,HTTP_POST=1,pdPASS=1;
static uint32_t clockMs=1000;uint32_t millis(){return clockMs;}
using QueueHandle_t=int;using TaskHandle_t=int;
struct SerialMock{template<class... T>void printf(const char*,T...){}void println(const char*){}} Serial;
static unsigned queued=0;static uint8_t queuedButton=0,targetBytes[6]{};
template<class T>int xQueueSend(int,const T *c,int){++queued;queuedButton=c->button;memcpy(targetBytes,c->target,6);return pdPASS;}
namespace tm_phone {
struct Store {
    bool fail=false;Credential saved{};
    bool load(Credential&,bool &p,uint32_t &f){p=false;f=0;return true;}
    bool saveCredential(const Credential &c){saved=c;return !fail;}
    bool saveFailures(uint32_t){return !fail;}
} store;
struct Random{uint8_t n=0;void fill(uint8_t *p,size_t len){while(len--)*p++=++n;}} random;
static Auth<Store,Random> auth(store,random);
struct Web {
    int status=0;String body,host="192.168.4.1";
    std::map<std::string,String> args_,headers_,out;
    std::map<std::string,std::function<void()>> handlers;
    std::function<void()> fallback;
    String arg(const char *key){return args_[key];}
    int args(){return args_.size();}
    String header(const char *key){return headers_[key];}
    String hostHeader(){return host;}
    void sendHeader(const char *key,const String &v){out[key]=v;}
    void send(int code,const char*,const String &value){status=code;body=value;}
    void send_P(int code,const char *mime,const char *value){send(code,mime,value);}
    void collectHeaders(const char **,int){}
    template<class F>void on(const char *path,int method,F fn){handlers[std::to_string(method)+path]=fn;}
    template<class F>void onNotFound(F fn){fallback=fn;}
} web;
static uint32_t lastSuccess=0;
static const char PHONE_PAGE[]="offline page";
}
'''
MAIN = r'''
using namespace tm_phone;
void request(const char *path,std::map<std::string,String> args={},bool mutation=true){
    web.args_=args;web.status=0;web.body="";web.out.clear();
    web.headers_["Content-Type"]="application/x-www-form-urlencoded";
    web.handlers.at(std::to_string(mutation?HTTP_POST:HTTP_GET)+path)();
}
void credentials(){web.headers_["Cookie"]=String("tm_session=")+auth.token;}
void groups(unsigned mask){request("/groups",{{"groups",String(mask)},{"csrf",auth.csrf}});}
int main(){
    assert(auth.begin(clockMs));phoneActive=true;routes();
    request("/state",{},false);assert(web.status==200&&web.body.find("\"setup\":true")!=String::npos);
    request("/setup",{{"first","1234"},{"second","5678"}});assert(web.status==400);
    web.headers_["Origin"]="http://evil.example";
    request("/setup",{{"first","1234"},{"second","1234"}});assert(web.status==403&&!auth.provisioned);
    web.headers_["Origin"]="http://192.168.4.1";
    request("/setup",{{"first","1234"},{"second","1234"}});assert(web.status==200&&auth.session);
    assert(web.out["Set-Cookie"].find("HttpOnly; SameSite=Strict")!=String::npos);
    credentials();groups(3);assert(web.status==200&&phoneGroups==3);
    request("/button",{{"button","1"}});assert(web.status==403&&queued==0); // CSRF required
    request("/button",{{"button","1"},{"csrf",auth.csrf}});assert(web.status==200&&queuedButton==1);
    request("/button",{{"button","4"},{"csrf",auth.csrf}});assert(web.status==409&&queued==1);
    phoneResult=PHONE_ACK;groups(2);assert(web.status==200);
    request("/button",{{"button","1"},{"csrf",auth.csrf}});assert(web.status==403);
    request("/button",{{"button","6"},{"csrf",auth.csrf}});assert(web.status==200&&queuedButton==6);
    phoneResult=PHONE_ACK;groups(4);
    auto epoch=phoneEpoch.load();groups(0);groups(4);assert(phoneEpoch>epoch);
    request("/test",{{"machine","C"},{"target","ESP32-P4-010203040506"},{"csrf",auth.csrf}});
    assert(web.status==200&&queuedButton==9&&targetBytes[0]==1&&targetBytes[5]==6);
    phoneResult=PHONE_ACK;
    request("/test",{{"machine","C"},{"target","ESP32-P4-UNKNOWN"},{"csrf",auth.csrf}});assert(web.status==400);
    request("/test",{{"machine","C"},{"target","ESP32-P4-000000000000"},{"csrf",auth.csrf}});assert(web.status==400);
    auto last=lastSuccess;clockMs+=1000;request("/state",{},false);assert(lastSuccess==last);
    String old=auth.token;
    request("/login",{{"first","1234"}});assert(web.status==200&&phoneGroups==0&&old!=auth.token);
    request("/groups",{{"groups","7"},{"csrf",auth.csrf}});assert(web.status==401); // old cookie
    credentials();request("/pin",{{"current","1234"},{"first","5678"},{"second","5678"},{"csrf",auth.csrf}});
    assert(web.status==200&&!auth.session&&phoneGroups==0);
    request("/login",{{"first","5678"}});assert(web.status==200);credentials();groups(7);
    request("/logout",{{"csrf",auth.csrf}});assert(web.status==200&&!auth.session&&!phoneGroups);
    // Device-global failure cutoff, independent of client cookie or reconnect.
    for(unsigned i=0;i<10;++i){
        clockMs+=900001;request("/login",{{"first","9999"}});assert(web.status==403);
    }
    assert(auth.failures==10&&phoneRequest==2);
    web.fallback();assert(web.status==302&&web.out["Location"]=="http://192.168.4.1/");
    puts("PASS actual HTTP callbacks: setup/login/PIN change, cookies/CSRF/origin, group independence, busy rejection, target validation, poll idle, 10-failure cutoff");
}
'''
if __name__ == "__main__":
    source = (ROOT / "phone_server.h").read_text()
    globals_ = source[source.index("struct PhoneCommand"):source.index("namespace tm_phone {")]
    functions = "\n".join(extract.function(source, signature) for signature in [
        "static const char *resultText", "static void headers", "static void error",
        "static void ok", "static bool localPost", "static String cookie",
        "static bool session", "static void success", "static void loginCookie",
        "static void deniedPin", "static void state", "static bool queue", "static void routes"])
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "mbedcrypto"], text=True))
    with tempfile.TemporaryDirectory(prefix="tm-phone-http-") as directory:
        src = Path(directory) / "test.cpp"
        src.write_text(STUBS + globals_ + "\nnamespace tm_phone {\n" + functions + "\n}\n" + MAIN)
        binary = Path(directory) / "test"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-variable", "-I", str(ROOT), str(src), *flags, "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
