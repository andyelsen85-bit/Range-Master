#include "network_test.h"
#include "network_trace.h"
#include "socket_census.h"
#include "bounded_dns.h"
#include "coprocessor.h"
#include "game_store.h"
#include "ui_manager.h"
#include "terminal_logs.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace {
TaskHandle_t worker;
std::atomic<bool> running{false};
portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
char statusText[384]="Netzwerktest bereit (max. 10 s)";
void status(const char *text) {
    portENTER_CRITICAL(&lock);
    snprintf(statusText,sizeof(statusText),"%s",text);
    portEXIT_CRITICAL(&lock);
}
bool busy() {
    bool acquired=false;if(!ui_manager_snapshot_lock(&acquired))return true;
    bool active=g_store.screen==SCREEN_SPIEL||g_store.activeGameOpen||g_store.harakiriOrdersReady;
    if(acquired)ui_manager_unlock();
    return active;
}
void hostname(const char *url,char *out,size_t capacity) {
    const char *host=strstr(url,"://");host=host?host+3:url;
    size_t len=strcspn(host,"/:");if(len>=capacity)len=capacity-1;
    memcpy(out,host,len);out[len]=0;
}
uint16_t checksum(const uint8_t *p,size_t n) {
    uint32_t sum=0;
    for(size_t i=0;i<n;i+=2)sum+=(uint16_t)((p[i]<<8)|(i+1<n?p[i+1]:0));
    while(sum>>16)sum=(sum&0xffff)+(sum>>16);
    return htons((uint16_t)~sum);
}
void run(void*) {
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        int64_t deadline=esp_timer_get_time()+10000000;
        if(busy()){status("Netzwerktest abgebrochen: Spiel aktiv");running=false;continue;}
        if(socket_census_free()<4){status("Sockets erschöpft");running=false;continue;}
        // Exactly ONE raw ICMP socket for the entire test, closed on every exit.
        int fd=lwip_socket(AF_INET,SOCK_RAW,IPPROTO_ICMP);
        if(fd<0) {
            status("Sockets erschöpft");
            terminal_log_add("network","ERROR","Sockets erschöpft: Netzwerktest abgebrochen");
            running=false;continue;
        }
        timeval timeout{0,100000};lwip_setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        char hosts[3][128]={{},{},{}};
        esp_netif_ip_info_t ip{};auto *netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if(netif&&esp_netif_get_ip_info(netif,&ip)==ESP_OK)
            snprintf(hosts[0],sizeof(hosts[0]),IPSTR,IP2STR(&ip.gw));
        bool acquired=false;
        if(ui_manager_snapshot_lock(&acquired)) {
            hostname(g_store.gatewayUrl,hosts[1],sizeof(hosts[1]));
            hostname(g_store.apiUrl,hosts[2],sizeof(hosts[2]));
            if(acquired)ui_manager_unlock();
        }
        sockaddr_in targets[3]{};bool resolved[3]{};
        for(int i=0;i<3&&!busy();i++) {
            addrinfo hints{},*info=nullptr;hints.ai_family=AF_INET;
            int64_t dns_deadline=esp_timer_get_time()+1000000;
            if(dns_deadline>deadline)dns_deadline=deadline;
            if(hosts[i][0]&&bounded_getaddrinfo(hosts[i],nullptr,&hints,&info,dns_deadline)==0&&info) {
                targets[i]=*(sockaddr_in*)info->ai_addr;resolved[i]=true;
            }
            if(info)freeaddrinfo(info);
        }
        unsigned sent[3]{},received[3]{};uint16_t sequence=0;
        int64_t next=esp_timer_get_time();
        while(esp_timer_get_time()<deadline&&!busy()) {
            int target=sequence%3;
            if(resolved[target]) {
                uint8_t packet[16]={8,0,0,0,0x52,0x4d,0,0};
                packet[6]=sequence>>8;packet[7]=sequence&255;
                uint16_t sum=checksum(packet,sizeof(packet));memcpy(packet+2,&sum,sizeof(sum));
                if(lwip_sendto(fd,packet,sizeof(packet),0,(sockaddr*)&targets[target],sizeof(sockaddr_in))<0) {
                    if(errno==ENFILE||errno==EMFILE||errno==ENOMEM) {
                        status("Sockets erschöpft");
                        terminal_log_add("network","ERROR","Sockets erschöpft: Netzwerktest abgebrochen");
                        break;
                    }
                } else {
                    ++sent[target];int64_t until=esp_timer_get_time()+800000;
                    while(esp_timer_get_time()<until&&esp_timer_get_time()<deadline&&!busy()) {
                        uint8_t reply[128];sockaddr_in from{};socklen_t len=sizeof(from);
                        int n=lwip_recvfrom(fd,reply,sizeof(reply),0,(sockaddr*)&from,&len);
                        if(n<20)continue;
                        int header=(reply[0]&15)*4;
                        if(header>=20&&n>=header+8&&reply[header]==0&&
                           from.sin_addr.s_addr==targets[target].sin_addr.s_addr&&
                           !memcmp(reply+header+4,packet+4,4)) {++received[target];break;}
                    }
                }
            }
            char text[384];snprintf(text,sizeof(text),
                "Netzwerktest: Router %u/%u, Gateway %u/%u, Portal %u/%u. RSSI %d dBm",
                received[0],sent[0],received[1],sent[1],received[2],sent[2],network_rssi());
            status(text);++sequence;next+=1000000;
            while(esp_timer_get_time()<next&&esp_timer_get_time()<deadline&&!busy())
                vTaskDelay(pdMS_TO_TICKS(25));
        }
        lwip_close(fd);
        if(busy())status("Netzwerktest abgebrochen: Spiel aktiv");
        char text[384];network_test_status(text,sizeof(text));
        terminal_log_add("network","INFO",text);
        running=false;
    }
}
}
void network_test_init() {
    bounded_dns_init();network_trace_install_log_filter();
    if(!worker&&xTaskCreate(run,"network_test",6144,nullptr,1,&worker)!=pdPASS)
        status("Netzwerktest: zu wenig RAM");
}
bool network_test_start(bool automatic) {
    if(automatic||!worker)return false; // Never triggered by a failed request.
    if(busy()){status("Netzwerktest nicht möglich: Spiel aktiv");return false;}
    if(socket_census_free()<4){status("Sockets erschöpft");return false;}
    bool expected=false;if(!running.compare_exchange_strong(expected,true))return false;
    status("Netzwerktest läuft (max. 10 s)");xTaskNotifyGive(worker);return true;
}
void network_test_status(char *out,size_t capacity) {
    portENTER_CRITICAL(&lock);snprintf(out,capacity,"%s",statusText);portEXIT_CRITICAL(&lock);
}
