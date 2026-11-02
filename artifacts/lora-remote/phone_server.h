#pragma once
#include <atomic>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_random.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "phone_policy.h"
#include "phone_page.h"

// The HTTP/DNS/PBKDF2 task never touches the OLED, SPI or Radio. A bounded
// mailbox hands admitted actions to the original radio loop. No second sender.
struct PhoneCommand { uint8_t button;uint8_t target[6];uint32_t epoch; };
enum PhoneResult { PHONE_READY,PHONE_QUEUED,PHONE_SENT,PHONE_ACK,PHONE_NO_ACK,
                   PHONE_BUSY,PHONE_ERROR,PHONE_CANCELLED };
static std::atomic<bool> phoneActive{false};
static std::atomic<unsigned> phoneGroups{0},phoneEpoch{1};
static std::atomic<int> phoneResult{PHONE_READY},phoneRequest{0};
static QueueHandle_t phoneCommands;
static TaskHandle_t phoneTaskHandle;
static bool phoneAuthorized(unsigned button,uint32_t epoch) {
    const unsigned mask=button>6?4:button<=3?1:2;
    return phoneActive.load()&&phoneEpoch.load()==epoch&&(phoneGroups.load()&mask);
}
static void phoneRevoke() {
    phoneGroups=0;++phoneEpoch;
    const int result=phoneResult.load();
    if(result==PHONE_SENT||result==PHONE_QUEUED) phoneResult=PHONE_CANCELLED;
}
namespace tm_phone {
struct NvsStore {
    Preferences prefs;
    bool load(Credential &c,bool &present,uint32_t &failures) {
        if(!prefs.begin("tm_phone",false)) return false;
        present=prefs.isKey("credential");
        if(present&&(prefs.getBytesLength("credential")!=sizeof(c)||
           prefs.getBytes("credential",&c,sizeof(c))!=sizeof(c)||c.version!=1)) return false;
        if(prefs.isKey("failures")&&prefs.getType("failures")!=PT_U32) return false;
        failures=prefs.getUInt("failures",0);return true;
    }
    bool saveCredential(const Credential &c) {return prefs.putBytes("credential",&c,sizeof(c))==sizeof(c);}
    bool saveFailures(uint32_t n) {return prefs.putUInt("failures",n)==sizeof(n);}
    bool reset() {
        // Never clear the remote's identity/counter or its LoRa provisioning.
        if(prefs.isKey("credential")&&!prefs.remove("credential")) return false;
        return saveFailures(0);
    }
};
struct Random {void fill(uint8_t *out,size_t n){esp_fill_random(out,n);}};
static NvsStore store;
static Random random;
static Auth<NvsStore,Random> auth(store,random);
static WebServer web(80);
static DNSServer dns;
static uint32_t lastSuccess=0,noStationSince=0;
static bool ready=false;
static const char *resultText() {
    switch(phoneResult.load()) {
    case PHONE_QUEUED:return "Anfrage angenommen";
    case PHONE_SENT:return "gesendet";
    case PHONE_ACK:return "ACK erhalten (Gateway, keine Auslösebestätigung)";
    case PHONE_NO_ACK:return "keine Antwort";
    case PHONE_BUSY:return "Funk belegt – nicht gesendet";
    case PHONE_ERROR:return "Speicher-/Verschlüsselungsfehler – nicht gesendet";
    case PHONE_CANCELLED:return "Freigabe beendet – weitere Funkversuche gestoppt";
    default:return "Bereit";
    }
}
static void headers() {
    web.sendHeader("Cache-Control","no-store");
    web.sendHeader("X-Content-Type-Options","nosniff");
    web.sendHeader("Content-Security-Policy",
        "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; frame-ancestors 'none'");
}
static void error(int code,const String &message) {
    headers();web.send(code,"application/json; charset=utf-8","{\"error\":\""+message+"\"}");
}
static void ok() {headers();web.send(200,"application/json","{\"ok\":true}");}
static bool localPost() {
    const String host=web.hostHeader();
    const String origin=web.header("Origin");
    if((host!="192.168.4.1"&&host!="192.168.4.1:80") ||
       (origin.length()&&origin!="http://"+host) ||
       web.header("Content-Type")!="application/x-www-form-urlencoded" ||
       web.arg("plain").length()>512 || web.args()>8) {
        error(403,"Anfrage abgelehnt");return false;
    }
    return true;
}
static String cookie() {
    String text=web.header("Cookie");
    if(text.length()>512) return "";
    int begin=0;
    while(begin<(int)text.length()) {
        int end=text.indexOf(';',begin);if(end<0) end=text.length();
        String part=text.substring(begin,end);part.trim();
        if(part.startsWith("tm_session=")) return part.substring(11);
        begin=end+1;
    }
    return "";
}
static bool session(bool mutation=true) {
    String value=cookie();
    if(!auth.valid(value.c_str(),millis())) {
        if(!auth.session) phoneRevoke();
        error(401,"Bitte anmelden");return false;
    }
    const String csrf=web.arg("csrf");
    if(mutation&&(!localPost()||csrf.length()!=32||!equal(csrf.c_str(),auth.csrf,32))) {
        if(csrf.length()!=32||!equal(csrf.c_str(),auth.csrf,32)) error(403,"Anfrage abgelehnt");
        return false;
    }
    return true;
}
static void success() {auth.lastAction=lastSuccess=millis();}
static void stop(const char *reason) {
    phoneRevoke();auth.revoke();
    dns.stop();web.stop();
    WiFi.softAPdisconnect(true);esp_wifi_stop();WiFi.mode(WIFI_OFF);
    // Keep the deep-sleep guard raised until WiFi/DNS cleanup is complete.
    phoneActive=false;
    Serial.printf("handy mode off (%s)\n",reason);
}
static void loginCookie() {
    phoneRevoke();auth.login(millis());success();
    web.sendHeader("Set-Cookie",String("tm_session=")+auth.token+"; Path=/; HttpOnly; SameSite=Strict");
}
static void deniedPin() {
    if(!auth.healthy) {auth.revoke();phoneRevoke();}
    const uint32_t seconds=(auth.wait(millis())+999)/1000;
    error(403,String("PIN falsch, nächster Versuch in ")+seconds+" s");
    if(auth.failures>=10) phoneRequest=2; // only a physical gesture re-enables it
}
static void state() {
    const String value=cookie();
    const bool signedIn=auth.valid(value.c_str(),millis());
    if(!auth.session) phoneRevoke();
    headers();
    if(!auth.healthy) {error(503,"PIN-Speicher nicht lesbar – physischer PIN-Reset erforderlich");return;}
    const int result=phoneResult.load();
    String body=String("{\"setup\":")+(auth.provisioned?"false":"true")+
        ",\"auth\":"+(signedIn?"true":"false")+
        ",\"groups\":"+String(signedIn?phoneGroups.load():0)+
        ",\"pending\":"+((result==PHONE_SENT||result==PHONE_QUEUED)?"true":"false")+
        ",\"csrf\":\""+(signedIn?auth.csrf:"")+"\",\"status\":\""+
        (signedIn?resultText():auth.provisioned?"Bitte anmelden":"PIN festlegen (vier Ziffern)")+"\"}";
    web.send(200,"application/json; charset=utf-8",body);
}
static bool queue(uint8_t button,const uint8_t *target=nullptr) {
    if(!session()) return false;
    if(!phoneAuthorized(button,phoneEpoch.load())) {error(403,"Gruppe nicht freigegeben");return false;}
    PhoneCommand command{};command.button=button;command.epoch=phoneEpoch.load();
    if(target) memcpy(command.target,target,6);
    int old=phoneResult.load();
    do {
        if(old==PHONE_QUEUED||old==PHONE_SENT) {error(409,"Funk belegt");return false;}
    } while(!phoneResult.compare_exchange_weak(old,PHONE_QUEUED));
    if(xQueueSend(phoneCommands,&command,0)!=pdPASS) {
        phoneResult=PHONE_BUSY;error(409,"Funk belegt");return false;
    }
    success();ok();return true;
}
static void routes() {
    const char *collected[]={"Cookie","Origin","Content-Type"};
    web.collectHeaders(collected,3);
    web.on("/",HTTP_GET,[]{headers();web.send_P(200,"text/html; charset=utf-8",PHONE_PAGE);});
    web.on("/state",HTTP_GET,state);
    web.on("/setup",HTTP_POST,[]{
        if(!localPost()) return;
        if(auth.provisioned||!auth.healthy) {error(403,"PIN bereits gesetzt oder Speicher gesperrt");return;}
        if(!auth.set(web.arg("first").c_str(),web.arg("second").c_str())) {
            error(400,"Zwei gleiche PINs mit genau vier Ziffern erforderlich");return;
        }
        loginCookie();ok();
    });
    web.on("/login",HTTP_POST,[]{
        if(!localPost()) return;
        if(!auth.attempt(web.arg("first").c_str(),millis())) {deniedPin();return;}
        loginCookie();ok();
    });
    web.on("/logout",HTTP_POST,[]{
        if(!session()) return;
        phoneRevoke();auth.revoke();web.sendHeader("Set-Cookie","tm_session=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict");ok();
    });
    web.on("/pin",HTTP_POST,[]{
        if(!session()) return;
        // Freeze phone groups while the current PIN is checked on the worker.
        phoneRevoke();
        if(!auth.attempt(web.arg("current").c_str(),millis())) {deniedPin();return;}
        if(!auth.set(web.arg("first").c_str(),web.arg("second").c_str())) {
            error(400,"Zwei gleiche PINs mit genau vier Ziffern erforderlich");return;
        }
        success();ok(); // all sessions invalid; explicitly log in with the new PIN
    });
    web.on("/groups",HTTP_POST,[]{
        if(!session()) return;
        const String n=web.arg("groups");
        if(n.length()!=1||n[0]<'0'||n[0]>'7') {error(400,"Ungültige Freigabe");return;}
        const unsigned next=n[0]-'0',previous=phoneGroups.load();
        // Revocation cannot be undone by quickly toggling off/on before loop()
        // observes it. Old queued/retrying commands retain the previous epoch.
        if(previous&~next) ++phoneEpoch;
        phoneGroups=next;success();ok();
    });
    web.on("/button",HTTP_POST,[]{
        const String n=web.arg("button");
        if(n.length()!=1||n[0]<'1'||n[0]>'6') {error(400,"Ungültiger Taster");return;}
        queue(n[0]-'0');
    });
    web.on("/test",HTTP_POST,[]{
        const String machine=web.arg("machine"),target=web.arg("target");
        if(machine.length()!=1||machine[0]<'A'||machine[0]>'H'||
           target.length()!=21||!target.startsWith("ESP32-P4-")) {
            error(400,"Maschine und eindeutige Terminal-ID erforderlich");return;
        }
        uint8_t bytes[6]={},any=0;
        for(unsigned i=0;i<12;++i) {
            char c=target[9+i];int v=c>='0'&&c<='9'?c-'0':
                c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
            if(v<0) {error(400,"Ungültige Terminal-ID");return;}
            bytes[i/2]|=v<<(i%2?0:4);
        }
        for(auto b:bytes) any|=b;
        if(!any) {error(400,"Ungültige Terminal-ID");return;}
        queue(7+machine[0]-'A',bytes);
    });
    web.onNotFound([]{
        // Includes Android generate_204, iOS hotspot-detect, Windows connecttest.
        web.sendHeader("Location","http://192.168.4.1/");web.send(302,"text/plain","");
    });
}
static void worker(void*) {
    ready=auth.begin(millis());routes();
    for(;;) {
        const int request=phoneRequest.exchange(0);
        if(request==2&&phoneActive) stop("physical/lockout");
        if(request==3) {
            if(phoneActive) stop("PIN reset");
            auth.revoke();phoneRevoke();
            if(store.reset()) {auth.provisioned=false;auth.failures=0;auth.healthy=ready=true;
                Serial.println("Phone PIN reset (LoRa identity/counter unchanged)");}
            else {auth.healthy=ready=false;Serial.println("Phone PIN reset failed");}
        }
        if(request==1&&!phoneActive) {
            if(!ready) {Serial.println("handy mode unavailable (PIN storage)");}
            else {
                auth.revoke();phoneRevoke();phoneResult=PHONE_READY;
                // An explicit physical activation may unlock the 10-failure cutoff,
                // but never clears the durable failure counter or its backoff.
                char ssid[40];snprintf(ssid,sizeof(ssid),"TrapMaster-Remote-%c",TM_REMOTE_ID);
                WiFi.mode(WIFI_AP);
                const IPAddress ip(192,168,4,1);
                if(!WiFi.softAPConfig(ip,ip,IPAddress(255,255,255,0))||
                   !WiFi.softAP(ssid,nullptr,1,false,2)) {
                    WiFi.mode(WIFI_OFF);Serial.println("handy mode unavailable (WiFi)");
                } else {
                    dns.start(53,"*",ip);web.begin();phoneActive=true;
                    lastSuccess=noStationSince=millis();
                    Serial.printf("handy mode on (%s, 192.168.4.1)\n",ssid);
                }
            }
        }
        if(phoneActive) {
            const uint32_t now=millis();
            if(auth.session&&now-auth.lastAction>=IDLE_MS) {auth.revoke();phoneRevoke();}
            if(WiFi.softAPgetStationNum()) noStationSince=now;
            const char *reason=shutdownReason(now,lastSuccess,noStationSince);
            if(reason) stop(reason);
            else {dns.processNextRequest();web.handleClient();}
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
} // namespace tm_phone
static void phoneInit() {
    phoneCommands=xQueueCreate(1,sizeof(PhoneCommand));
    if(!phoneCommands||xTaskCreatePinnedToCore(tm_phone::worker,"phone_http",12288,
        nullptr,1,&phoneTaskHandle,0)!=pdPASS) {
        Serial.println("handy mode unavailable (task allocation)");
        if(phoneCommands) {vQueueDelete(phoneCommands);phoneCommands=nullptr;}
    }
}
static void phoneToggle() {
    if(phoneTaskHandle) phoneRequest=phoneActive?2:1;
}
static void phoneResetPin() {if(phoneTaskHandle) phoneRequest=3;}
static bool phoneTake(PhoneCommand &out) {
    return phoneCommands&&xQueueReceive(phoneCommands,&out,0)==pdPASS;
}
