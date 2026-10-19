#include "socket_census.h"
#include "socket_census_config.h"
#include "runtime_safety.h"
#include "lwip/sockets.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
static portMUX_TYPE owner_lock=portMUX_INITIALIZER_UNLOCKED;
static char owners[CONFIG_LWIP_MAX_SOCKETS][128]{};
// Only the census worker logs. socket_census_free() never accesses this array.
struct SocketLine { int fd,port; const char *kind; char detail[64]; };
static SocketLine listed[CONFIG_LWIP_MAX_SOCKETS];
void socket_census_owner(int fd,const char *host,const char *path) {
    int index=fd-LWIP_SOCKET_OFFSET;if(index<0||index>=CONFIG_LWIP_MAX_SOCKETS)return;
    portENTER_CRITICAL(&owner_lock);
    snprintf(owners[index],sizeof(owners[index]),"%.60s/%.45s",host,path);
    portEXIT_CRITICAL(&owner_lock);
}

// Query the real descriptor table, not a create/close approximation. This also
// catches sockets opened inside precompiled lwIP/TLS/WebSocket libraries.
static int census(bool log) {
    int saved=errno,open=0,ws=0,gateway=0,tls=0,other=0;
    for(int fd=LWIP_SOCKET_OFFSET;fd<LWIP_SOCKET_OFFSET+CONFIG_LWIP_MAX_SOCKETS;fd++) {
        int type=0;socklen_t size=sizeof(type);
        if(lwip_getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&size)!=0)continue;
        ++open;
        sockaddr_in peer{};size=sizeof(peer);
        int port=0;
        if(lwip_getpeername(fd,(sockaddr*)&peer,&size)==0&&peer.sin_family==AF_INET)
            port=ntohs(peer.sin_port);
        sockaddr_in local{};size=sizeof(local);
        int local_port=0,accepting=0;
        if(lwip_getsockname(fd,(sockaddr*)&local,&size)==0&&local.sin_family==AF_INET)
            local_port=ntohs(local.sin_port);
        size=sizeof(accepting);
        (void)lwip_getsockopt(fd,SOL_SOCKET,SO_ACCEPTCONN,&accepting,&size);
        const char *owner=port==81?"gateway-ws":port==80?"http":port==443?"portal-TLS":"other/listen";
        if(port==81)++ws;else if(port==80)++gateway;else if(port==443)++tls;else ++other;
        if(log) {
            auto &line=listed[open-1];
            line.fd=fd;line.port=port;line.kind=owner;
            portENTER_CRITICAL(&owner_lock);
            memcpy(line.detail,owners[fd-LWIP_SOCKET_OFFSET],sizeof(line.detail)-1);
            line.detail[sizeof(line.detail)-1]=0;
            portEXIT_CRITICAL(&owner_lock);
            // Descriptor numbers are reused. Known socket roles override a
            // stale trace label left by an earlier HTTP connection on this fd.
            if(port==81)snprintf(line.detail,sizeof(line.detail),"gateway-websocket-client");
            else if(accepting) {
                line.kind="listen";line.port=local_port;
                snprintf(line.detail,sizeof(line.detail),"%s port=%d",
                    local_port==80?"httpd-config-server":"tcp-server",local_port);
            } else if(type==SOCK_DGRAM) {
                line.kind="udp";line.port=local_port;
                snprintf(line.detail,sizeof(line.detail),"%s port=%d",
                    local_port==32768?"httpd-control":
                    local_port==5353?"mdns":local_port==68?"dhcp":"udp-bound",local_port);
            } else if(local_port==80) {
                snprintf(line.detail,sizeof(line.detail),"httpd-config-client");
            } else if(!line.detail[0])snprintf(line.detail,sizeof(line.detail),"tcp local=%d peer=%d",local_port,port);
        }
    }
    if(log) {
        ESP_LOGI("sockets","open=%d max=%d free=%d gateway_ws81=%d http80=%d TLS443=%d other=%d",
            open,CONFIG_LWIP_MAX_SOCKETS,CONFIG_LWIP_MAX_SOCKETS-open,ws,gateway,tls,other);
        ESP_LOGI("sockets","heap=%u internal=%u",
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        for(int i=0;i<open;i++) {
            const auto &line=listed[i];
            ESP_LOGI("sockets","fd=%d kind=%s port=%d owner=%.63s",line.fd,line.kind,line.port,line.detail);
        }
    }
    errno=saved;return CONFIG_LWIP_MAX_SOCKETS-open;
}
int socket_census_free(){return census(false);}
static void worker(void*) {
    runtime_task_watch(RuntimeTask::Census);
    census(true);
    runtime_task_first_report(RuntimeTask::Census);
    for(;;){vTaskDelay(pdMS_TO_TICKS(10000));census(true);}
}
void socket_census_init() {
    ESP_LOGI("sockets","CONFIG_LWIP_MAX_SOCKETS=%d",CONFIG_LWIP_MAX_SOCKETS);
    if(!runtime_optional_tasks_allowed())return;
    static bool started=false;
    if(started)return;
    started=true;
    if(xTaskCreateWithCaps(worker,"socket_census",8192,nullptr,1,nullptr,
                           MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
        started=false;
        ESP_LOGE("sockets","socket census task allocation failed");
    }
}
