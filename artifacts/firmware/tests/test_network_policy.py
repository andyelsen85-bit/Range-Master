#!/usr/bin/env python3
"""Actual phase wrappers/log aggregation and deadline-safe DNS ownership, no UI."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
STUBS={
"esp_err.h":"""#pragma once
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_ERR_TIMEOUT=263;
inline const char *esp_err_to_name(int r){return r?"ESP_ERR_TIMEOUT":"ESP_OK";}
""",
"freertos/FreeRTOS.h":"""#pragma once
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) (void)(p)
#define portEXIT_CRITICAL(p) (void)(p)
#define portMAX_DELAY 0
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
""",
"freertos/task.h":"""#pragma once
using TaskHandle_t=void*;
inline auto xTaskGetCurrentTaskHandle(){return (void*)1;}
""",
"esp_timer.h":"""#pragma once
#include <cstdint>
extern int64_t clockUs;
inline int64_t esp_timer_get_time(){return clockUs;}
""",
"esp_log.h":"""#pragma once
#include <cstdarg>
using vprintf_like_t=int(*)(const char*,va_list);
using esp_log_level_t=int;
constexpr int ESP_LOG_WARN=2,ESP_LOG_INFO=3;
extern vprintf_like_t logger;
inline auto esp_log_set_vprintf(vprintf_like_t next){auto old=logger;logger=next;return old;}
void capture(int,const char*,...);
#define ESP_LOG_LEVEL(level,tag,fmt,...) capture(level,fmt,##__VA_ARGS__)
""",
"terminal_logs.h":"""#pragma once
inline void terminal_log_add(const char*,const char*,const char*){}
""",
"coprocessor.h":"#pragma once\n",
"lwip/sockets.h":"#pragma once\n#include <sys/socket.h>\n#include <sys/select.h>\n",
"lwip/netdb.h":"#pragma once\n#include <netdb.h>\n",
"mbedtls/ssl.h":"#pragma once\nstruct mbedtls_ssl_context{};\n",
}
TRACE=r'''
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "esp_log.h"
#include "network_trace.h"
#include "bounded_dns.h"
#include "lwip/sockets.h"
#include "mbedtls/ssl.h"
int64_t clockUs=1000000;std::vector<std::string> logs,noise;
void capture(int,const char *format,...){char text[2048];va_list a;va_start(a,format);vsnprintf(text,sizeof(text),format,a);va_end(a);logs.emplace_back(text);}
int original(const char *format,va_list a){char text[2048];int n=vsnprintf(text,sizeof(text),format,a);noise.emplace_back(text);return n;}
vprintf_like_t logger=original;
void sdk(const char *format,...){va_list a;va_start(a,format);logger(format,a);va_end(a);}
int network_rssi(){return -52;}int network_disconnect_reason(){return 8;}
bool network_test_start(bool){return false;}
void socket_census_owner(int,const char*,const char*){}
int bounded_getaddrinfo(const char*,const char*,const addrinfo*,addrinfo**,int64_t){clockUs+=5000;return 0;}
extern "C" int __real_lwip_connect(int,const sockaddr*,socklen_t){clockUs+=10000;return 0;}
extern "C" int __real_mbedtls_ssl_handshake(mbedtls_ssl_context*){clockUs+=30000;return 0;}
extern "C" ssize_t __real_lwip_send(int,const void*,size_t n,int){clockUs+=2000;return n;}
extern "C" ssize_t __real_lwip_recv(int,void*,size_t,int){clockUs+=5000;return 1;}
int selects=0;int64_t selectedUs=0;
extern "C" int __real_lwip_select(int,fd_set*,fd_set*,fd_set*,timeval*t){selects++;selectedUs=t->tv_sec*1000000+t->tv_usec;return 1;}
extern "C" int __wrap_lwip_getaddrinfo(const char*,const char*,const addrinfo*,addrinfo**);
extern "C" int __wrap_lwip_connect(int,const sockaddr*,socklen_t);
extern "C" int __wrap_mbedtls_ssl_handshake(mbedtls_ssl_context*);
extern "C" ssize_t __wrap_lwip_send(int,const void*,size_t,int);
extern "C" ssize_t __wrap_lwip_recv(int,void*,size_t,int);
extern "C" int __wrap_lwip_select(int,fd_set*,fd_set*,fd_set*,timeval*);
int main(){
 network_trace_begin("https://user:synthetic-secret@portal.invalid","/api/sync/kredite?secret=redact",0);
 __wrap_lwip_getaddrinfo("portal.invalid",nullptr,nullptr,nullptr);
 __wrap_lwip_connect(1,nullptr,0);__wrap_mbedtls_ssl_handshake(nullptr);
 __wrap_lwip_send(1,"x",1,0);char b;__wrap_lwip_recv(1,&b,1,0);network_trace_end(ESP_OK,200);
 assert(logs.size()==1&&logs.back().find("dns_ms=5 tcp_ms=10 tls_ms=30 first_byte_ms=52 total_ms=52")!=std::string::npos);
 assert(logs.back().find("synthetic-secret")==std::string::npos&&logs.back().find("redact")==std::string::npos);
 network_trace_begin("https://portal.invalid","/api/sync/kredite",0);
 __wrap_lwip_send(1,"x",1,0);__wrap_lwip_recv(1,&b,1,0);network_trace_end(ESP_OK,200);
 assert(logs.back().find("reused=1")!=std::string::npos);
 int64_t deadline=clockUs+50000;network_trace_begin("https://portal.invalid","/api/sync/kredite",1,deadline);
 timeval timeout{2,0};assert(__wrap_lwip_select(0,nullptr,nullptr,nullptr,&timeout)==1&&selectedUs==50000);
 clockUs=deadline+1;assert(__wrap_lwip_select(0,nullptr,nullptr,nullptr,&timeout)==0&&selects==1);
 network_trace_end(ESP_ERR_TIMEOUT,0);auto count=logs.size();
 for(int i=0;i<20;i++){clockUs+=1000;network_trace_begin("https://portal.invalid","/api/sync/kredite",i);network_trace_end(ESP_ERR_TIMEOUT,0);}
 assert(logs.size()==count);clockUs+=61000000;
 network_trace_begin("https://portal.invalid","/api/sync/kredite",0);network_trace_end(ESP_ERR_TIMEOUT,0);
 assert(logs.size()==count+1&&logs.back().find("count=22")!=std::string::npos);
 network_trace_install_log_filter();for(int i=0;i<10;i++)sdk("esp-tls: select() timeout\n");
 assert(noise.size()==1);clockUs+=61000000;sdk("esp-tls: select() timeout\n");
 assert(noise.size()==3&&noise.back().find("count=11")!=std::string::npos);
 puts("PASS real network wrappers: phase timing, reuse, secret redaction, deadline-clamped select and first/minute error aggregation.");
}
'''
with tempfile.TemporaryDirectory(prefix="rm-network-policy-") as tmp:
 p=Path(tmp);inc=p/"inc"
 for name,text in STUBS.items():
  target=inc/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(text)
 (p/"trace.cpp").write_text((ROOT/"main/net/network_trace.cpp").read_text())
 (p/"test.cpp").write_text(TRACE)
 subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-I"+str(inc),"-I"+str(ROOT/"main/net"),
  str(p/"test.cpp"),str(p/"trace.cpp"),"-o",str(p/"test")],check=True)
 subprocess.run([str(p/"test")],check=True)
