#!/usr/bin/env python3
"""Actual gateway idle/status diagnostics with a host clock/WiFi/HTTP shim."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("nonce_fixture", Path(__file__).with_name("test_nonce.py"))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)

STUBS = r"""
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <string>
#include <vector>
struct String : std::string {
    using std::string::string;
    String(const std::string &s) : std::string(s) {}
    String(uint32_t n) : std::string(std::to_string(n)) {}
    String(int n) : std::string(std::to_string(n)) {}
};
constexpr int WL_CONNECTED = 3;
static uint32_t clock_ms = 0;
uint32_t millis() { return clock_ms; }
struct {
    int status() { return WL_CONNECTED; }
    int RSSI() { return -58; }
    struct IP { String toString() { return "192.0.2.1"; } };
    IP localIP() { return {}; }
} WiFi;
struct {
    std::vector<std::string> lines;
    void printf(const char *format, ...) {
        char text[256]; va_list args; va_start(args, format);
        vsnprintf(text, sizeof(text), format, args); va_end(args); lines.emplace_back(text);
    }
} Serial;
struct {
    String body; int status = 0;
    void send(int code, const char *, const String &value) { status = code; body = value; }
} server;
static void *pendingFire = nullptr;
static bool staticAddressEnabled = false, lastAck = false;
static char staticGatewayText[16] = {};
static String lastMachine = "-", lastResult = "Ready";
"""

MAIN = r"""
int main() {
    noteHttpRequest(); assert(httpRequestsSinceBoot == 1);
    clock_ms = 59999; logHttpIdle(); assert(Serial.lines.empty());
    clock_ms = 60000; logHttpIdle(); assert(Serial.lines.size() == 1);
    assert(Serial.lines[0].find("alive uptimeMs=60000 noRequestMs=60000 requests=1") != std::string::npos);
    assert(Serial.lines[0].find("wifi=connected rssi=-58") != std::string::npos);
    clock_ms = 60001; logHttpIdle(); assert(Serial.lines.size() == 1);
    noteHttpRequest();
    clock_ms = 120000; logHttpIdle(); assert(Serial.lines.size() == 1);
    clock_ms = 120001; logHttpIdle(); assert(Serial.lines.size() == 2);
    handleStatus();
    assert(server.status == 200 && server.body.find("\"rssi\":-58") != std::string::npos);
    assert(server.body.find("\"uptimeMs\":120001") != std::string::npos);
    assert(server.body.find("\"httpRequestsSinceBoot\":2") != std::string::npos);
    clock_ms = 0xfffffff0; noteHttpRequest();
    clock_ms = 32; logHttpIdle(); assert(Serial.lines.size() == 2);
    puts("PASS: request counting; 60-second idle heartbeat throttling/reset and clock wrap; RSSI/uptime status fields");
}
"""

if __name__ == "__main__":
    source = (ROOT / "TrapMasterGateway.ino").read_text()
    globals_ = source[source.index("static uint32_t lastHttpRequestMs"):
                      source.index("static void noteHttpRequest")]
    functions = "\n".join(fixture.function(source, name) for name in
                           ("static void noteHttpRequest", "static void logHttpIdle", "static void handleStatus"))
    setup = fixture.function(source, "void setup()")
    assert setup.index("WiFi.setSleep(false)") > setup.index('wifiManager.autoConnect')
    assert "WiFi.setAutoReconnect(true)" in setup and "server.onNotFound" in setup
    for route in ("/nonce", "/fire", "/fire-pair", "/health", "/status"):
        assert f'server.on("{route}", HTTP_GET, [] {{ noteHttpRequest();' in setup
    with tempfile.TemporaryDirectory(prefix="tm-http-diag-") as directory:
        path = Path(directory)
        (path / "test.cpp").write_text(STUBS + globals_ + functions + MAIN)
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        str(path / "test.cpp"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)