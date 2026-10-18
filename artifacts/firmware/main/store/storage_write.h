#pragma once
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
struct StorageWriteFailure { const char *step=nullptr; int error=0; };
// Commit a visible first chunk before growing a file; bound subsequent growth.
// This reduces, but cannot eliminate, FAT's allocation/directory power-cut window.
inline bool storage_write_committed(FILE *file,const void *data,size_t size,bool *complete=nullptr,
                                    StorageWriteFailure *failure=nullptr) {
    if(complete)*complete=false;
    const auto *bytes=(const uint8_t*)data;
    size_t done=0;
    while(done<size) {
        size_t chunk=done?16384:512;
        if(chunk>size-done)chunk=size-done;
        if(fwrite(bytes+done,1,chunk,file)!=chunk) {
            if(failure)*failure={"fwrite",errno?errno:EIO};
            return false;
        }
        done+=chunk;
        if(complete && done==size)*complete=true;
        if(fflush(file)!=0) {
            if(failure)*failure={"fflush",errno?errno:EIO};
            return false;
        }
        if(fsync(fileno(file))!=0) {
            if(failure)*failure={"fsync",errno?errno:EIO};
            return false;
        }
    }
    return true;
}
