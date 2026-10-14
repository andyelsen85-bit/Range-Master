#!/usr/bin/env python3
"""Real shared HMAC/nonce/sampling code; no radio hardware or provisioning."""
from pathlib import Path
import shlex
import subprocess
import tempfile

COMMON=Path(__file__).resolve().parents[2]/"lora-common"
GATEWAY=COMMON.parent/"lora-gateway"
MD=r"""
#pragma once
#include <openssl/hmac.h>
#define MBEDTLS_MD_SHA256 1
struct mbedtls_md_info_t {};
inline const mbedtls_md_info_t *mbedtls_md_info_from_type(int) {static mbedtls_md_info_t m;return &m;}
inline int mbedtls_md_hmac(const mbedtls_md_info_t*,const unsigned char *key,size_t k,
 const unsigned char *body,size_t n,unsigned char *out) {
 unsigned size=32;return HMAC(EVP_sha256(),key,(int)k,body,n,out,&size)?0:-1;
}
"""
CPP=r"""
#include "fire_link.h"
#include "nonce_registry.h"
#include <cassert>
#include <cstdio>
int main() {
 const char *key="compile-test-only-no-device-key";
 const char *server="0123456789abcdef";
 const char *client="00112233445566778899aabbccddeeff";
 const char *nonce="ffeeddccbbaa99887766554433221100";
 char signature[65],changed[65];
 assert(tm_fire_link::reply_mac(key,server,client,nonce,200,"{\"ack\":true}",signature));
 assert(tm_fire_link::reply_mac(key,server,client,nonce,202,"{\"ack\":true}",changed));
 assert(!tm_buttons::equal(signature,changed,64));
 assert(tm_fire_link::reply_mac(key,server,client,nonce,200,"{\"ack\":false}",changed));
 assert(!tm_buttons::equal(signature,changed,64));
 assert(tm_fire_link::reply_mac(key,"abcdef0123456789",client,nonce,200,"{\"ack\":true}",changed));
 assert(!tm_buttons::equal(signature,changed,64));
 assert(tm_fire_link::reply_mac(key,server,"ffeeddccbbaa99887766554433221100",nonce,200,"{\"ack\":true}",changed));
 assert(!tm_buttons::equal(signature,changed,64));
 assert(!tm_fire_link::reply_mac(key,server,client,"invalid",200,"{}",changed));
 tm_gateway::NonceRegistry registry;tm_gateway::Entry *entry=nullptr;
 unsigned transmissions=0;
 // One model radio execution for a mixture of HTTP/WS retries, running and
 // cached responses. The actual SAME registry is used by both transport paths.
 for(unsigned action=0;action<100;++action) {
   char id[33];snprintf(id,sizeof(id),"%032x",action+1);
   const uint32_t now=action*1000+100;
   assert(registry.issue(id,now));
   const bool pair=action%2;
   auto verdict=registry.admit(id,pair,'A',pair?'G':0,pair?200:0,false,now,&entry);
   assert(verdict==tm_gateway::Admission::Start);++transmissions;
   assert(registry.admit(id,pair,'A',pair?'G':0,pair?200:0,true,now+20,&entry)==tm_gateway::Admission::Busy);
   assert(registry.admit(id,false,'B',0,0,true,now+30,&entry)==tm_gateway::Admission::Conflict);
   registry.finish(entry,action%3?200:202,"{}",now+100);
   assert(registry.admit(id,pair,'A',pair?'G':0,pair?200:0,false,now+200,&entry)==tm_gateway::Admission::Cached);
 }
 assert(transmissions==100);
 tm_fire_link::Samples samples;
 for(unsigned i=1;i<=100;++i)samples.add(i);
 uint32_t min,median,p95,max;samples.summary(min,median,p95,max);
 assert(samples.count==50&&min==51&&median==75&&p95==98&&max==100);
 samples.clear();samples.summary(min,median,p95,max);
 assert(!samples.count&&!min&&!median&&!p95&&!max);
 std::puts("PASS: actual HMAC binds response/session/action; 100 modeled retries never repeat admission; bounded 50-shot statistics");
}
"""
if __name__=="__main__":
    with tempfile.TemporaryDirectory(prefix="tm-fire-link-") as temp:
        path=Path(temp);(path/"mbedtls").mkdir()
        (path/"mbedtls/md.h").write_text(MD);(path/"test.cpp").write_text(CPP)
        flags=shlex.split(subprocess.check_output(["pkg-config","--cflags","--libs","openssl"],text=True))
        subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-I",temp,"-I",str(COMMON),"-I",str(GATEWAY),str(path/"test.cpp"),"-o",str(path/"test"),*flags],check=True)
        subprocess.run([str(path/"test")],check=True)
