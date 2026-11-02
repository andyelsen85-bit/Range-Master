#pragma once
#include "trapmaster_protocol.h"

// A new remote AES domain gets its own replay state. Old gateway relay and
// legacy remote counters are retained, never reset. Full key digest detects
// even a collision of the shortened NVS namespace.
namespace tm_remote_counters {
struct State {uint32_t version=1;uint8_t fingerprint[32]{};uint32_t counters[8]{};};
template<class Store> class Ledger {
    Store &store;
    State state{};
    bool ready=false;
public:
    explicit Ledger(Store &s):store(s) {}
    bool begin(const uint8_t *key) {
        ready=false;uint8_t fingerprint[32];
        if(!tm_protocol::key_fingerprint(key,fingerprint)) return false;
        char ns[15]="tmr_";
        const char hex[]="0123456789abcdef";
        for(unsigned i=0;i<5;++i) {ns[4+2*i]=hex[fingerprint[i]>>4];ns[5+2*i]=hex[fingerprint[i]&15];}
        ns[14]=0;
        if(!store.begin(ns,false)) return false;
        const size_t length=store.getBytesLength("state");
        if(!length&&store.isKey("state")) return false;
        if(length) {
            if(length!=sizeof(state)||store.getBytes("state",&state,sizeof(state))!=sizeof(state)||
               state.version!=1||memcmp(state.fingerprint,fingerprint,32)) return false;
        } else {
            state=State{};memcpy(state.fingerprint,fingerprint,32);
            if(store.putBytes("state",&state,sizeof(state))!=sizeof(state)) return false;
        }
        return ready=true;
    }
    uint32_t counter(unsigned i) const {return ready&&i<8?state.counters[i]:0;}
    bool save(unsigned i,uint32_t counter) {
        if(!ready||i>=8||counter<=state.counters[i]) return false;
        State next=state;next.counters[i]=counter;
        if(store.putBytes("state",&next,sizeof(next))!=sizeof(next)) return false;
        state=next;return true;
    }
};
}
