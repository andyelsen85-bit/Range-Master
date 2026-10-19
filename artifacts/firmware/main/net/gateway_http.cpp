#include "gateway_http.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <atomic>

static StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t mutex=xSemaphoreCreateMutexStatic(&mutex_storage);
static esp_http_client_handle_t handle;
static esp_err_t (*callback)(esp_http_client_event_t*);
static void *context;
static esp_err_t dispatch(esp_http_client_event_t *event) {
    if(!callback)return ESP_OK;
    auto copy=*event;copy.user_data=context;return callback(&copy);
}
esp_http_client_handle_t gateway_http_acquire(const esp_http_client_config_t *request) {
    if(xSemaphoreTake(mutex,pdMS_TO_TICKS(1500))!=pdTRUE)return nullptr;
    callback=request->event_handler;context=request->user_data;
    if(handle&&esp_http_client_set_url(handle,request->url)!=ESP_OK) {
        esp_http_client_cleanup(handle);handle=nullptr;
    }
    if(!handle) {
        auto cfg=*request;cfg.event_handler=dispatch;cfg.user_data=nullptr;
        cfg.keep_alive_enable=true;cfg.keep_alive_idle=10;cfg.keep_alive_interval=5;cfg.keep_alive_count=2;
        handle=esp_http_client_init(&cfg);
    }
    if(!handle){callback=nullptr;context=nullptr;xSemaphoreGive(mutex);return nullptr;}
    esp_http_client_set_timeout_ms(handle,request->timeout_ms);
    esp_http_client_delete_header(handle,"X-TrapMaster-Auth");
    return handle;
}
void gateway_http_release(esp_http_client_handle_t client,esp_err_t result) {
    if(result!=ESP_OK&&client){esp_http_client_cleanup(client);handle=nullptr;}
    // Never retain stack-owned response buffers between requests.
    callback=nullptr;context=nullptr;xSemaphoreGive(mutex);
}
