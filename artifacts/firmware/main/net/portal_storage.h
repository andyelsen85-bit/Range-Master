#pragma once
#include "portal_model.h"
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "../store/storage_write.h"
inline bool portal_queue_read_file(const char *path,PortalQueueRecord *out) {
 FILE *f=fopen(path,"rb");if(!f)return false;
 PortalQueueRecord value={};bool ok=fread(&value,1,sizeof(value),f)==sizeof(value)&&fgetc(f)==EOF&&!ferror(f)&&portal_queue_valid(value);
 if(fclose(f)!=0)ok=false;
 if(ok)*out=value;
 return ok;
}
inline bool portal_queue_repair_safe_files(const char *root) {
 bool found=false;uint32_t generation=0;PortalQueueRecord latest={};
 for(int s=0;s<2;s++) {
  char name[128];snprintf(name,sizeof(name),"%s/queue%d.bin",root,s);
  errno=0;FILE *f=fopen(name,"rb");
  if(!f){if(errno!=ENOENT)return false;continue;}
  if(fclose(f)!=0)return false;
  PortalQueueRecord value={};if(!portal_queue_read_file(name,&value))return false;
  if(!found||value.generation>generation){found=true;generation=value.generation;latest=value;}
 }
 return !found||(!latest.count&&!latest.reserved.id);
}
inline bool portal_queue_write_file(const char *path,const PortalQueueRecord &value) {
 FILE *f=fopen(path,"wb");if(!f)return false;
 bool ok=storage_write_committed(f,&value,sizeof(value));
 if(fclose(f)!=0)ok=false;
 if(!ok)(void)unlink(path);
 return ok;
}
