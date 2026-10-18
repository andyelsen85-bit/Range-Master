#include "network_trace.h"
#include "network_test.h"
#include "bounded_dns.h"
#include "coprocessor.h"
#include "terminal_logs.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "mbedtls/ssl.h"
#include <atomic>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
namespace {
struct Trace {
    std::atomic<TaskHandle_t> owner{nullptr};
    int64_t start=0,dns=0,tcpStart=0,tcpEnd=0,tlsStart=0,tlsEnd=0,first=0,deadline=0;
    bool sent=false;
    unsigned retry=0;
    char host[128]{},path[128]{};
};
Trace traces[4];
Trace *current() {
    auto task=xTaskGetCurrentTaskHandle();
    for(auto &t:traces)if(t.owner.load()==task)return &t;
    return nullptr;
}
static int64_t now(){return esp_timer_get_time();}
struct ErrorRun { char key[160]{};uint32_t count=0;int64_t first=0,last=0,reported=0; };
ErrorRun errors[16];
portMUX_TYPE errorLock=portMUX_INITIALIZER_UNLOCKED;
std::atomic<vprintf_like_t> previousLogger{nullptr};
struct Noise {char key[112]{};uint32_t count=0;int64_t first=0,last=0,reported=0;};
Noise noise[8];
portMUX_TYPE noiseLock=portMUX_INITIALIZER_UNLOCKED;
int previous_print(const char *format,...) {
    va_list args;va_start(args,format);int result=previousLogger.load()(format,args);va_end(args);return result;
}
int filtered_print(const char *format,va_list args) {
    char text[256];va_list copy;va_copy(copy,args);int length=vsnprintf(text,sizeof(text),format,copy);va_end(copy);
    int kind=strstr(text,"select() timeout")?1:
        strstr(text,"Connection timed out before data was ready")?2:
        (strstr(text,"esp_tls_conn_read error")&&
         (strstr(text,"No more processes")||strstr(text,"Resource temporarily unavailable")||strstr(text,"EAGAIN")))?3:0;
    if(!kind)return previousLogger.load()(format,args);
    auto *trace=current();char key[112];
    snprintf(key,sizeof(key),"%.100s/%d",trace?trace->host:"background",kind);
    int64_t stamp=now(),first=stamp;uint32_t count=1;bool emit=true;
    portENTER_CRITICAL(&noiseLock);
    Noise *entry=nullptr;
    for(auto &n:noise)if(!strcmp(n.key,key)){entry=&n;break;}
    if(!entry)for(auto &n:noise)if(!n.count){entry=&n;break;}
    if(!entry)entry=&noise[0];
    if(strcmp(entry->key,key)){*entry={};snprintf(entry->key,sizeof(entry->key),"%s",key);entry->first=stamp;}
    count=++entry->count;first=entry->first;entry->last=stamp;
    emit=count==1||stamp-entry->reported>=60000000;
    if(emit)entry->reported=stamp;
    portEXIT_CRITICAL(&noiseLock);
    if(!emit)return length;
    int result=previousLogger.load()(format,args);
    if(count>1)previous_print("network_noise host/kind=%s count=%lu first_ms=%lld last_ms=%lld\n",
        key,(unsigned long)count,(long long)(first/1000),(long long)(stamp/1000));
    return result;
}
}
void network_trace_install_log_filter() {
    if(!previousLogger.load()) {
        previousLogger.store(vprintf);
        auto prior=esp_log_set_vprintf(filtered_print);
        previousLogger.store(prior?prior:vprintf);
    }
}
void network_trace_begin(const char *url,const char *path,unsigned retry,int64_t deadline) {
    if(current())return;
    for(auto &t:traces) {
        TaskHandle_t empty=nullptr;
        if(!t.owner.compare_exchange_strong(empty,xTaskGetCurrentTaskHandle()))continue;
        t.start=now();t.dns=t.tcpStart=t.tcpEnd=t.tlsStart=t.tlsEnd=t.first=0;t.sent=false;t.retry=retry;
        t.deadline=deadline;
        const char *host=strstr(url,"://");host=host?host+3:url;
        const char *at=(const char*)memchr(host,'@',strcspn(host,"/"));
        if(at)host=at+1;
        size_t size=strcspn(host,"/:");if(size>=sizeof(t.host))size=sizeof(t.host)-1;
        memcpy(t.host,host,size);t.host[size]=0;
        size_t path_size=path?strcspn(path,"?"):0;
        if(path_size>=sizeof(t.path))path_size=sizeof(t.path)-1;
        if(path_size)memcpy(t.path,path,path_size);
        t.path[path_size]=0;return;
    }
}
void network_trace_end(esp_err_t result,int status) {
    auto *t=current();if(!t)return;
    int64_t end=now();
    int socket_error=errno;
    int dns=t->dns/1000;
    int tcp=t->tcpStart?((t->tcpEnd?t->tcpEnd:(t->tlsStart?t->tlsStart:end))-t->tcpStart)/1000:0;
    int tls=t->tlsStart?((t->tlsEnd?t->tlsEnd:end)-t->tlsStart)/1000:0;
    bool failed=result!=ESP_OK||status>=400;
    bool emit=true;uint32_t count=1;int64_t first=t->start;
    if(failed) {
        if(result!=ESP_OK)(void)network_test_start(true);
        char key[160];snprintf(key,sizeof(key),"%.90s %.45s %d/%d",t->host,t->path,(int)result,status);
        portENTER_CRITICAL(&errorLock);
        ErrorRun *entry=nullptr;
        for(auto &e:errors)if(!strcmp(e.key,key)){entry=&e;break;}
        if(!entry)for(auto &e:errors)if(!e.count){entry=&e;break;}
        if(!entry)entry=&errors[0];
        if(strcmp(entry->key,key)){*entry={};snprintf(entry->key,sizeof(entry->key),"%s",key);entry->first=t->start;}
        count=++entry->count;first=entry->first;entry->last=end;
        emit=count==1||end-entry->reported>=60000000;
        if(emit)entry->reported=end;
        portEXIT_CRITICAL(&errorLock);
    }
    if(emit) {
        // Real DNS/socket/TLS calls are measured by link-time wrappers below.
        esp_log_level_t level=failed?ESP_LOG_WARN:ESP_LOG_INFO;
        ESP_LOG_LEVEL(level,"net_timing",
            "host=%s path=%s dns_ms=%d tcp_ms=%d tls_ms=%d first_byte_ms=%d total_ms=%d result=%s http=%d errno=%d rssi=%d disconnect=%d retry=%u reused=%d count=%lu first_ms=%lld last_ms=%lld",
            t->host,t->path,dns,tcp,tls,t->first?(int)((t->first-t->start)/1000):-1,
            (int)((end-t->start)/1000),esp_err_to_name(result),status,socket_error,network_rssi(),network_disconnect_reason(),
            t->retry,!t->tcpStart,(unsigned long)count,(long long)(first/1000),(long long)(end/1000));
        if(failed) {
            char msg[201];snprintf(msg,sizeof(msg),"Netzwerk %.75s: %.60s HTTP %d, %d ms, RSSI %d, Fehler %lu",
                t->host,esp_err_to_name(result),status,(int)((end-t->start)/1000),network_rssi(),(unsigned long)count);
            terminal_log_add("network","WARN",msg);
        }
    }
    t->owner.store(nullptr);
}
// --wrap measures the actual ESP-IDF transport, not a second synthetic DNS,
// TCP or TLS connection. Only the calling HTTP task's active trace is touched.
extern "C" int __real_lwip_getaddrinfo(const char*,const char*,const struct addrinfo*,struct addrinfo**);
extern "C" int __wrap_lwip_getaddrinfo(const char *host,const char *service,const struct addrinfo *hints,struct addrinfo **out) {
    auto *t=current();int64_t start=now();
    int result=bounded_getaddrinfo(host,service,hints,out,t?t->deadline:0);
    if(t)t->dns+=now()-start;
    return result;
}
extern "C" int __real_lwip_select(int,fd_set*,fd_set*,fd_set*,struct timeval*);
extern "C" int __wrap_lwip_select(int nfds,fd_set *read,fd_set *write,fd_set *except,struct timeval *timeout) {
    auto *t=current();struct timeval bounded{};
    if(t&&t->deadline) {
        int64_t remaining=t->deadline-now();
        if(remaining<=0){errno=ETIMEDOUT;return 0;}
        int64_t requested=timeout?(int64_t)timeout->tv_sec*1000000+timeout->tv_usec:remaining;
        if(remaining<requested||!timeout){bounded.tv_sec=remaining/1000000;bounded.tv_usec=remaining%1000000;timeout=&bounded;}
    }
    return __real_lwip_select(nfds,read,write,except,timeout);
}
extern "C" int __real_lwip_connect(int,const struct sockaddr*,socklen_t);
extern "C" int __wrap_lwip_connect(int fd,const struct sockaddr *addr,socklen_t length) {
    auto *t=current();if(t)t->tcpStart=now();
    int result=__real_lwip_connect(fd,addr,length);if(t&&result==0)t->tcpEnd=now();return result;
}
extern "C" int __real_mbedtls_ssl_handshake(mbedtls_ssl_context*);
extern "C" int __wrap_mbedtls_ssl_handshake(mbedtls_ssl_context *ssl) {
    auto *t=current();
    if(t&&!t->tlsStart){t->tlsStart=now();t->tcpEnd=t->tlsStart;}
    int result=__real_mbedtls_ssl_handshake(ssl);if(t&&result==0)t->tlsEnd=now();return result;
}
extern "C" ssize_t __real_lwip_send(int,const void*,size_t,int);
extern "C" ssize_t __wrap_lwip_send(int fd,const void *data,size_t size,int flags) {
    auto *t=current();if(t&&!t->tcpEnd&&t->tcpStart)t->tcpEnd=now();
    ssize_t result=__real_lwip_send(fd,data,size,flags);
    if(t&&result>0&&(!t->tlsStart||t->tlsEnd))t->sent=true;
    return result;
}
extern "C" ssize_t __real_lwip_recv(int,void*,size_t,int);
extern "C" ssize_t __wrap_lwip_recv(int fd,void *data,size_t size,int flags) {
    ssize_t result=__real_lwip_recv(fd,data,size,flags);auto *t=current();
    if(t&&result>0&&t->sent&&!t->first)t->first=now();
    return result;
}
