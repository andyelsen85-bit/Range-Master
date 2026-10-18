#pragma once
#include "fat_name.h"
#include <fcntl.h>
#include <unistd.h>

struct FatPattern {const char *base,*extension;bool cache;};
inline constexpr FatPattern FAT_PATTERNS[]={
    {"ACTIVE0","BIN",false},{"ACTIVE0","TMP",false},{"ACTIVE1","BIN",false},{"ACTIVE1","TMP",false},
    {"ROSTER","BIN",true},{"ROSTER","TMP",true},{"ROSTER","BAK",true},
    {"PRODUCTS","BIN",true},{"PRODUCTS","TMP",true},{"PRODUCTS","BAK",true},
    {"HISTORY","BIN",true},{"HISTORY","TMP",true},{"HISTORY","BAK",true},
    {"CREDITS","BIN",true},{"CREDITS","TMP",true},{"CREDITS","BAK",true},
    {"SALES","BIN",true},{"SALES","TMP",true},{"SALES","BAK",true},
    {"BILLS","BIN",true},{"BILLS","TMP",true},{"BILLS","BAK",true},
    {"META","BIN",true},{"META","TMP",true},{"META","BAK",true},
    {"J000","REC",false},{"J000","PRT",false},{"J000","BAK",false},
    {"J999","REC",false},{"J999","PRT",false},{"J999","BAK",false},
    {"QUEUE0","BIN",false},{"QUEUE1","BIN",false},
    {"DAYP0","BIN",false},{"DAYP1","BIN",false},{"PROBE","TMP",false}
};
// Dedicated scratch names, never active slots or game records. An interrupted
// self-test may remove only a file with this exact non-game marker and EOF.
inline bool fat_test_owned(const char *path,bool allow_missing=true) {
    FILE *f=fopen(path,"rb");if(!f)return allow_missing&&errno==ENOENT;
    char bytes[24]={};size_t n=fread(bytes,1,sizeof(bytes),f);bool eof=feof(f)&&!ferror(f);
    int read_error=ferror(f)?(errno?errno:EIO):0;
    bool closed=fclose(f)==0;
    bool owned=closed&&eof&&n==sizeof("RM-FAT-SELFTEST-v1")&&!memcmp(bytes,"RM-FAT-SELFTEST-v1",n);
    if(!owned)errno=read_error?read_error:!closed?(errno?errno:EIO):EILSEQ;
    return owned;
}
inline bool fat_patterns_self_test(const char *root,char *error,size_t capacity) {
    for(const auto &pattern:FAT_PATTERNS) {
        char actual[192],from[192],to[192],base[9],destination[9],directory[160];
        snprintf(directory,sizeof(directory),"%s%s",root,pattern.cache?"/TMCACHE":"");
        if(!fat_name(actual,sizeof(actual),pattern.base,pattern.extension,directory))return false;
        snprintf(base,sizeof(base),"%s",pattern.base);base[0]='Z';
        snprintf(destination,sizeof(destination),"%s",pattern.base);destination[0]='Y';
        if(!fat_name(from,sizeof(from),base,pattern.extension,directory)||
           !fat_name(to,sizeof(to),destination,pattern.extension,directory))return false;
        const char *step="scratch ownership";bool created=false;
        if(!fat_test_owned(from)||!fat_test_owned(to))goto failed;
        if((unlink(from)!=0&&errno!=ENOENT)||(unlink(to)!=0&&errno!=ENOENT))goto failed;
        {
            step="open";int fd=open(from,O_WRONLY|O_CREAT|O_EXCL,0600);
            if(fd<0)goto failed;
            created=true;step="write";
            bool ok=write(fd,"RM-FAT-SELFTEST-v1",sizeof("RM-FAT-SELFTEST-v1"))==(ssize_t)sizeof("RM-FAT-SELFTEST-v1");
            if(ok){step="fsync";ok=fsync(fd)==0;}
            int saved=errno;
            if(close(fd)!=0&&ok){ok=false;step="close";saved=errno;}
            errno=saved;if(!ok)goto failed;
        }
        step="rename";if(rename(from,to)!=0)goto failed;
        created=false;step="read";if(!fat_test_owned(to,false))goto failed;
        step="unlink";if(unlink(to)!=0)goto failed;
        continue;
failed:
        {
            int saved=errno?errno:EIO;
            snprintf(error,capacity,"FAT %.90s: %s errno=%d",actual,step,saved);
            if(created)(void)unlink(from);
            return false;
        }
    }
    if(capacity)error[0]=0;
    return true;
}
