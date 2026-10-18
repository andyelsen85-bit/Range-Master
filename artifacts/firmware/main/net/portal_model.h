#pragma once
#include <stdint.h>
#include <string.h>
#include <stddef.h>
struct PortalQueuePlayer {int id,entryId;char name[64];};
struct PortalQueueGame {int id,mode,count,status;PortalQueuePlayer players[6];};
struct PortalQueueSnapshot {char version[80];bool enabled;int count,total;PortalQueueGame games[6];};
struct PortalQueueEvent {int queueId,kind;char externalId[40];}; // kind 1 started, 2 release
struct PortalQueueRecord {
 uint32_t magic,version,generation,checksum;
 PortalQueueGame reserved;
 int count;PortalQueueEvent events[8];
};
static_assert(sizeof(PortalQueueRecord)<=1024,"Queue outbox must stay small and bounded");
inline uint32_t portal_queue_checksum(const PortalQueueRecord &r) {
 const auto *p=(const uint8_t*)&r;uint32_t h=2166136261u;
 for(size_t i=0;i<sizeof(r);i++)if(i<offsetof(PortalQueueRecord,checksum)||i>=offsetof(PortalQueueRecord,checksum)+4)h=(h^p[i])*16777619u;
 return h;
}
inline bool portal_queue_valid(const PortalQueueRecord &r) {
 return r.magic==0x51554555&&r.version==1&&r.count>=0&&r.count<=8&&r.reserved.count>=0&&r.reserved.count<=6
  &&r.checksum==portal_queue_checksum(r);
}
inline bool portal_queue_merge(const int previous[6],const PortalQueueGame &game,bool merge,int result[6]) {
 if(game.count<1||game.count>6)return false;
 memset(result,0,sizeof(int)*6);
 if(merge)memcpy(result,previous,sizeof(int)*6);
 for(int i=0;i<game.count;i++) {
  if(game.players[i].id<=0)return false;
  bool seen=false;for(int j=0;j<6;j++)if(result[j]==game.players[i].id)seen=true;
  if(!seen){
   int free=0;while(free<6&&result[free])free++;
   if(free==6)return false;
   result[free]=game.players[i].id;
  }
 }
 return true;
}
struct PortalLiveGate {
 uint64_t version=0,consumed=0;int64_t lastSend=-1500000;
 void changed(){version++;}
 bool ready(int64_t now,bool wifi,bool sync,bool fire)const {
  return version!=consumed&&wifi&&!sync&&!fire&&now-lastSend>=1500000;
 }
 void claim(int64_t now){consumed=version;lastSend=now;}
};
