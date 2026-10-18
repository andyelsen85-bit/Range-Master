"""Real PIN state machine with deterministic host-only crypto/NVS adapters.

The adapter checks the production PBKDF2 arguments, not the cryptographic
primitive (provided by mbedTLS in the real ESP32-P4 build).
"""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]/"main/store"
SDK={
"nvs.h":r'''
#pragma once
#include <vector>
#include <cstring>
#include <cstdint>
#include <cassert>
using esp_err_t=int;using nvs_handle_t=unsigned;
constexpr int ESP_OK=0,ESP_ERR_NVS_NOT_FOUND=1,NVS_READWRITE=1;
inline std::vector<uint8_t> blob,staged;
inline bool commit_fail=false;
inline int nvs_open(const char *name,int,unsigned *h){assert(!strcmp(name,"tm_admin"));*h=1;return 0;}
inline void nvs_close(unsigned){staged.clear();}
inline int nvs_get_blob(unsigned,const char*,void *out,size_t *bytes){
 if(blob.empty())return ESP_ERR_NVS_NOT_FOUND;
 if(*bytes<blob.size())return -1;memcpy(out,blob.data(),blob.size());*bytes=blob.size();return 0;
}
inline int nvs_set_blob(unsigned,const char*,const void *p,size_t bytes){staged.assign((const uint8_t*)p,(const uint8_t*)p+bytes);return 0;}
inline int nvs_commit(unsigned){if(commit_fail)return -1;blob=staged;return 0;}
''',
"esp_timer.h":r'''
#pragma once
#include <cstdint>
inline int64_t host_us=1000000;
inline int64_t esp_timer_get_time(){return host_us;}
''',
"esp_random.h":r'''
#pragma once
#include <cstddef>
#include <cstdint>
inline void esp_fill_random(void *out,size_t bytes){static uint8_t n=1;auto p=(uint8_t*)out;while(bytes--)*p++=n++;}
''',
"mbedtls/md.h":r'''
#pragma once
constexpr int MBEDTLS_MD_SHA256=6;
''',
"mbedtls/pkcs5.h":r'''
#pragma once
#include "md.h"
#include <cstddef>
#include <cstdint>
#include <cassert>
inline int mbedtls_pkcs5_pbkdf2_hmac_ext(int md,const unsigned char *pw,size_t n,
 const unsigned char *salt,size_t s,unsigned iterations,size_t bytes,unsigned char *out){
 assert(md==MBEDTLS_MD_SHA256&&s==16&&iterations==120000&&bytes==32);
 uint32_t h=2166136261u;while(n--)h=(h^*pw++)*16777619u;while(s--)h=(h^*salt++)*16777619u;
 while(bytes--){h=h*1664525u+1013904223u;*out++=h>>24;}return 0;
}
'''}
MAIN=r'''
#include "storage_admin.h"
#include "nvs.h"
#include "esp_timer.h"
#include <cstdio>
int main(){
 assert(!storage_admin_configured());
 assert(!storage_admin_set("","12a4"));assert(!storage_admin_set("","123"));
 assert(!storage_admin_verify("2468"));
 assert(storage_admin_set("","2468"));assert(storage_admin_configured());
 assert(storage_admin_verify("2468"));
 assert(!storage_admin_set("wrong","8642"));
 assert(storage_admin_set("2468","8642"));assert(!storage_admin_verify("2468"));
 assert(storage_admin_verify("8642"));
 for(int i=0;i<5;i++)assert(!storage_admin_verify("0000"));
 assert(!storage_admin_verify("8642"));
 host_us+=30000001;assert(storage_admin_verify("8642"));
 commit_fail=true;assert(!storage_admin_verify("8642"));commit_fail=false;
 assert(storage_admin_verify("8642"));
 blob[0]^=1;assert(storage_admin_configured()); // corrupt is not "unset"
 assert(!storage_admin_set("","1111"));assert(!storage_admin_verify("8642"));
 puts("PASS actual storage PIN: setup/change, PBKDF2 arguments, wrong PIN, persisted attempt count, cooldown, NVS failure and corrupt-state fail-closed.");
}
'''
with tempfile.TemporaryDirectory(prefix="storage-pin-") as directory:
    path=Path(directory)
    for name,content in SDK.items():
        file=path/name;file.parent.mkdir(parents=True,exist_ok=True);file.write_text(content)
    (path/"test.cpp").write_text(MAIN)
    subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-Wno-misleading-indentation",
                    "-I",str(path),"-I",str(ROOT),str(ROOT/"storage_admin.cpp"),str(path/"test.cpp"),
                    "-o",str(path/"test")],check=True)
    subprocess.run([str(path/"test")],check=True)
