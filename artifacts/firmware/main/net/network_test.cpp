#include "network_test.h"
#include "network_trace.h"
#include "bounded_dns.h"
#include "coprocessor.h"
#include "game_store.h"
#include "terminal_logs.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "ping/ping_sock.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include <atomic>
#include <stdio.h>
#include <string.h>
namespace {
TaskHandle_t worker;
std::atomic<bool> running{false};
std::atomic<int64_t> lastAutomatic{0};
portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
char statusText[384]="Netzwerktest bereit (30 s)";
struct Ping {TaskHandle_t task;std::atomic<bool> answered{false};uint32_t ms=0;};
Ping probe{};
void ping_ok(esp_ping_handle_t handle,void *arg) {
    auto *p=(Ping*)arg;
    esp_ping_get_profile(handle,ESP_PING_PROF_TIMEGAP,&p->ms,sizeof(p->ms));p->answered.store(true);
}
void ping_end(esp_ping_handle_t,void *arg){auto *p=(Ping*)arg;xTaskNotifyGive(p->task);}
void hostname(const char *url,char *out,size_t capacity) {
    const char *host=strstr(url,"://");host=host?host+3:url;
    size_t len=strcspn(host,"/:");if(len>=capacity)len=capacity-1;memcpy(out,host,len);out[len]=0;
}
int ping(const char *host,int remaining) {
    if(remaining<1)return -1;
    int64_t deadline=esp_timer_get_time()+(int64_t)remaining*1000;
    struct addrinfo hints{},*info=nullptr;hints.ai_family=AF_INET;
    int result=bounded_getaddrinfo(host,nullptr,&hints,&info,deadline);if(result||!info)return -2;
    remaining=(deadline-esp_timer_get_time())/1000;
    if(remaining<1){freeaddrinfo(info);return -1;}
    esp_ping_config_t cfg=ESP_PING_DEFAULT_CONFIG();
    ip_addr_set_ip4_u32(&cfg.target_addr,((struct sockaddr_in*)info->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(info);cfg.count=1;cfg.interval_ms=100;cfg.timeout_ms=remaining<800?remaining:800;
    esp_ping_callbacks_t callbacks{};callbacks.cb_args=&probe;callbacks.on_ping_success=ping_ok;callbacks.on_ping_end=ping_end;
    probe.task=xTaskGetCurrentTaskHandle();probe.answered.store(false);probe.ms=0;
    (void)ulTaskNotifyTake(pdTRUE,0);
    esp_ping_handle_t handle=nullptr;
    if(esp_ping_new_session(&cfg,&callbacks,&handle)!=ESP_OK)return -3;
    (void)esp_ping_start(handle);
    (void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(remaining<1200?remaining:1200));
    (void)esp_ping_stop(handle);(void)esp_ping_delete_session(handle);
    return probe.answered.load()?(int)probe.ms:-1;
}
void run(void*) {
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        int64_t deadline=esp_timer_get_time()+30000000;
        char router[32]="10.10.20.1",gateway[128],portal[128];
        esp_netif_ip_info_t ip{};
        auto *netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if(netif&&esp_netif_get_ip_info(netif,&ip)==ESP_OK)
            snprintf(router,sizeof(router),IPSTR,IP2STR(&ip.gw));
        ESP_LOGI("network_test","DHCP ip=" IPSTR " netmask=" IPSTR " gateway=" IPSTR " connected=%d",
            IP2STR(&ip.ip),IP2STR(&ip.netmask),IP2STR(&ip.gw),cop_wifi_is_connected());
        hostname(g_store.gatewayUrl,gateway,sizeof(gateway));hostname(g_store.apiUrl,portal,sizeof(portal));
        wifi_ps_type_t ps=WIFI_PS_NONE;esp_err_t psResult=esp_wifi_get_ps(&ps);
        ESP_LOGI("network_test","WiFi rssi=%d last_disconnect=%d power_save=%d ps_result=%s heap_internal=%u",
            network_rssi(),network_disconnect_reason(),(int)ps,esp_err_to_name(psResult),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        int success[3]{},attempts[3]{};int last[3]{-1,-1,-1};
        const char *hosts[]={router,gateway,portal};
        while(esp_timer_get_time()<deadline) {
            for(int i=0;i<3&&esp_timer_get_time()<deadline;i++) {
                if(!hosts[i][0])continue;
                int remaining=(deadline-esp_timer_get_time())/1000;
                last[i]=ping(hosts[i],remaining);attempts[i]++;
                if(last[i]>=0)success[i]++;
                ESP_LOGD("network_test","ping host=%s ms=%d",hosts[i],last[i]);
            }
            char text[384];snprintf(text,sizeof(text),
                "Netzwerktest: Router %d/%d (%d ms), Gateway %d/%d (%d ms), Portal %d/%d (%d ms). RSSI %d dBm",
                success[0],attempts[0],last[0],success[1],attempts[1],last[1],success[2],attempts[2],last[2],network_rssi());
            portENTER_CRITICAL(&lock);snprintf(statusText,sizeof(statusText),"%s",text);portEXIT_CRITICAL(&lock);
            if(esp_timer_get_time()<deadline)vTaskDelay(pdMS_TO_TICKS(100));
        }
        char text[384];network_test_status(text,sizeof(text));
        terminal_log_add("network",success[0]?"INFO":"WARN",text);
        // ICMP may be filtered by the portal; that alone does NOT prove failure.
        terminal_log_add("network","INFO","Netzwerktest beendet. Fehlendes Portal-Ping kann ICMP-Sperre bedeuten; HTTP/TLS-Zeiten separat prüfen.");
        running.store(false);
    }
}
}
void network_test_init() {
    bounded_dns_init();
    network_trace_install_log_filter();
    if(!worker)xTaskCreateWithCaps(run,"network_test",6144,nullptr,2,&worker,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
bool network_test_start(bool automatic) {
    if(!worker)return false;
    if(automatic) {
        int64_t last=lastAutomatic.load(),now=esp_timer_get_time();
        if(last&&now-last<60000000)return false;
        if(!lastAutomatic.compare_exchange_strong(last,now))return false;
    }
    bool expected=false;if(!running.compare_exchange_strong(expected,true))return false;
    portENTER_CRITICAL(&lock);snprintf(statusText,sizeof(statusText),"Netzwerktest läuft (30 s)");portEXIT_CRITICAL(&lock);
    xTaskNotifyGive(worker);return true;
}
void network_test_status(char *out,size_t capacity) {
    portENTER_CRITICAL(&lock);snprintf(out,capacity,"%s",statusText);portEXIT_CRITICAL(&lock);
}
