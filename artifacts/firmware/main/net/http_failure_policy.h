#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
struct HttpFailurePolicy {
    struct Entry {char path[160]{},method[8]{};int64_t blockedUntil=0,nextLog=0;bool used=false;int status=0;} entries[32];
    static bool contract_error(const char *path,int status) {
        if(!path || (status!=404 && status!=405))return false;
        return !strncmp(path,"/api/sync/",10) || !strncmp(path,"/sync/",6);
    }
    int blocked_status(const char *path,int64_t now,const char *method="POST") const {
        for(const auto &e:entries)if(e.used&&!strcmp(e.path,path)&&!strcmp(e.method,method)&&now<e.blockedUntil)return e.status;
        return 0;
    }
    void reset_contract_errors() {
        for(auto &e:entries)if(e.status==404 || e.status==405)e={};
    }
    Entry &entry(const char *path,const char *method="POST") {
        Entry *victim=&entries[0];
        for(auto &e:entries) {
            if(e.used&&!strcmp(e.path,path)&&!strcmp(e.method,method))return e;
            if(!e.used){victim=&e;break;}
            if(e.nextLog<victim->nextLog)victim=&e;
        }
        *victim={};victim->used=true;snprintf(victim->path,sizeof(victim->path),"%s",path);
        snprintf(victim->method,sizeof(victim->method),"%s",method);return *victim;
    }
    bool blocked(const char *path,int64_t now,const char *method="POST") {
        for(auto &e:entries)if(e.used&&!strcmp(e.path,path)&&!strcmp(e.method,method))return now<e.blockedUntil;
        return false;
    }
    bool failure(const char *path,int status,int64_t now,const char *method="POST") {
        auto &e=entry(path,method);
        bool contract=contract_error(path,status);
        if(status==400 || contract)e.blockedUntil=now+600000000LL;
        bool log=now>=e.nextLog || e.status!=status;
        e.status=status;e.nextLog=log?now+(contract?3600000000LL:60000000LL):e.nextLog;return log;
    }
};
