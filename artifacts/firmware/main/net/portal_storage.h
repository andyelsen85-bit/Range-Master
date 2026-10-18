#pragma once
#include "portal_model.h"
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "../store/storage_write.h"
#include "../store/storage_repair.h"
#include "../store/fat_name.h"
#include <dirent.h>
#include <strings.h>
inline bool portal_queue_read_file(const char *path,PortalQueueRecord *out) {
 FILE *f=fopen(path,"rb");if(!f)return false;
 PortalQueueRecord value={};bool ok=fread(&value,1,sizeof(value),f)==sizeof(value)&&fgetc(f)==EOF&&!ferror(f)&&portal_queue_valid(value);
 if(fclose(f)!=0)ok=false;
 if(ok)*out=value;
 return ok;
}
inline bool portal_queue_inspect_repair_files(const char *root,QueueRepairInspection *out) {
 *out={};DIR *dir=opendir(root);
 if(!dir){out->unreadable=1;out->error=errno;return false;}
 bool found=false;uint32_t generation=0;PortalQueueRecord latest={};
 for(;;) {
  errno=0;auto *entry=readdir(dir);
  if(!entry){if(errno){++out->unreadable;out->error=errno;}break;}
  if(strcasecmp(entry->d_name,"queue0.bin")&&strcasecmp(entry->d_name,"queue1.bin"))continue;
  ++out->present;char name[384];
  if(!fat_name(name,sizeof(name),entry->d_name,nullptr,root)){++out->unreadable;out->error=EINVAL;continue;}
  PortalQueueRecord value={};
  if(!portal_queue_read_file(name,&value)){++out->unreadable;out->error=errno?errno:EBADMSG;continue;}
  if(!found||value.generation>generation){found=true;generation=value.generation;latest=value;}
 }
 if(closedir(dir)!=0){++out->unreadable;out->error=errno;}
 out->non_empty=found&&(latest.count||latest.reserved.id);
 return !out->unreadable&&!out->non_empty;
}
inline bool portal_queue_repair_safe_files(const char *root) {
 QueueRepairInspection result{};
 return portal_queue_inspect_repair_files(root,&result);
}
inline bool portal_queue_write_file(const char *path,const PortalQueueRecord &value) {
 FILE *f=fopen(path,"wb");if(!f)return false;
 bool ok=storage_write_committed(f,&value,sizeof(value));
 if(fclose(f)!=0)ok=false;
 if(!ok)(void)unlink(path);
 return ok;
}
