#pragma once
#include "button_events.h"

// Replies are bound to both connection challenges AND the original fire nonce.
// A captured result cannot satisfy another action or a reconnected client.
namespace tm_fire_link {
inline bool reply_mac(const char *key,const char *server,const char *client,
                      const char *nonce,int status,const char *body,char *out) {
    if(!tm_buttons::hex(server,16)||!tm_buttons::hex(client,32)||
       !tm_buttons::hex(nonce,32)||!body||strlen(body)>384) return false;
    char message[544];
    const int n=snprintf(message,sizeof(message),"fire-reply|%s|%s|%s|%d|%s",
                        server,client,nonce,status,body);
    return n>0&&(size_t)n<sizeof(message)&&tm_buttons::mac(key,message,out);
}
struct Samples {
    uint32_t values[50]{};
    unsigned count=0,next=0;
    void add(uint32_t value) {values[next]=value;next=(next+1)%50;if(count<50)++count;}
    void clear() {*this=Samples{};}
    void summary(uint32_t &min,uint32_t &median,uint32_t &p95,uint32_t &max) const {
        uint32_t sorted[50];
        for(unsigned i=0;i<count;++i) {
            sorted[i]=values[i];
            for(unsigned j=i;j&&sorted[j]<sorted[j-1];--j) {
                uint32_t t=sorted[j];sorted[j]=sorted[j-1];sorted[j-1]=t;
            }
        }
        min=median=p95=max=0;if(!count)return;
        min=sorted[0];max=sorted[count-1];
        median=count%2?sorted[count/2]:(uint32_t)(((uint64_t)sorted[count/2-1]+sorted[count/2])/2);
        // Nearest rank: actual samples, never an average of percentiles.
        p95=sorted[(95*count+99)/100-1];
    }
};
}
