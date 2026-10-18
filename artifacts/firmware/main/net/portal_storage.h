#pragma once
#include "portal_model.h"
#include <stdio.h>
#include <unistd.h>
inline bool portal_queue_read_file(const char *path,PortalQueueRecord *out) {
 FILE *f=fopen(path,"rb");if(!f)return false;
 PortalQueueRecord value={};bool ok=fread(&value,1,sizeof(value),f)==sizeof(value)&&fgetc(f)==EOF&&portal_queue_valid(value);
 fclose(f);if(ok)*out=value;return ok;
}
inline bool portal_queue_write_file(const char *path,const PortalQueueRecord &value) {
 FILE *f=fopen(path,"wb");if(!f)return false;
 bool ok=fwrite(&value,1,sizeof(value),f)==sizeof(value);
 if(ok)ok=fflush(f)==0&&fsync(fileno(f))==0;
 if(fclose(f)!=0)ok=false;
 if(!ok)(void)unlink(path);
 return ok;
}
