#include "bounded_dns.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"
#include <atomic>
#include <new>
#include <string.h>
#include <errno.h>
extern "C" int __real_lwip_getaddrinfo(const char*,const char*,const struct addrinfo*,struct addrinfo**);
namespace {
// lwIP's blocking resolver cannot be cancelled. The waiter owns neither its
// arguments nor its result after timeout: the permanent worker frees them.
struct Job {
    std::atomic<int> state{0}; // 0 working, 1 result handed over, 2 abandoned
    char node[256]{},service[16]{};
    bool hasService=false,hasHints=false;
    struct addrinfo hints{},*result=nullptr;
    int error=EAI_AGAIN;
};
QueueHandle_t queue;
void dispose(Job *job){job->~Job();heap_caps_free(job);}
void worker(void*) {
    for(;;) {
        Job *job=nullptr;xQueueReceive(queue,&job,portMAX_DELAY);
        if(job->state.load()!=2)
            job->error=__real_lwip_getaddrinfo(job->node,job->hasService?job->service:nullptr,
                job->hasHints?&job->hints:nullptr,&job->result);
        int waiting=0;
        if(!job->state.compare_exchange_strong(waiting,1)) {
            if(job->result)freeaddrinfo(job->result);
            dispose(job);
        }
        // No access after publishing state=1: the waiter may already free it.
    }
}
}
void bounded_dns_init() {
    if(queue)return;
    queue=xQueueCreate(4,sizeof(Job*));TaskHandle_t handle=nullptr;
    if(queue&&xTaskCreateWithCaps(worker,"bounded_dns",4096,nullptr,3,&handle,
        MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {vQueueDelete(queue);queue=nullptr;}
}
int bounded_getaddrinfo(const char *node,const char *service,const struct addrinfo *hints,
                       struct addrinfo **result,int64_t deadline) {
    if(!deadline)return __real_lwip_getaddrinfo(node,service,hints,result);
    if(!queue||!node||!result||strlen(node)>=256||(service&&strlen(service)>=16))return EAI_AGAIN;
    *result=nullptr;
    void *memory=heap_caps_malloc(sizeof(Job),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!memory)return EAI_MEMORY;
    Job *job=new(memory)Job;
    memcpy(job->node,node,strlen(node)+1);
    if(service){job->hasService=true;memcpy(job->service,service,strlen(service)+1);}
    if(hints) {
        job->hasHints=true;job->hints.ai_family=hints->ai_family;job->hints.ai_socktype=hints->ai_socktype;
        job->hints.ai_protocol=hints->ai_protocol;job->hints.ai_flags=hints->ai_flags;
    }
    if(xQueueSend(queue,&job,0)!=pdTRUE){dispose(job);return EAI_AGAIN;}
    while(job->state.load()!=1&&esp_timer_get_time()<deadline)vTaskDelay(pdMS_TO_TICKS(10));
    int waiting=0;
    if(job->state.compare_exchange_strong(waiting,2)){errno=ETIMEDOUT;return EAI_AGAIN;}
    *result=job->result;int error=job->error;dispose(job);return error;
}
