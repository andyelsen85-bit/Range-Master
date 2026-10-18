#pragma once
#include <cstdint>
// Caller serializes access. Bounded per-message counters, monotonic timestamps.
struct FailureLogCount {uint32_t key=0,count=0;int64_t first=0,last=0,emitted=0;};
struct FailureLogGate {
    FailureLogCount entries[32]{};
    bool admit(uint32_t key,int64_t now,FailureLogCount *result) {
        FailureLogCount *slot=nullptr,*oldest=&entries[0];
        for(auto &entry:entries) {
            if(entry.count&&entry.key==key){slot=&entry;break;}
            if(!entry.count)oldest=&entry;
            else if(oldest->count&&entry.last<oldest->last)oldest=&entry;
        }
        if(!slot){slot=oldest;*slot={key,0,now,now,now-60000000};}
        ++slot->count;slot->last=now;*result=*slot;
        if(slot->count>1&&now-slot->emitted<60000000)return false;
        slot->emitted=now;return true;
    }
};
