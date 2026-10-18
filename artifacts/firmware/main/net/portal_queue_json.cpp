#include "portal_queue_json.h"
#include "display_text.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
static int positive(cJSON *object,const char *key) {
 auto *v=cJSON_GetObjectItemCaseSensitive(object,key);
 return cJSON_IsNumber(v)&&v->valueint>0&&v->valuedouble==(double)v->valueint?v->valueint:0;
}
bool portal_queue_parse(const char *json,PortalQueueSnapshot *out) {
 if(!json||!out)return false;
 cJSON *root=cJSON_Parse(json);if(!root)return false;
 auto *version=cJSON_GetObjectItemCaseSensitive(root,"version"),*settings=cJSON_GetObjectItemCaseSensitive(root,"settings");
 auto *enabled=cJSON_GetObjectItemCaseSensitive(settings,"queueEnabled"),*games=cJSON_GetObjectItemCaseSensitive(root,"games");
 PortalQueueSnapshot next={};bool ok=cJSON_IsString(version)&&version->valuestring[0]&&strlen(version->valuestring)<sizeof(next.version)
  &&cJSON_IsBool(enabled)&&cJSON_IsArray(games)&&cJSON_GetArraySize(games)<=40;
 const char *modes[]={"NORMAL","HARAKIRI","CUSTOM_1","CUSTOM_2","CUSTOM_3","CUSTOM_4","HARAKIRI_CUSTOM"};
 if(ok){snprintf(next.version,sizeof(next.version),"%s",version->valuestring);next.enabled=cJSON_IsTrue(enabled);next.total=cJSON_GetArraySize(games);}
 int seen[40]={},entries[40]={},seenCount=0;cJSON *raw;
 cJSON_ArrayForEach(raw,games) {
  if(!ok)break;
  PortalQueueGame game={};game.id=positive(raw,"queueGameId");game.mode=-1;
  auto *mode=cJSON_GetObjectItemCaseSensitive(raw,"modus"),*state=cJSON_GetObjectItemCaseSensitive(raw,"status"),*players=cJSON_GetObjectItemCaseSensitive(raw,"players");
  if(cJSON_IsString(mode))for(int m=0;m<7;m++)if(!strcmp(mode->valuestring,modes[m]))game.mode=m;
  ok=game.id>0&&game.mode>=0&&cJSON_IsString(state)&&(!strcmp(state->valuestring,"OPEN")||!strcmp(state->valuestring,"LOCKED"))
   &&cJSON_IsArray(players)&&cJSON_GetArraySize(players)>=1&&cJSON_GetArraySize(players)<=6;
  if(!ok)break;
  game.status=strcmp(state->valuestring,"OPEN")?1:0;cJSON *member;
  cJSON_ArrayForEach(member,players) {
   if(seenCount==40){ok=false;break;}
   auto &p=game.players[game.count++];p.id=positive(member,"spielerId");p.entryId=positive(member,"entryId");
   auto *name=cJSON_GetObjectItemCaseSensitive(member,"name");ok=p.id>0&&p.entryId>0&&cJSON_IsString(name)&&name->valuestring[0];
   for(int s=0;s<seenCount;s++)if(seen[s]==p.id||entries[s]==p.entryId)ok=false;
   if(!ok)break;
   seen[seenCount]=p.id;entries[seenCount++]=p.entryId;utf8_to_display(p.name,name->valuestring,sizeof(p.name));
  }
  if(ok&&next.count<6)next.games[next.count++]=game;
 }
 cJSON_Delete(root);if(ok)*out=next;return ok;
}
