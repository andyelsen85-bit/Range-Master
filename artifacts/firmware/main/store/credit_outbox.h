#pragma once
#include "game_store.h"
#include <stdint.h>
#include <string.h>

// Count and records share ONE atomic NVS blob. A power cut between updating
// this blob and the old count key must not discard any cancellation.
struct CreditOutboxHeader {uint32_t magic,version,count;char cancelledGameId[40];};
struct CreditOutboxLegacy {char id[40];int player;char day[11],type[8];int quantity;bool flight;};
struct CreditOutboxReceipt {char id[40];int player;char day[11],type[8];int quantity;
    char occurred[32];int revision,cents;bool flight;};
static constexpr uint32_t CREDIT_OUTBOX_MAGIC=0x43524432;
static inline size_t credit_outbox_size(int count) {
    return sizeof(CreditOutboxHeader)+(size_t)count*sizeof(KreditEvent);
}
static inline bool credit_outbox_decode(const void *data,size_t bytes,int legacy_count,KreditEvent *out,int *count,char *cancelled=nullptr) {
    *count=0;memset(out,0,sizeof(KreditEvent)*MAX_KREDIT_EVENTS);
    if(cancelled)cancelled[0]=0;
    bool decoded=false;
    if(bytes>=sizeof(CreditOutboxHeader)) {
        CreditOutboxHeader h;memcpy(&h,data,sizeof(h));
        if(h.magic==CREDIT_OUTBOX_MAGIC) {
            if(h.version!=1||h.count>MAX_KREDIT_EVENTS||bytes!=credit_outbox_size(h.count)||
                !memchr(h.cancelledGameId,0,sizeof(h.cancelledGameId)))return false;
            memcpy(out,(const char*)data+sizeof(h),h.count*sizeof(KreditEvent));*count=h.count;
            if(cancelled)memcpy(cancelled,h.cancelledGameId,40);
            decoded=true;
        }
    }
    if(!decoded) {
        if(legacy_count<=0||legacy_count>MAX_KREDIT_EVENTS)return false;
        bool receipt=bytes==(size_t)legacy_count*sizeof(CreditOutboxReceipt)||bytes==MAX_KREDIT_EVENTS*sizeof(CreditOutboxReceipt);
        bool legacy=bytes==(size_t)legacy_count*sizeof(CreditOutboxLegacy)||bytes==MAX_KREDIT_EVENTS*sizeof(CreditOutboxLegacy);
        if(!receipt&&!legacy)return false;
        for(int i=0;i<legacy_count;i++) {
            CreditOutboxLegacy old{};
            if(receipt) {
                CreditOutboxReceipt r;memcpy(&r,(const char*)data+i*sizeof(r),sizeof(r));
                memcpy(old.id,r.id,40);old.player=r.player;memcpy(old.day,r.day,11);
                memcpy(old.type,r.type,8);old.quantity=r.quantity;old.flight=r.flight;
                memcpy(out[i].occurredAt,r.occurred,32);out[i].preisRevisionId=r.revision;out[i].unitPriceCent=r.cents;
            } else memcpy(&old,(const char*)data+i*sizeof(old),sizeof(old));
            memcpy(out[i].externalId,old.id,40);out[i].spielerId=old.player;
            memcpy(out[i].datum,old.day,11);memcpy(out[i].typ,old.type,8);
            out[i].anzahl=old.quantity;out[i].inFlight=old.flight;
            // Old releases cleared inFlight after failed ACKs. Such a USE can
            // never be proved unsent and must be compensated conservatively.
            out[i].mayBeAccepted=!memcmp(old.type,"USE",4);
        }
        *count=legacy_count;
    }
    for(int i=0;i<*count;i++) {
        const auto&e=out[i];
        if(e.spielerId==0||!e.anzahl||!memchr(e.externalId,0,40)||!e.externalId[0]||
            !memchr(e.datum,0,11)||!memchr(e.typ,0,sizeof(e.typ))||
            !memchr(e.voidsExternalId,0,40))return false;
        if(!strcmp(e.typ,"VOID_USE")) {
            if(e.anzahl!=1||!e.voidsExternalId[0]||!strcmp(e.externalId,e.voidsExternalId))return false;
        } else if((strcmp(e.typ,"GRANT")&&strcmp(e.typ,"USE"))||e.voidsExternalId[0])return false;
    }
    return true;
}
