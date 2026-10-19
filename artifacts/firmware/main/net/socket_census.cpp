#include "socket_census.h"
#include "sdkconfig.h"
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
    char peers[640]={};size_t used=0;
    for(int fd=LWIP_SOCKET_OFFSET;fd<LWIP_SOCKET_OFFSET+CONFIG_LWIP_MAX_SOCKETS;fd++) {
        int type=0;socklen_t size=sizeof(type);
        if(lwip_getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&size)!=0)continue;
        ++open;
        sockaddr_in peer{};size=sizeof(peer);
        int port=0;
        if(lwip_getpeername(fd,(sockaddr*)&peer,&size)==0&&peer.sin_family==AF_INET)
            port=ntohs(peer.sin_port);
        const char *owner=port==81?"gateway-ws":port==80?"http":port==443?"portal-TLS":"other/listen";
        if(port==81)++ws;else if(port==80)++gateway;else if(port==443)++tls;else ++other;
        char detail[128]={};
        portENTER_CRITICAL(&owner_lock);
        memcpy(detail,owners[fd-LWIP_SOCKET_OFFSET],sizeof(detail));detail[sizeof(detail)-1]=0;
        portEXIT_CRITICAL(&owner_lock);
        if(log&&used<sizeof(peers)-1) {
            int n=snprintf(peers+used,sizeof(peers)-used," fd%d=%s:%d[%s]",fd,owner,port,detail);
            if(n>0)used+=(size_t)n<sizeof(peers)-used?(size_t)n:sizeof(peers)-used-1;
        }
    }
    if(log)ESP_LOGI("sockets","open=%d max=%d free=%d gateway_ws81=%d http80=%d TLS443=%d other=%d heap=%u internal=%u%s",
        open,CONFIG_LWIP_MAX_SOCKETS,CONFIG_LWIP_MAX_SOCKETS-open,ws,gateway,tls,other,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),peers);
    errno=saved;return CONFIG_LWIP_MAX_SOCKETS-open;
}
int socket_census_free(){return census(false);}
static void worker(void*) {
    for(;;){census(true);vTaskDelay(pdMS_TO_TICKS(10000));}
}
void socket_census_init() {
    ESP_LOGI("sockets","CONFIG_LWIP_MAX_SOCKETS=%d",CONFIG_LWIP_MAX_SOCKETS);
    if(xTaskCreate(worker,"socket_census",3072,nullptr,1,nullptr)!=pdPASS)
        ESP_LOGE("sockets","socket census task allocation failed");
}
