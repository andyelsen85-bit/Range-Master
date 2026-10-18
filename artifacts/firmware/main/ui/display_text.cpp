#include "display_text.h"
#include "font_coverage.h"
#include "terminal_logs.h"
#include <atomic>
#include <stdio.h>
#include <string.h>

// Strict scalar decoder: invalid bytes become visible replacements, not boxes.
static uint32_t next_codepoint(const unsigned char *&s,size_t &bytes) {
    uint32_t cp=*s;bytes=1;
    if(cp<0x80){s++;return cp;}
    size_t n=cp>=0xc2&&cp<=0xdf?2:cp>=0xe0&&cp<=0xef?3:cp>=0xf0&&cp<=0xf4?4:1;
    if(n==1){s++;return 0xfffd;}
    cp&=(1u<<(7-n))-1;
    for(size_t i=1;i<n;i++) {
        if(!s[i] || (s[i]&0xc0)!=0x80){s++;return 0xfffd;}
        cp=(cp<<6)|(s[i]&0x3f);
    }
    if((n==2&&cp<0x80)||(n==3&&cp<0x800)||(n==4&&cp<0x10000)||
       (cp>=0xd800&&cp<=0xdfff)||cp>0x10ffff){s++;return 0xfffd;}
    bytes=n;s+=n;return cp;
}
void utf8_to_display(char *dst,const char *src,size_t capacity) {
    if(!dst||!capacity)return;
    size_t used=0;unsigned replaced=0;
    const unsigned char *s=(const unsigned char*)(src?src:"");
    while(*s && used+1<capacity) {
        const unsigned char *start=s;size_t bytes;
        uint32_t cp=next_codepoint(s,bytes);
        bool supported=ui_codepoint_supported(cp)||cp=='\n'||cp=='\r'||cp=='\t';
        if(!supported){dst[used++]='?';replaced++;continue;}
        if(used+bytes>=capacity)break; // Never split a valid UTF-8 sequence.
        memmove(dst+used,start,bytes);used+=bytes;
    }
    dst[used]=0;
    if(replaced) {
        static std::atomic<unsigned> total{0};
        unsigned cumulative=total.fetch_add(replaced,std::memory_order_relaxed)+replaced;
        char msg[128];snprintf(msg,sizeof(msg),"unsupported glyph: %u Ersetzungen insgesamt seit Start",cumulative);
        terminal_log_add("unsupported_glyph","WARN",msg);
    }
}
// Latin case folding preserves accents; plain typing also finds accented names.
static uint32_t folded(uint32_t cp) {
    if(cp>='A'&&cp<='Z')cp+=32;
    if((cp>=0xc0&&cp<=0xd6)||(cp>=0xd8&&cp<=0xde))cp+=32;
    if(cp>=0x100&&cp<=0x12f&&!(cp&1))cp++;
    if(cp>=0x132&&cp<=0x137&&!(cp&1))cp++;
    if(cp>=0x139&&cp<=0x148&&(cp&1))cp++;
    if(cp>=0x14a&&cp<=0x177&&!(cp&1))cp++;
    if(cp==0x178)cp=0xff;
    if(cp>=0x179&&cp<=0x17e&&(cp&1))cp++;
    if(cp>=0xe0&&cp<=0xe5)return 'a';
    if(cp==0xe7)return 'c';
    if(cp>=0xe8&&cp<=0xeb)return 'e';
    if(cp>=0xec&&cp<=0xef)return 'i';
    if(cp==0xf1)return 'n';
    if(cp>=0xf2&&cp<=0xf6)return 'o';
    if(cp>=0xf9&&cp<=0xfc)return 'u';
    return cp;
}
static bool prefix(const unsigned char *name,const unsigned char *query) {
    while(*query) {
        if(!*name)return false;
        size_t a,b;
        uint32_t x=next_codepoint(name,a),y=next_codepoint(query,b);
        if(folded(x)!=folded(y))return false;
    }
    return true;
}
bool display_name_matches(const char *name,const char *query) {
    if(!query||!*query)return true;
    const unsigned char *s=(const unsigned char*)(name?name:"");
    while(*s) {
        if(prefix(s,(const unsigned char*)query))return true;
        size_t n;next_codepoint(s,n);
    }
    return false;
}
