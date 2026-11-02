#!/usr/bin/env python3
"""Real PBKDF2 policy, fake durable store/random source; never uses real PINs."""
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "phone_policy.h"
#include <cassert>
#include <cstdio>
struct Store {
    tm_phone::Credential c{};
    bool present=false,fail=false,corrupt=false;
    uint32_t failures=0;
    bool load(tm_phone::Credential &out,bool &p,uint32_t &f) {
        out=c;p=present;f=failures;return !corrupt;
    }
    bool saveCredential(const tm_phone::Credential &next) {
        if(fail)return false;c=next;present=true;return true;
    }
    bool saveFailures(uint32_t f){if(fail)return false;failures=f;return true;}
};
struct Random {uint8_t n=0;void fill(uint8_t *p,size_t len){while(len--)*p++=++n;}};
int main(){
    assert(!tm_phone::shutdownReason(119999,0,0));
    assert(!strcmp(tm_phone::shutdownReason(120000,0,0),"2 min no station"));
    assert(!tm_phone::shutdownReason(599999,0,599999)); // connected station
    assert(!strcmp(tm_phone::shutdownReason(600000,0,600000),"10 min inactivity"));
    assert(!tm_phone::shutdownReason(15,UINT32_MAX-20,UINT32_MAX-20)); // timer rollover
    Store s;Random r;tm_phone::Auth<Store,Random> a(s,r);
    assert(a.begin(0)&&!a.provisioned&&!a.session);
    assert(!a.set("123","123")&&!a.set("12345","12345")&&!a.set("12a4","12a4"));
    assert(!a.set("1234","1235")&&a.set("1234","1234"));
    assert(!a.session&&memcmp(a.credential.hash,"1234",4));
    uint8_t salt[16];memcpy(salt,a.credential.salt,16);
    assert(a.attempt("1234",0));a.login(0);
    char old[33];memcpy(old,a.token,33);
    assert(a.valid(old,599999)&&!a.valid(old,600000));
    a.login(600001);assert(!a.valid(old,600001)&&a.valid(a.token,600001));
    assert(!a.attempt("9999",600002));assert(!a.attempt("9999",600003));
    assert(!a.attempt("9999",600004)&&a.wait(600004)==30000);
    assert(!a.attempt("1234",600005)&&a.failures==3);
    // Reboot keeps the global failures and reapplies full lockout.
    tm_phone::Auth<Store,Random> reboot(s,r);
    assert(reboot.begin(0)&&reboot.failures==3&&reboot.wait(0)==30000);
    assert(!reboot.attempt("9999",30000)&&reboot.wait(30000)==60000);
    assert(tm_phone::backoff(8)==900000&&tm_phone::backoff(999)==900000);
    assert(reboot.attempt("1234",90000)&&reboot.failures==0&&s.failures==0);
    reboot.login(90000);memcpy(old,reboot.token,33);
    assert(reboot.set("5678","5678")&&!reboot.valid(old,90000));
    assert(memcmp(salt,reboot.credential.salt,16));
    assert(!reboot.attempt("1234",90001)&&reboot.attempt("5678",90002));
    s.fail=true;assert(!reboot.attempt("5678",90003)&&!reboot.healthy);
    s.fail=false;s.corrupt=true;assert(!a.begin(0)&&!a.valid(old,0));
    puts("PASS real PBKDF2, strict PINs, random salt/token rotation, idle expiry, persistent global backoff, NVS fail-closed");
}
'''
if __name__ == "__main__":
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "mbedcrypto"], text=True))
    with tempfile.TemporaryDirectory(prefix="tm-phone-") as directory:
        src = Path(directory) / "test.cpp"
        src.write_text(SOURCE)
        binary = Path(directory) / "test"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-Wno-misleading-indentation", "-I", str(ROOT), str(src), *flags, "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
