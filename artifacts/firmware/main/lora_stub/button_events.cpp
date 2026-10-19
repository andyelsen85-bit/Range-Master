#include "button_events.h"
#include "../../../lora-common/button_events.h"
#include "../../../lora-common/fire_link.h"
#include "esp_websocket_client.h"
#include "esp_http_client.h"
#include "gateway_http.h"
#include "network_trace.h"
#include "coprocessor.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "cJSON.h"
#include <atomic>
#include <string.h>
#include <stdio.h>
#include <math.h>

namespace {
constexpr char TAG[]="remote_buttons";
struct WireMessage { size_t len; char text[512]; };
struct Shared {
    ButtonEventsConfig config{};
    uint32_t generation=0,lastMs=0;
    bool connected=false;
};
Shared shared;
SemaphoreHandle_t mutex;
QueueHandle_t commands,wire;
std::atomic<bool> lost{false};
esp_websocket_client_handle_t socketClient;
tm_buttons::Cursor cursor;
uint32_t helloBoot=0;
char clientNonce[33]{},serverNonce[17]{};
bool authenticated=false;
struct FireExchange {
    bool active=false,progress=false,done=false;
    char payload[256]{},nonce[33]{};
    uint32_t deadline=0,lastSend=0;
    unsigned maxAttempts=3;
    FireLinkResult result{};
} fire;
void *socketReserve;
uint32_t nowMs() { return (uint32_t)(esp_timer_get_time()/1000); }
void randomHex(char *out,size_t bytes) {
    for(size_t i=0;i<bytes;++i) {
        uint8_t b;esp_fill_random(&b,1);
        out[i*2]=tm_auth::hex_digit(b>>4);out[i*2+1]=tm_auth::hex_digit(b&15);
    }
    out[bytes*2]=0;
}
Shared snapshot() {
    Shared s; xSemaphoreTake(mutex,portMAX_DELAY);s=shared;xSemaphoreGive(mutex);return s;
}
void connection(bool value) {
    xSemaphoreTake(mutex,portMAX_DELAY);shared.connected=value;xSemaphoreGive(mutex);
}
bool number(const cJSON *json,const char *name,uint32_t &n) {
    const auto *v=cJSON_GetObjectItemCaseSensitive(json,name);
    if(!cJSON_IsNumber(v)||v->valuedouble<0||v->valuedouble>UINT32_MAX||
       floor(v->valuedouble)!=v->valuedouble) return false;
    n=(uint32_t)v->valuedouble;return true;
}
const char *text(const cJSON *json,const char *name) {
    const auto *v=cJSON_GetObjectItemCaseSensitive(json,name);
    return cJSON_IsString(v)?v->valuestring:nullptr;
}
bool sendText(const char *s) {
    return socketClient && esp_websocket_client_send_text(socketClient,s,strlen(s),
        pdMS_TO_TICKS(100))==(int)strlen(s);
}
void websocketEvent(void *,esp_event_base_t,int32_t type,void *data) {
    // This callback is on the component's own task, never the UI task.
    static WireMessage message{};
    static bool assembling=false;
    if(type==WEBSOCKET_EVENT_DISCONNECTED||type==WEBSOCKET_EVENT_ERROR) {
        lost=true;assembling=false;return;
    }
    if(type!=WEBSOCKET_EVENT_DATA) return;
    const auto *e=(esp_websocket_event_data_t *)data;
    if(e->op_code==8) { lost=true;return; }
    if(e->op_code==9||e->op_code==10) return;
    if(e->payload_offset==0) {
        if(e->op_code!=1 || !e->fin) { lost=true;return; }
        message.len=0;assembling=true;
    }
    if(!assembling||e->data_len<0||e->payload_len<0||e->payload_offset<0||
       (size_t)e->payload_offset!=message.len||
       (size_t)e->payload_len>=sizeof(message.text)||
       message.len+(size_t)e->data_len>=sizeof(message.text)) { lost=true;return; }
    memcpy(message.text+message.len,e->data_ptr,e->data_len);message.len+=e->data_len;
    if(message.len==(size_t)e->payload_len) {
        message.text[message.len]=0;assembling=false;
        if(xQueueSend(wire,&message,0)!=pdPASS) lost=true;
    }
}
bool admit(const cJSON *json,uint32_t boot,const char *sn,const char *cn,const Shared &s,bool ws) {
    uint32_t id,src,btn,age;
    if(!number(json,"id",id)||!number(json,"src",src)||!number(json,"btn",btn)||
       !number(json,"age_ms",age)||src<1||src>8||btn<1||btn>6) return false;
    tm_buttons::Event e{id,(uint8_t)src,(uint8_t)btn,0};
    char expected[17];
    if(!tm_buttons::event_mac(s.config.key,sn,cn,boot,e,age,expected)||
       !tm_buttons::equal(expected,text(json,"sig"),16)) {
        ESP_LOGW(TAG,"Event signature/session rejected");return false;
    }
    auto verdict=cursor.check(boot,e,age);
    if(verdict==tm_buttons::Cursor::INVALID) return false;
    if(verdict==tm_buttons::Cursor::STALE) {
        ESP_LOGW(TAG,"stale button event dropped id=%lu age=%lu",(unsigned long)id,(unsigned long)age);
        cursor.after=id;return true; // reject, no UI command and no delivery ACK
    }
    if(verdict==tm_buttons::Cursor::ACCEPT) {
        RemoteButtonEvent command{(uint8_t)src,(uint8_t)btn,nowMs()-age,s.generation};
        if(xQueueSend(commands,&command,0)!=pdPASS) {
            ESP_LOGW(TAG,"UI queue full: cursor/ACK unchanged");return false;
        }
        xSemaphoreTake(mutex,portMAX_DELAY);shared.lastMs=nowMs();xSemaphoreGive(mutex);
    }
    if(ws) {
        char ack[40];snprintf(ack,sizeof(ack),"{\"ack\":%lu}",(unsigned long)id);
        if(!sendText(ack)) {
            // Queue admission already succeeded. Retain this cursor even if
            // ACK is lost, so reconnect cannot execute the same action twice.
            if(verdict==tm_buttons::Cursor::ACCEPT) cursor.after=id;
            return false;
        }
    }
    if(verdict==tm_buttons::Cursor::ACCEPT) cursor.after=id;
    return true;
}
bool process(const WireMessage &message,const Shared &s) {
    cJSON *json=cJSON_ParseWithLength(message.text,message.len);
    if(!json) return false;
    bool ok=true;
    uint32_t boot,next;
    if(cJSON_HasObjectItem(json,"hello")) {
        const char *nonce=text(json,"nonce");
        ok=number(json,"boot",helloBoot)&&tm_buttons::hex(nonce,16);
        if(ok) {
            memcpy(serverNonce,nonce,17);char mac[65],auth[224];
            ok=tm_buttons::auth_mac(s.config.key,serverNonce,clientNonce,cursor.after,cursor.boot,mac);
            if(ok) {
                snprintf(auth,sizeof(auth),"{\"auth\":\"%s\",\"after\":%lu,\"boot\":%lu,\"client_nonce\":\"%s\"}",
                    mac,(unsigned long)cursor.after,(unsigned long)cursor.boot,clientNonce);
                ok=sendText(auth);
                authenticated=ok;
                if(ok) connection(true);
            }
        }
    } else if(!authenticated) ok=false;
    else if(cJSON_HasObjectItem(json,"fire")) {
        uint32_t status=0;char signature[65];
        const char *nonce=text(json,"fire"),*body=text(json,"body");
        ok=number(json,"status",status)&&status<=599&&
            tm_fire_link::reply_mac(s.config.key,serverNonce,clientNonce,nonce,(int)status,body,signature)&&
            tm_buttons::equal(signature,text(json,"sig"),64);
        if(ok) {
            xSemaphoreTake(mutex,portMAX_DELAY);
            if(fire.active&&nonce&&!strcmp(nonce,fire.nonce)) {
                if(!fire.result.firstByteMs)fire.result.firstByteMs=nowMs();
                if(status==102)fire.progress=true;
                else if(status>=200) {
                    fire.result.status=(int)status;fire.result.finishedMs=nowMs();fire.done=true;
                }
            }
            xSemaphoreGive(mutex);
        }
    }
    else if(cJSON_HasObjectItem(json,"resync")) {
        ok=number(json,"boot",boot)&&boot==helloBoot&&number(json,"next",next);
        if(ok) { cursor.boot=boot;cursor.after=next; }
    } else if(cJSON_HasObjectItem(json,"gap")) {
        ESP_LOGW(TAG,"Gateway event buffer gap");
    } else if(s.config.enabled) ok=admit(json,helloBoot,serverNonce,clientNonce,s,true);
    cJSON_Delete(json);return ok;
}
struct HttpBody { char bytes[2048];size_t len;bool overflow; };
esp_err_t httpEvent(esp_http_client_event_t *e) {
    if(e->event_id==HTTP_EVENT_ON_DATA && e->data_len>0) {
        auto *b=(HttpBody *)e->user_data;
        if(b->len+(size_t)e->data_len>=sizeof(b->bytes)) b->overflow=true;
        else { memcpy(b->bytes+b->len,e->data,e->data_len);b->len+=e->data_len;b->bytes[b->len]=0; }
    }
    return ESP_OK;
}
void fallback(const Shared &s) {
    // Each HTTP request has its own nonce. Never reuse a WebSocket nonce here.
    char cn[33];randomHex(cn,16);
    char url[448],mac[65];uint8_t digest[32];
    if(!tm_auth::make_health_mac((const uint8_t *)s.config.key,strlen(s.config.key),digest)) return;
    tm_auth::mac_to_hex(digest,mac);
    size_t base=strlen(s.config.url);
    while(base && s.config.url[base-1]=='/') --base;
    snprintf(url,sizeof(url),"%.*s/events?after=%lu&boot=%lu&client_nonce=%s",
        (int)base,s.config.url,(unsigned long)cursor.after,(unsigned long)cursor.boot,cn);
    HttpBody body{};
    esp_http_client_config_t cfg{};cfg.url=url;cfg.timeout_ms=1200;
    cfg.disable_auto_redirect=true;cfg.event_handler=httpEvent;cfg.user_data=&body;
    auto client=gateway_http_acquire(&cfg);
    if(!client) return;
    esp_http_client_set_header(client,"X-TrapMaster-Auth",mac);
    network_trace_begin(url,"gateway-events",0);
    esp_err_t rc=esp_http_client_perform(client);
    int code=esp_http_client_get_status_code(client);
    network_trace_end(rc,code);
    gateway_http_release(client,rc);
    if(rc!=ESP_OK||code!=200||body.overflow) return;
    cJSON *json=cJSON_Parse(body.bytes);if(!json) return;
    uint32_t boot,next;
    const char *sn=text(json,"nonce"),*echo=text(json,"client_nonce");
    if(number(json,"boot",boot)&&number(json,"next",next)&&tm_buttons::hex(sn,16)&&
       tm_buttons::equal(echo,cn,32)) {
        if(boot!=cursor.boot||cursor.after>next) { cursor.boot=boot;cursor.after=next; }
        else {
            if(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json,"gap")))
                ESP_LOGW(TAG,"HTTP event buffer gap");
            auto *events=cJSON_GetObjectItemCaseSensitive(json,"events");
            if(cJSON_IsArray(events)&&cJSON_GetArraySize(events)<=8) {
                cJSON *e=nullptr;
                cJSON_ArrayForEach(e,events) if(!admit(e,boot,sn,cn,s,false)) break;
            }
        }
    }
    cJSON_Delete(json);
}
bool wsUrl(const char *url,char *out,size_t cap) {
    // A local IPv4/DNS gateway only: no credentials, paths, redirects or TLS.
    if(strncmp(url,"http://",7)) return false;
    const char *host=url+7;size_t n=strcspn(host,":/");
    if(!n||n>200) return false;
    for(size_t i=0;i<n;++i) {
        char c=host[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-')) return false;
    }
    int len=snprintf(out,cap,"ws://%.*s:81/",(int)n,host);
    return len>0&&(size_t)len<cap;
}
void stopSocket() {
    if(socketClient) {
        esp_websocket_client_stop(socketClient);
        esp_websocket_client_destroy(socketClient);socketClient=nullptr;
    }
    // Also reserve after init failure, when no client handle was created.
    // Do not leave the next attempt competing with later UI allocations.
    if(!socketReserve) {
        socketReserve=heap_caps_malloc(7168,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    }
    connection(false);authenticated=false;xQueueReset(wire);lost=false;
}
void worker(void *) {
    uint32_t generation=UINT32_MAX,down=nowMs(),attemptAt=0,lastHttp=0,started=0;
    unsigned retry=0;
    const uint32_t delays[]={1000,2000,5000,10000};
    for(;;) {
        Shared s=snapshot();
        if(generation!=s.generation) {
            stopSocket();generation=s.generation;cursor={};down=nowMs();attemptAt=0;retry=0;
        }
        char uri[224];
        const bool enabled=strlen(s.config.key)>=16&&wsUrl(s.config.url,uri,sizeof(uri));
        if(!enabled) {
            if(socketClient) stopSocket();
            vTaskDelay(pdMS_TO_TICKS(100));continue;
        }
        // Do not start the component's socket task while WiFi is down: its
        // internal connect path otherwise reports a misleading errno=118 ERROR.
        if(!cop_wifi_is_connected()) {
            if(socketClient)stopSocket();
            attemptAt=nowMs()+1000;
            retry=0;
            vTaskDelay(pdMS_TO_TICKS(100));continue;
        }
        if(socketClient && lost.exchange(false)) {
            if(authenticated && nowMs()-started>=5000) down=nowMs();
            stopSocket();attemptAt=nowMs()+delays[retry<3?retry++:3];
        }
        WireMessage message;
        while(socketClient&&xQueueReceive(wire,&message,0)==pdPASS) {
            if(!process(message,s)) { lost=true;break; }
        }
        uint32_t now=nowMs();
        // This is the sole socket writer. Three total transmissions, byte-for-
        // byte identical; a signed progress reply suppresses short retries.
        xSemaphoreTake(mutex,portMAX_DELAY);
        if(fire.active&&!fire.done&&authenticated&&!lost.load()&&
           !fire.progress&&fire.result.attempts<fire.maxAttempts&&
           (!fire.result.attempts||now-fire.lastSend>=300)) {
            fire.lastSend=now;
            if(!fire.result.attempts)fire.result.sentMs=now;
            ++fire.result.attempts;
            fire.result.ambiguous=true; // even a partial socket write is ambiguous
            if(!sendText(fire.payload))lost=true;
        }
        const bool firing=fire.active;
        xSemaphoreGive(mutex);
        if(!socketClient && (int32_t)(now-attemptAt)>=0) {
            randomHex(clientNonce,16);serverNonce[0]=0;authenticated=false;
            started=now;
            esp_websocket_client_config_t cfg{};cfg.uri=uri;
            cfg.disable_auto_reconnect=true;cfg.task_stack=4096;cfg.task_prio=4;
            cfg.buffer_size=512;cfg.network_timeout_ms=1500;
            cfg.ping_interval_sec=10;cfg.pingpong_timeout_sec=20;
            cfg.task_name="remote_ws";
            heap_caps_free(socketReserve);socketReserve=nullptr;
            socketClient=esp_websocket_client_init(&cfg);
            if(!socketClient || esp_websocket_register_events(socketClient,WEBSOCKET_EVENT_ANY,websocketEvent,nullptr)!=ESP_OK ||
               esp_websocket_client_start(socketClient)!=ESP_OK) {
                stopSocket();attemptAt=now+delays[retry<3?retry++:3];
                ESP_LOGW(TAG,"WebSocket startup failed; free internal=%u",
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            } else attemptAt=now+4000; // hello must arrive promptly
        }
        if(socketClient&&!authenticated&&(int32_t)(now-attemptAt)>=0) lost=true;
        if(authenticated && now-started>=5000) retry=0;
        // down is deliberately not reset by unsuccessful connection attempts.
        if(!firing&&!authenticated && now-down>5000 && s.config.enabled&&s.config.gameActive && now-lastHttp>=2000) {
            lastHttp=now;fallback(s);
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

} // namespace

bool button_events_fire(const char *url,const char *key,const char *nonce,
    const char *mac,char first,char second,uint32_t delayMs,int timeoutMs,
    FireLinkResult *result,unsigned maxAttempts) {
    if(!result)return false;
    *result={};
    if(!mutex||!maxAttempts||maxAttempts>3||!tm_buttons::hex(nonce,32)||!tm_buttons::hex(mac,64))return false;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(!shared.connected||fire.active||strcmp(url,shared.config.url)||strcmp(key,shared.config.key)) {
        xSemaphoreGive(mutex);return false;
    }
    fire={};fire.active=true;fire.maxAttempts=maxAttempts;memcpy(fire.nonce,nonce,33);
    fire.deadline=nowMs()+(uint32_t)timeoutMs;
    if(second)snprintf(fire.payload,sizeof(fire.payload),
        "{\"fire\":\"%s\",\"mac\":\"%s\",\"first\":\"%c\",\"second\":\"%c\",\"delayMs\":%lu}",
        nonce,mac,first,second,(unsigned long)delayMs);
    else snprintf(fire.payload,sizeof(fire.payload),"{\"fire\":\"%s\",\"mac\":\"%s\",\"first\":\"%c\"}",
                  nonce,mac,first);
    xSemaphoreGive(mutex);
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        xSemaphoreTake(mutex,portMAX_DELAY);
        const bool done=fire.done;
        const bool stop=done||!shared.connected||
            (int32_t)(nowMs()-fire.deadline)>=0||
            (!fire.progress&&fire.result.attempts>=fire.maxAttempts&&nowMs()-fire.lastSend>=300);
        if(stop) {
            *result=fire.result;fire.active=false;xSemaphoreGive(mutex);return done;
        }
        xSemaphoreGive(mutex);
    }
}
void button_events_init() {
    if(mutex) return;
    const size_t before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    mutex=xSemaphoreCreateMutex();commands=xQueueCreate(8,sizeof(RemoteButtonEvent));
    wire=xQueueCreate(4,sizeof(WireMessage));
    socketReserve=heap_caps_malloc(7168,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!mutex||!commands||!wire||!socketReserve ||
       xTaskCreateWithCaps(worker,"button_events",8192,nullptr,4,nullptr,
           MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)!=pdPASS) {
        if(mutex) vSemaphoreDelete(mutex);
        if(commands) vQueueDelete(commands);
        if(wire) vQueueDelete(wire);
        heap_caps_free(socketReserve);socketReserve=nullptr;mutex=nullptr;commands=wire=nullptr;
        ESP_LOGE(TAG,"Remote worker unavailable; remote reception disabled");
    }
    ESP_LOGI(TAG,"Internal RAM before=%u after=%u",(unsigned)before,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}
void button_events_publish(const ButtonEventsConfig &config) {
    if(!mutex) return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(config.enabled!=shared.config.enabled || strcmp(config.url,shared.config.url)||
       strcmp(config.key,shared.config.key)) {
        ++shared.generation;shared.connected=false;shared.lastMs=0;
    }
    shared.config=config;
    xSemaphoreGive(mutex);
}
bool button_events_take(RemoteButtonEvent *out) {
    if(!mutex||!out) return false;
    const Shared s=snapshot();
    while(xQueueReceive(commands,out,0)==pdPASS) {
        if(s.config.enabled&&out->generation==s.generation) return true;
    }
    return false;
}
void button_events_status(bool *connected,uint32_t *ageMs) {
    Shared s{};
    if(mutex) s=snapshot();
    if(connected) *connected=s.connected;
    if(ageMs) *ageMs=s.lastMs ? nowMs()-s.lastMs : UINT32_MAX;
}
