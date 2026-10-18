"""Compile the real chip-ID function and monotonic hourly gate without WiFi."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "main/net/http_sync.cpp").read_text()
start = source.index("static bool terminal_id(")
end = source.index("\nstatic cJSON *config_snapshot_to_json", start)
identity = source[start:end]
assert "ESP_MAC_WIFI_STA" not in identity and "UNKNOWN" not in identity
assert "esp_efuse_mac_get_default" in identity
# Every identity-bearing path must handle failure, before network submission.
assert source.count("if(!terminal_id(") == 6

cpp = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "hourly_log_gate.h"
using esp_err_t=int;
constexpr int ESP_OK=0;
static uint8_t factory[6]={0x10,0x20,0x30,0x40,0x50,0x60};
static int read_error=0,errors=0;
int esp_efuse_mac_get_default(uint8_t *out) {memcpy(out,factory,6);return read_error;}
void set_http_error(const char*,const char*,const char*) {++errors;}
void terminal_log_add(const char*,const char*,const char*) {}
''' + identity + r'''
int main() {
    char first[40],again[40],second[40];
    assert(terminal_id(first,sizeof(first)) && !strcmp(first,"ESP32-P4-102030405060"));
    assert(terminal_id(again,sizeof(again)) && !strcmp(first,again));
    factory[5]=0x61;
    assert(terminal_id(second,sizeof(second)) && strcmp(first,second));
    read_error=-1;strcpy(again,"stale");
    assert(!terminal_id(again,sizeof(again)) && !again[0] && errors==1);
    read_error=0;memset(factory,0,sizeof(factory));
    assert(!terminal_id(again,sizeof(again)) && !again[0]);
    memset(factory,0xff,sizeof(factory));
    assert(!terminal_id(again,sizeof(again)) && !again[0]);
    assert(!terminal_id(again,5) && !again[0]);
    assert(!terminal_id(nullptr,0));
    HourlyLogGate gate;
    assert(gate.admit(0)); // first occurrence, including the first microsecond
    assert(!gate.admit(1));
    assert(!gate.admit(3599999999LL));
    assert(gate.admit(3600000000LL));
    assert(!gate.admit(3600000001LL));
    assert(gate.admit(7200000000LL));
    puts("PASS actual eFuse ID: stable/distinct, invalid/failure closed, no WiFi/shared fallback; hourly monotonic gate");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(cpp)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "main/store"), str(path / "test.cpp"),
                    "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
