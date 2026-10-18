#pragma once
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
// Commit a visible first chunk before growing a file; bound subsequent growth.
// This reduces, but cannot eliminate, FAT's allocation/directory power-cut window.
inline bool storage_write_committed(FILE *file,const void *data,size_t size,bool *complete=nullptr) {
    if(complete)*complete=false;
    const auto *bytes=(const uint8_t*)data;
    size_t done=0;
    while(done<size) {
        size_t chunk=done?16384:512;
        if(chunk>size-done)chunk=size-done;
        if(fwrite(bytes+done,1,chunk,file)!=chunk)return false;
        done+=chunk;
        if(complete && done==size)*complete=true;
        if(fflush(file)!=0 || fsync(fileno(file))!=0)return false;
    }
    return true;
}
