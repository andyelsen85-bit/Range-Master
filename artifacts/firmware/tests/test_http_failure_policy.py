#!/usr/bin/env python3
"""Execute the real POST helper with a deterministic transport/monotonic clock."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("fixture",Path(__file__).with_name("test_machine_batch.py"))
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
source=(ROOT/"main/net/http_sync.cpp").read_text()
post=source[source.index("// ── Generic POST helper"):]
functions="\n".join(fixture.function(post,s) for s in [
 "static bool post_blocked(","static bool post_failure_log(","static void contract_error_message(",
 "static int endpoint_blocked_status(","static void http_error_excerpt(","static esp_err_t http_post_json(",
 "static esp_err_t http_get_json("])
stubs=r'''
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <initializer_list>
#include "http_failure_policy.h"
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_ARG=-2,ESP_ERR_INVALID_STATE=-3,ESP_ERR_INVALID_SIZE=-4,HTTP_METHOD_POST=1,HTTP_METHOD_GET=2;
static int64_t now=0;
int64_t esp_timer_get_time(){return now;}
static HttpFailurePolicy s_http_failures;
static int s_last_http_status,requests,delays,log_count,status=400;
static std::string last_log,response=R"({"error":"terminalId invalid"})";
static char s_contract_error[160];
static int diagnostic_errors=0;
static std::string diagnostic;
struct cJSON {std::string endpoint;int status=0;};
cJSON *cJSON_CreateObject(){return new cJSON;}
void cJSON_AddStringToObject(cJSON *data,const char*,const char *value){data->endpoint=value;}
void cJSON_AddNumberToObject(cJSON *data,const char*,int value){data->status=value;}
void cJSON_Delete(cJSON *data){delete data;}
void terminal_log_add(const char *category,const char *level,const char *message,const cJSON *details) {
 assert(!strcmp(category,"diagnostic")&&!strcmp(level,"ERROR"));
 assert(details && !details->endpoint.empty() && (details->status==404||details->status==405));diagnostic=message;diagnostic_errors++;
}
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
#define pdMS_TO_TICKS(n) (n)
void vTaskDelay(int ms){delays++;now+=(int64_t)ms*1000;}
struct SyncHttpMeasurement{explicit SyncHttpMeasurement(const char*){}};
struct http_session_t{void *client=nullptr;};
struct http_acc_t{char *buf;int len,cap;bool truncated;};
static http_acc_t *capture;
http_session_t *http_active_session_for_current_task(){return nullptr;}
int http_session_open(http_session_t *s){s->client=(void*)1;return 0;}
void http_session_close(http_session_t*){}
int http_session_reopen(http_session_t*){return 0;}
void redact_query(const char *s,char *out,size_t n){snprintf(out,n,"%s",s?s:"");}
void set_http_error(const char*,const char*,const char*){}
int http_prepare_request(http_session_t*,const char*,int,const char*,http_acc_t *a){capture=a;return 0;}
int esp_http_client_perform(void*){requests++;if(capture->buf)snprintf(capture->buf,capture->cap,"%s",response.c_str());return 0;}
int esp_http_client_set_user_data(void*,void*){return 0;}
int esp_http_client_get_status_code(void*){return status;}
const char *esp_err_to_name(int){return "mock";}
void warn(const char*,const char *format,...){char line[512];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);last_log=line;log_count++;}
#define ESP_LOGW warn
#define ESP_LOGE warn
static const char *TAG="test";
'''
main=r'''
int main(){
 char body[1024];
 assert(http_post_json("/api/sync/terminal-logs","{}",body,sizeof(body))!=0);
 assert(requests==1&&delays==0&&s_last_http_status==400&&log_count==1);
 assert(last_log.find("terminalId invalid")!=std::string::npos);
 assert(http_post_json("/api/sync/terminal-logs","{}",body,sizeof(body))==ESP_ERR_INVALID_STATE&&requests==1);
 status=500;int before=log_count;
 assert(http_post_json("/api/sync/sales","{}",body,sizeof(body))!=0&&requests==4&&delays==2&&log_count==before+1);
 status=200;assert(http_post_json("/api/sync/kredite","{}",body,sizeof(body))==0&&requests==5);
 now=600000000LL;status=400;
 assert(http_post_json("/api/sync/terminal-logs","{}",body,sizeof(body))!=0&&requests==6);
 status=401;assert(http_post_json("/api/sync/payments","{}",body,sizeof(body))!=0&&requests==7);
 char excerpt[201];http_error_excerpt("password=never-log-this",excerpt);
 assert(!strstr(excerpt,"never-log-this"));
 std::string long_body(400,'x');http_error_excerpt(long_body.c_str(),excerpt);assert(strlen(excerpt)==200);
  status=404;int count=requests,waits=delays;
  assert(http_post_json("/api/sync/kredite/state","{}",body,sizeof(body))!=0);
  assert(requests==count+1&&delays==waits&&diagnostic_errors==1);
  assert(!strcmp(s_contract_error,"Portal-Version veraltet: /sync/kredite/state fehlt"));
  assert(http_post_json("/api/sync/kredite/state","{}",body,sizeof(body))==ESP_ERR_INVALID_STATE);
  assert(requests==count+1&&diagnostic_errors==1&&s_last_http_status==404);
  status=200;assert(http_get_json("/api/sync/kredite/state",body,sizeof(body))==0);
  status=405;count=requests;
  assert(http_get_json("/api/sync/spieler",body,sizeof(body))!=0&&requests==count+1&&delays==waits);
  assert(diagnostic_errors==2&&diagnostic.find("nicht unterstützt")!=std::string::npos);
  assert(http_get_json("/api/sync/spieler",body,sizeof(body))==ESP_ERR_INVALID_STATE&&requests==count+1&&s_last_http_status==405);
  s_http_failures.reset_contract_errors();status=200;
  assert(http_post_json("/api/sync/kredite/state","{}",body,sizeof(body))==0);
  assert(http_get_json("/api/sync/spieler",body,sizeof(body))==0);
 puts("PASS real POST: 400 one attempt + endpoint-only 10-minute backoff, bounded/redacted body, rate limit, 5xx retry, other endpoints continue");
  puts("PASS real GET/POST: 404/405 stop immediately, ERROR diagnostic once, original status retained, method isolation, new-build recovery");
}
'''
with tempfile.TemporaryDirectory(prefix="terminal-http-") as td:
 path=Path(td);(path/"test.cpp").write_text(stubs+functions+main)
 subprocess.run(["c++","-std=c++17","-Wall","-Wextra","-Werror","-I",str(ROOT/"main/net"),str(path/"test.cpp"),"-o",str(path/"test")],check=True)
 subprocess.run([str(path/"test")],check=True)
identity=fixture.function(source,"static bool terminal_id(")
assert "esp_efuse_mac_get_default" in identity and "ESP_MAC_WIFI_STA" not in identity and "ESP32-P4-UNKNOWN" not in identity
print("PASS terminal identity: factory eFuse, no shared fallback")
