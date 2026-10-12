#!/usr/bin/env python3
"""Real AES-GCM/HMAC plus pure event/input logic; no fake crypto primitive."""
from pathlib import Path
import shlex
import subprocess
import tempfile

COMMON = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include <cassert>
#include <cstdio>
#include <set>
#include <string>
#define TM_ALLOW_INSECURE_BENCH_KEY
#include "trapmaster_protocol.h"
#include "button_events.h"
int main() {
    std::set<std::string> nonces;
    for(uint8_t sender='1';sender<='8';++sender) {
        for(uint8_t command=0x11;command<=0x16;++command) {
            uint8_t frame[tm_protocol::FRAME_LEN],ack[tm_protocol::FRAME_LEN];
            assert(tm_protocol::encrypt(sender,command,123,frame));
            assert(nonces.insert(std::string((char *)frame,12)).second);
            tm_protocol::DecodedPacket p{};
            assert(tm_protocol::decrypt(frame,sizeof(frame),&p));
            assert(p.machine==sender&&p.counter==123&&p.command==command);
            frame[15]^=1;assert(!tm_protocol::decrypt(frame,sizeof(frame),&p));
            assert(tm_protocol::encrypt(sender,tm_protocol::CMD_BTN_ACK,123,ack));
            if(command==0x11) assert(nonces.insert(std::string((char *)ack,12)).second);
        }
    }
    assert(!tm_protocol::valid_address('0',0x11));
    assert(!tm_protocol::valid_address('1',tm_protocol::CMD_FIRE));
    uint8_t relay[tm_protocol::FRAME_LEN],legacy[12];
    assert(tm_protocol::encrypt('A',tm_protocol::CMD_FIRE,123,relay));
    tm_protocol::make_nonce(123,tm_protocol::CMD_FIRE,legacy);
    assert(!memcmp(relay,legacy,12)&&relay[9]==0&&relay[10]==0&&relay[11]==0);
    tm_protocol::DecodedPacket decoded{};
    assert(tm_protocol::decrypt(relay,sizeof(relay),&decoded));

    const char *key="synthetic-private-auth-key";
    const char *sn="0011223344556677",*cn="00112233445566778899aabbccddeeff";
    const char *sn2="0011223344556678",*cn2="00112233445566778899aabbccddee00";
    char a[65],b[65];assert(tm_buttons::auth_mac(key,sn,cn,57,999,a));
    // Compare the canonical text with the actual SHA256 primitive.
    assert(tm_buttons::mac(key,"ws|0011223344556677|57|999|00112233445566778899aabbccddeeff",b));
    assert(tm_buttons::equal(a,b,64));
    assert(tm_buttons::auth_mac(key,sn,cn2,57,999,b));assert(!tm_buttons::equal(a,b,64));
    assert(tm_buttons::auth_mac(key,sn2,cn,57,999,b));assert(!tm_buttons::equal(a,b,64));
    assert(tm_buttons::auth_mac(key,sn,cn,58,999,b));assert(!tm_buttons::equal(a,b,64));
    tm_buttons::Event event{57,1,1,0};
    assert(tm_buttons::event_mac(key,sn,cn,999,event,120,a));
    assert(tm_buttons::mac(key,"evt|999|57|1|1|120|00112233445566778899aabbccddeeff|0011223344556677",b,8));
    assert(tm_buttons::equal(a,b,16));
    assert(tm_buttons::event_mac(key,sn,cn,999,event,121,b));assert(!tm_buttons::equal(a,b,16));
    assert(tm_buttons::event_mac(key,sn,cn2,999,event,120,b));assert(!tm_buttons::equal(a,b,16));
    assert(tm_buttons::event_mac(key,sn2,cn,999,event,120,b));assert(!tm_buttons::equal(a,b,16));
    event.src=2;assert(tm_buttons::event_mac(key,sn,cn,999,event,120,b));assert(!tm_buttons::equal(a,b,16));
    event.src=1;event.btn=6;assert(tm_buttons::event_mac(key,sn,cn,999,event,120,b));assert(!tm_buttons::equal(a,b,16));
    assert(!tm_buttons::equal(a,"000000000000000z",16));
    assert(!tm_buttons::auth_mac(key,"too short",cn,0,0,a));

    tm_buttons::Ring ring;
    for(unsigned i=1;i<=40;++i) assert(ring.append(i%8+1,i%6+1,i*10));
    assert(ring.next==40&&ring.count==32&&ring.oldest()==9);
    assert(ring.gap(7)&&!ring.gap(8)&&!ring.get(8)&&ring.get(9));
    for(unsigned i=9;i<=40;++i) assert(ring.get(i)->id==i&&ring.get(i)->t_ms==i*10);
    ring.next=UINT32_MAX;assert(!ring.append(1,1,0));
    tm_buttons::Cursor cursor{999,56};event={57,1,1,0};
    assert(cursor.check(999,event,3000)==tm_buttons::Cursor::ACCEPT);
    assert(cursor.check(999,event,3001)==tm_buttons::Cursor::STALE);
    assert(cursor.check(1000,event,0)==tm_buttons::Cursor::INVALID);
    // Failed queue admission leaves the cursor unchanged; retry remains valid.
    assert(cursor.after==56);assert(cursor.check(999,event,0)==tm_buttons::Cursor::ACCEPT);
    cursor.after=57;assert(cursor.check(999,event,0)==tm_buttons::Cursor::DUPLICATE);
    cursor={1000,40};assert(cursor.check(1000,event,0)==tm_buttons::Cursor::ACCEPT);

    tm_buttons::Input input;
    input.init(true,0);assert(!input.sample(true,500,30,true)); // held at boot
    assert(!input.sample(false,501,30,true));
    assert(!input.sample(false,531,30,true));
    assert(!input.sample(true,532,30,true));assert(!input.sample(true,561,30,true));
    assert(input.sample(true,562,30,true));assert(!input.sample(true,900,30,true));
    input.sample(false,901,30,true);input.sample(false,931,30,true);
    input.sample(true,940,30,false);assert(!input.sample(true,1000,30,false));
    input.released=false;assert(!input.sample(true,1100,30,true)); // arm while held
    input.sample(false,1101,30,true);input.sample(false,1131,30,true);
    input.sample(true,1132,30,true);assert(input.sample(true,1162,30,true));
    input.sample(false,1163,30,true);input.sample(false,1193,30,true);
    input.sample(true,1194,30,true);assert(!input.sample(true,1224,30,true)); // cooldown
    puts("PASS real AES/HMAC: sender/ACK nonce separation, legacy relay, tamper/session/age binding");
    puts("PASS ring wrap/resume/gap, boot/cursor/staleness, debounce/held/rearm/cooldown");
}
'''

if __name__ == "__main__":
    flags = shlex.split(subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "mbedcrypto"], text=True))
    with tempfile.TemporaryDirectory(prefix="tm-buttons-") as directory:
        src = Path(directory) / "test.cpp"
        src.write_text(SOURCE)
        executable = Path(directory) / "test"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(COMMON), str(src), *flags, "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
