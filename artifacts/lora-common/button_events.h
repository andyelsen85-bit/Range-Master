#pragma once
// Shared, bounded button-event protocol. No Arduino, store or LVGL dependency.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "trapmaster_auth.h"

namespace tm_buttons {
enum Button : uint8_t { FIRE=1, REPEAT, SKIP, P0, P1, P2 };
constexpr uint8_t BTN_FIRE=FIRE,BTN_REPEAT=REPEAT,BTN_SKIP=SKIP,
    BTN_P0=P0,BTN_P1=P1,BTN_P2=P2;
constexpr size_t SERVER_NONCE_HEX = 16, CLIENT_NONCE_HEX = 32;
constexpr uint32_t MAX_AGE_MS = 3000;
inline bool hex(const char *s, size_t len) {
    if (!s || strlen(s) != len) return false;
    for (size_t i=0; i<len; ++i) if (tm_auth::hex_value(s[i]) < 0) return false;
    return true;
}
inline bool mac(const char *key, const char *text, char *out, size_t bytes=32) {
    if (!key || strlen(key)<16 || !text || !out || bytes>32) return false;
    uint8_t digest[32];
    auto *info=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info || mbedtls_md_hmac(info,(const uint8_t *)key,strlen(key),
            (const uint8_t *)text,strlen(text),digest)) return false;
    for(size_t i=0;i<bytes;++i) {
        out[2*i]=tm_auth::hex_digit(digest[i]>>4);
        out[2*i+1]=tm_auth::hex_digit(digest[i]&15);
    }
    out[2*bytes]=0;
    return true;
}
inline bool equal(const char *a,const char *b,size_t len) {
    if(!hex(a,len)||!hex(b,len)) return false;
    unsigned diff=0;
    for(size_t i=0;i<len;++i) diff |= tm_auth::hex_value(a[i]) ^ tm_auth::hex_value(b[i]);
    return diff==0;
}
inline bool auth_mac(const char *key,const char *server,const char *client,
                     uint32_t after,uint32_t boot,char *out) {
    if(!hex(server,16)||!hex(client,32)) return false;
    char text[128];
    snprintf(text,sizeof(text),"ws|%s|%lu|%lu|%s",server,
        (unsigned long)after,(unsigned long)boot,client);
    return mac(key,text,out);
}
struct Event { uint32_t id; uint8_t src,btn; uint32_t t_ms; };
inline bool event_mac(const char *key,const char *server,const char *client,
                      uint32_t boot,const Event &e,uint32_t age,char *out) {
    if(!hex(server,16)||!hex(client,32)||e.src<1||e.src>8||e.btn<1||e.btn>6) return false;
    char text[160];
    snprintf(text,sizeof(text),"evt|%lu|%lu|%u|%u|%lu|%s|%s",
        (unsigned long)boot,(unsigned long)e.id,e.src,e.btn,
        (unsigned long)age,client,server);
    return mac(key,text,out,8);
}
struct Ring {
    Event items[32]{};
    uint32_t next=0;
    uint8_t count=0;
    bool append(uint8_t src,uint8_t btn,uint32_t now) {
        if(next==UINT32_MAX || src<1||src>8||btn<1||btn>6) return false;
        ++next;
        items[(next-1)%32]={next,src,btn,now};
        if(count<32) ++count;
        return true;
    }
    uint32_t oldest() const { return count ? next-count+1 : next+1; }
    bool gap(uint32_t after) const { return count && after < oldest()-1; }
    const Event *get(uint32_t id) const {
        return count && id>=oldest() && id<=next ? &items[(id-1)%32] : nullptr;
    }
};
// Admission is a two-phase operation: never ACK or advance a cursor until the
// receiving UI queue has accepted the event. Stale valid events can be retired.
struct Cursor {
    uint32_t boot=0,after=0;
    enum Verdict { ACCEPT, DUPLICATE, STALE, INVALID };
    Verdict check(uint32_t b,const Event &e,uint32_t age) const {
        if(b!=boot || !e.id || e.src<1||e.src>8||e.btn<1||e.btn>6) return INVALID;
        if(e.id<=after) return DUPLICATE;
        return age>MAX_AGE_MS ? STALE : ACCEPT;
    }
};
// Stable-level debounce with held-at-boot suppression and per-button cooldown.
struct Input {
    bool raw=false,stable=false,released=false;
    uint32_t changed=0,last=0;
    void init(bool down,uint32_t now) { raw=stable=down; released=!down; changed=now; last=now-250; }
    bool sample(bool down,uint32_t now,uint32_t debounce,bool armed) {
        if(down!=raw) { raw=down;changed=now; }
        if(!armed) released=false;
        if(now-changed<debounce) return false;
        if(!down) released=true;
        if(stable==down) return false;
        stable=down;
        if(!down||!armed||!released||now-last<250) return false;
        released=false;last=now;return true;
    }
};
} // namespace tm_buttons
