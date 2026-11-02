#!/usr/bin/env python3
"""Real crypto: remote-only key split, atomic identity migration, replay domains."""
from pathlib import Path
import shlex
import subprocess
import tempfile

REMOTE = Path(__file__).resolve().parents[1]
COMMON = REMOTE.parent / "lora-common"
SOURCE = r'''
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#define TM_ALLOW_INSECURE_BENCH_KEY 1
#ifdef SPLIT
#define TM_REMOTE_KEY_BYTES {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}
#endif
#include "trapmaster_protocol.h"
#include "remote_counter_store.h"
#include "remote_identity.h"
struct Store {
    std::string ns="remote";
    std::map<std::string,std::map<std::string,std::vector<uint8_t>>> data;
    std::map<std::string,uint32_t> legacy;
    bool fail=false;
    bool begin(const char *name,bool){ns=name;return true;}
    bool isKey(const char *key){return data[ns].count(key);}
    size_t getBytesLength(const char *key){return data[ns].count(key)?data[ns][key].size():0;}
    size_t getBytes(const char *key,void *out,size_t cap){
        auto &bytes=data[ns][key];if(bytes.size()>cap)return 0;
        memcpy(out,bytes.data(),bytes.size());return bytes.size();
    }
    size_t putBytes(const char *key,const void *value,size_t n){
        if(fail)return 0;
        auto *p=(const uint8_t*)value;data[ns][key]={p,p+n};return n;
    }
    uint8_t getUChar(const char *key,uint8_t fallback){return legacy.count(key)?legacy[key]:fallback;}
    uint32_t getULong(const char *key,uint32_t fallback){return legacy.count(key)?legacy[key]:fallback;}
};
bool decodeWith(const uint8_t *frame,size_t bodyLen,const uint8_t *key) {
    uint8_t plain[32];
    mbedtls_gcm_context ctx;mbedtls_gcm_init(&ctx);
    assert(!mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,key,128));
    int rc=mbedtls_gcm_auth_decrypt(&ctx,bodyLen,frame,12,nullptr,0,
        frame+12+bodyLen,16,frame+12,plain);
    mbedtls_gcm_free(&ctx);return rc==0;
}
void legacyFrame(uint8_t sender,uint8_t cmd,uint32_t counter,uint8_t *frame) {
    uint8_t plain[7]={1,sender,cmd,0,0,0,0};
    tm_protocol::write_u32_le(plain+3,counter);
    tm_protocol::make_nonce(counter,cmd,frame,tm_protocol::valid_remote(sender)?sender:0);
    mbedtls_gcm_context ctx;mbedtls_gcm_init(&ctx);
    assert(!mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,tm_protocol::SHARED_KEY,128));
    assert(!mbedtls_gcm_crypt_and_tag(&ctx,MBEDTLS_GCM_ENCRYPT,7,frame,12,nullptr,0,plain,frame+12,16,frame+19));
    mbedtls_gcm_free(&ctx);
}
int main(){
    uint8_t frame[tm_protocol::TEST_FRAME_LEN],legacy[tm_protocol::FRAME_LEN];
    tm_protocol::DecodedPacket packet{};
    assert(tm_protocol::encrypt('A',tm_protocol::CMD_FIRE,42,frame));
    legacyFrame('A',tm_protocol::CMD_FIRE,42,legacy);
    assert(!memcmp(frame,legacy,sizeof(legacy))); // EXACT old relay wire bytes
    assert(decodeWith(frame,7,tm_protocol::SHARED_KEY));
    assert(tm_protocol::encrypt('2',0x11,42,frame));
    assert(decodeWith(frame,7,tm_protocol::REMOTE_KEY));
    assert(tm_protocol::decrypt(frame,tm_protocol::FRAME_LEN,&packet)&&packet.machine=='2');
    legacyFrame('2',0x11,42,legacy);
#ifdef SPLIT
    assert(!decodeWith(frame,7,tm_protocol::SHARED_KEY));
    assert(!tm_protocol::decrypt(legacy,sizeof(legacy),&packet)); // no old-key fallback
#else
    assert(!memcmp(frame,legacy,sizeof(legacy)));
#endif
    uint8_t target[6]={1,2,3,4,5,6};
    assert(tm_protocol::encrypt_test('2','B',43,target,frame));
    assert(decodeWith(frame,13,tm_protocol::REMOTE_KEY));
    assert(tm_protocol::encrypt('2',tm_protocol::CMD_BTN_ACK,42,frame));
    assert(decodeWith(frame,7,tm_protocol::REMOTE_KEY));
    // The gateway preserves previous AES-domain replay ledgers.
    Store s;tm_remote_counters::Ledger<Store> before(s);
    assert(before.begin(tm_protocol::SHARED_KEY)&&before.save(0,900));
    auto oldNs=s.ns;
    tm_remote_counters::Ledger<Store> after(s);
    assert(after.begin(tm_protocol::REMOTE_KEY));
#ifdef SPLIT
    assert(s.ns!=oldNs&&after.counter(0)==0&&after.save(0,12));
#else
    assert(s.ns==oldNs&&after.counter(0)==900&&after.save(0,912));
#endif
    const auto saved=after.counter(0);
    s.fail=true;assert(!after.save(0,saved+1)&&after.counter(0)==saved);s.fail=false;
    tm_remote_counters::Ledger<Store> reboot(s);
    assert(reboot.begin(tm_protocol::REMOTE_KEY)&&reboot.counter(0)==saved);
    tm_remote_counters::Ledger<Store> original(s);
#ifdef SPLIT
    assert(original.begin(tm_protocol::SHARED_KEY)&&original.counter(0)==900);
#endif
    auto &raw=s.data[s.ns]["state"];
    auto tampered=raw;tampered[4]^=1;raw=tampered;
    assert(!original.begin(tm_protocol::SHARED_KEY)); // full digest binds namespace
    s.data[s.ns]["state"].clear();
    assert(!original.begin(tm_protocol::SHARED_KEY)); // unreadable != absent
    // Existing physical board: legacy sender=1, durable counter unchanged.
    Store id;id.legacy["sender"]='1';id.legacy["counter"]=12345;
    assert(tm_remote_identity::bind(id,'2')==tm_remote_identity::ID_LOCKED);
    id.fail=true;
#ifdef SPLIT
    assert(tm_remote_identity::bind(id,'2','1')==tm_remote_identity::STORAGE_ERROR);
    assert(!id.isKey("identity_v2")&&id.legacy["sender"]=='1');
    id.fail=false;
    assert(tm_remote_identity::bind(id,'2','1')==tm_remote_identity::MIGRATED);
    assert(tm_remote_identity::bind(id,'2','1')==tm_remote_identity::READY);
    assert(tm_remote_identity::bind(id,'1','2')==tm_remote_identity::KEY_NOT_ROTATED);
#else
    id.fail=false;
    assert(tm_remote_identity::bind(id,'2','1')==tm_remote_identity::KEY_NOT_ROTATED);
    assert(tm_remote_identity::bind(id,'1')==tm_remote_identity::READY);
#endif
    assert(id.legacy["counter"]==12345&&id.legacy["sender"]=='1');
    id.data[id.ns]["identity_v2"].clear();
    assert(tm_remote_identity::bind(id,'2','1')==tm_remote_identity::INVALID_BINDING);
    puts("PASS relay byte compatibility, remote key separation, no legacy fallback, atomic identity migration/counter retention, durable per-key replay");
}
'''
if __name__ == "__main__":
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "mbedcrypto"], text=True))
    with tempfile.TemporaryDirectory(prefix="tm-reprovision-") as directory:
        src = Path(directory) / "test.cpp"
        src.write_text(SOURCE)
        for split in (False, True):
            binary = Path(directory) / ("split" if split else "legacy")
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            *(["-DSPLIT=1"] if split else []),
                            "-I", str(REMOTE), "-I", str(COMMON), str(src), *flags,
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
        bad = Path(directory) / "bad.cpp"
        bad.write_text('#define TM_ALLOW_INSECURE_BENCH_KEY 1\n'
                       '#define TM_REMOTE_KEY_BYTES {0x54,0x4d,0x2d,0x44,0x45,0x56,0x2d,0x4b,0x45,0x59,0x2d,0x43,0x48,0x41,0x4e,0x47}\n'
                       '#include "trapmaster_protocol.h"\n')
        rejected = subprocess.run(["g++", "-std=c++17", "-I", str(COMMON),
                                   "-c", str(bad), *flags, "-o", str(Path(directory) / "bad.o")],
                                  text=True, capture_output=True)
        assert rejected.returncode and "Remote key must differ" in rejected.stderr
        print("PASS build rejects remote key equal to relay key")
