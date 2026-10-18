#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
struct HttpFailurePolicy {
    struct Entry {char path[160]{};int64_t blockedUntil=0,nextLog=0;bool used=false;} entries[32];
    Entry &entry(const char *path) {
        Entry *victim=&entries[0];
        for(auto &e:entries) {
            if(e.used&&!strcmp(e.path,path))return e;
            if(!e.used){victim=&e;break;}
            if(e.nextLog<victim->nextLog)victim=&e;
        }
        *victim={};victim->used=true;snprintf(victim->path,sizeof(victim->path),"%s",path);return *victim;
    }
    bool blocked(const char *path,int64_t now) {
        for(auto &e:entries)if(e.used&&!strcmp(e.path,path))return now<e.blockedUntil;
        return false;
    }
    bool failure(const char *path,int status,int64_t now) {
        auto &e=entry(path);
        if(status==400)e.blockedUntil=now+600000000LL;
        bool log=now>=e.nextLog;e.nextLog=log?now+60000000LL:e.nextLog;return log;
    }
};
