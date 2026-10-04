#!/usr/bin/env python3
"""Exercise actual gateway handlers/radio orchestration without hardware.

Only the SDK hash primitive, socket, radio, clock and NVS are mocked. The real
HMAC payload construction, hex validation, nonce registry and fire handlers run.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


MD = r"""
#pragma once
#include <cassert>
#include <cstdint>
#include <vector>
static std::vector<uint8_t> signed_payload;
constexpr int MBEDTLS_MD_SHA256 = 1;
struct mbedtls_md_info_t {};
inline const mbedtls_md_info_t *mbedtls_md_info_from_type(int type) {
    assert(type == MBEDTLS_MD_SHA256);
    static mbedtls_md_info_t info; return &info;
}
inline int mbedtls_md_hmac(const mbedtls_md_info_t *, const uint8_t *key,
                           size_t key_len, const uint8_t *bytes, size_t size, uint8_t *out) {
    signed_payload.assign(bytes, bytes + size);
    // Deterministic tamper-sensitive host stand-in, NOT production cryptography.
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < key_len; ++i) hash = (hash ^ key[i]) * 16777619u;
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    for (unsigned i = 0; i < 32; ++i) { hash = hash * 1664525u + 1013904223u; out[i] = hash >> 24; }
    return 0;
}
"""

STUBS = r"""
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include "trapmaster_auth.h"
#include "nonce_registry.h"
struct String : std::string {
    using std::string::string;
    String(const std::string &s) : std::string(s) {}
    String(char c) : std::string(1, c) {}
    String(uint32_t n) : std::string(std::to_string(n)) {}
};
static uint32_t now = 0, random_id = 0;
uint32_t millis() { return now; }
void delay(uint32_t ms) { now += ms; }
void esp_fill_random(void *out, size_t size) {
    memset(out, 0, size); ((uint8_t *)out)[size - 1] = (uint8_t)++random_id;
}
struct WiFiClient {
    std::shared_ptr<std::string> output = std::make_shared<std::string>();
    void printf(const char *format, ...) {
        char body[1024]; va_list args; va_start(args, format);
        vsnprintf(body, sizeof(body), format, args); va_end(args); *output += body;
    }
    void stop() {}
};
struct Server {
    std::map<std::string, String> args;
    String auth, body;
    int status = 0;
    WiFiClient connection;
    std::function<void()> next;
    bool hasArg(const char *name) { return args.count(name); }
    String arg(const char *name) { return args[name]; }
    bool hasHeader(const char *) { return !auth.empty(); }
    String header(const char *) { return auth; }
    void sendHeader(const char *, const char *) {}
    void send(int code, const char *, const String &text) { status = code; body = text; }
    WiFiClient detachClient() { auto client = connection; connection = {}; return client; }
    void handleClient() { auto cb = next; next = {}; if (cb) cb(); }
} server;
struct { struct IP { String toString() { return "192.0.2.1"; } };
    IP localIP() { return {}; } } WiFi;
static bool storage_ok = true, tx_ok = true, ack_ok = true;
static uint32_t stored_counter = 0;
static std::vector<std::string> operations;
static std::vector<char> transmissions;
struct { size_t putULong(const char *key, uint32_t counter) {
    assert(std::string(key) == "counter"); // No HTTP request/sequence writes!
    operations.push_back("persist");
    if (!storage_ok) return 0;
    stored_counter = counter; return sizeof(counter);
} } preferences;
namespace tm_protocol {
constexpr size_t FRAME_LEN = 35;
constexpr uint8_t CMD_FIRE = 1;
bool valid_machine(char machine) { return machine >= 'A' && machine <= 'H'; }
bool encrypt(char machine, uint8_t command, uint32_t counter, uint8_t *frame) {
    assert(command == CMD_FIRE && stored_counter == counter);
    memset(frame, 0, FRAME_LEN); frame[0] = machine; return true;
}
}
struct RadioMock {
    bool receiving = false;
    void Send(uint8_t *frame, size_t size) {
        assert(size == tm_protocol::FRAME_LEN && operations.back() == "persist");
        operations.push_back("transmit"); transmissions.push_back(frame[0]); receiving = false;
    }
    void Rx(uint32_t) { receiving = true; }
    void IrqProcess();
} Radio;
constexpr uint32_t TX_TIMEOUT_MS = 500, ACK_TIMEOUT_MS = 3000, PAIR_MAX_DELAY_MS = 10000;
static constexpr char GATEWAY_AUTH_KEY[] = "synthetic-gateway-auth-key";
"""

AFTER_GLOBALS = r"""
static void renderStatus() {}
void RadioMock::IrqProcess() {
    if (!receiving) { if (tx_ok) txDone = true; }
    else if (ack_ok) ackReceived = true;
}
"""

MAIN = r"""
void reset() {
    now = random_id = nextCounter = stored_counter = 0;
    storage_ok = tx_ok = ack_ok = true;
    nonces = {}; pendingFire = nullptr; pendingClient = {}; server = {};
    txDone = txTimeout = ackReceived = radioBusy = false;
    lastResult = ""; Radio.receiving = false;
    transmissions.clear(); operations.clear();
}
std::string issue() {
    server.status = 0; handleNonce(); assert(server.status == 200);
    auto start = server.body.find("\"nonce\":\"") + 9;
    std::string nonce = server.body.substr(start, 32);
    uint8_t bytes[16]; assert(tm_auth::nonce_from_hex(nonce.c_str(), bytes));
    return nonce;
}
void sign(const std::string &nonce, char first = 'A', char second = 0, uint32_t delay_ms = 0) {
    uint8_t bytes[16], mac[32]; char hex[65];
    assert(tm_auth::nonce_from_hex(nonce.c_str(), bytes));
    server.status = 0; server.body = ""; server.args.clear();
    server.args["nonce"] = nonce;
    if (second) {
        server.args["first"] = String(first); server.args["second"] = String(second);
        server.args["delayMs"] = String(delay_ms);
        assert(tm_auth::make_pair_request_mac((const uint8_t *)GATEWAY_AUTH_KEY,
            sizeof(GATEWAY_AUTH_KEY)-1, first, second, delay_ms, bytes, mac));
    } else {
        server.args["machine"] = String(first);
        assert(tm_auth::make_request_mac((const uint8_t *)GATEWAY_AUTH_KEY,
            sizeof(GATEWAY_AUTH_KEY)-1, first, bytes, mac));
    }
    tm_auth::mac_to_hex(mac, hex); server.auth = hex;
}
void assert_cached(const std::string &nonce, int code, const std::string &body,
                   char first = 'A', char second = 0, uint32_t delay_ms = 0) {
    auto count = transmissions.size(); sign(nonce, first, second, delay_ms);
    if (second) handleFirePair(); else handleFire();
    assert(server.status == code && server.body == body && !pendingFire);
    assert(transmissions.size() == count);
}
int main() {
    uint8_t raw[16], mac[32]; char encoded[33];
    assert(tm_auth::nonce_from_hex("00112233445566778899AABBCCDDEEFF", raw));
    tm_auth::nonce_to_hex(raw, encoded);
    assert(std::string(encoded) == "00112233445566778899aabbccddeeff");
    assert(!tm_auth::nonce_from_hex("0011", raw));
    assert(!tm_auth::nonce_from_hex("g0112233445566778899aabbccddeeff0", raw));
    assert(tm_auth::make_request_mac((const uint8_t *)GATEWAY_AUTH_KEY,
        sizeof(GATEWAY_AUTH_KEY)-1, 'H', raw, mac));
    std::vector<uint8_t> single = {'T','M',1,'H'}; single.insert(single.end(), raw, raw+16);
    assert(signed_payload == single);
    assert(tm_auth::make_pair_request_mac((const uint8_t *)GATEWAY_AUTH_KEY,
        sizeof(GATEWAY_AUTH_KEY)-1, 'D','F',1000,raw,mac));
    std::vector<uint8_t> pair = {'T','M',1,'P','D','F',0xe8,3,0,0};
    pair.insert(pair.end(), raw, raw+16); pair.insert(pair.end(), {'D','O','U'});
    assert(signed_payload == pair);
    assert(tm_auth::make_health_mac((const uint8_t *)GATEWAY_AUTH_KEY,
        sizeof(GATEWAY_AUTH_KEY)-1, mac));
    assert((signed_payload == std::vector<uint8_t>{'T','M',1,'C','H','E','C','K'}));

    reset(); auto nonce = issue(); handleNonce(); assert(server.status == 429);
    for (unsigned i = 1; i < 8; ++i) { now += 20; issue(); }
    now += 20; handleNonce(); assert(server.status == 429);
    now += 10000; issue(); // expired unused slots reclaimed

    reset(); nonce = issue(); sign(nonce);
    server.auth[0] = server.auth[0] == '0' ? '1' : '0';
    handleFire(); assert(server.status == 401 && transmissions.empty());
    sign(nonce); server.args["machine"] = "B";
    handleFire(); assert(server.status == 401 && !pendingFire);
    sign(nonce); handleFire(); assert(pendingFire && server.status == 0);
    auto response = pendingClient.output;
    // A second terminal and a duplicate original arrive during the actual TX wait.
    now += 20; auto second_nonce = issue();
    server.next = [=]() {
        sign(second_nonce, 'B'); handleFire(); assert(server.status == 409);
        sign(nonce); handleFire(); assert(server.status == 409);
    };
    runPendingFire(); assert(!pendingFire && transmissions == std::vector<char>{'A'});
    assert(response->find("HTTP/1.1 200") == 0);
    auto body = response->substr(response->find("\r\n\r\n") + 4);
    assert_cached(nonce, 200, body);
    assert_cached(second_nonce, 409, "{\"ok\":false,\"error\":\"busy\"}", 'B');
    sign(nonce, 'B'); handleFire(); assert(server.status == 409);
    now += 30000; sign(nonce); handleFire(); assert(server.status == 401);
    assert(transmissions.size() == 1);

    reset(); nonce = issue(); now += 10000; sign(nonce);
    handleFire(); assert(server.status == 401 && transmissions.empty());
    reset(); nonce = issue(); sign(nonce); handleFire();
    nonces = {}; pendingFire = nullptr; // simulated reboot, including loss mid-fire
    sign(nonce); handleFire(); assert(server.status == 401 && transmissions.empty());

    for (bool fail_storage : {true, false}) {
        reset(); nonce = issue(); sign(nonce); handleFire();
        storage_ok = !fail_storage; tx_ok = fail_storage;
        response = pendingClient.output; runPendingFire();
        body = response->substr(response->find("\r\n\r\n") + 4);
        assert_cached(nonce, fail_storage ? 503 : 502, body);
        assert(transmissions.size() == (fail_storage ? 0u : 1u));
    }
    reset(); ack_ok = false; nonce = issue(); sign(nonce); handleFire();
    response = pendingClient.output; runPendingFire();
    body = response->substr(response->find("\r\n\r\n") + 4);
    assert_cached(nonce, 202, body);

    for (uint32_t timing : {0u, 100u}) {
        reset(); nonce = issue(); sign(nonce, 'D', 'F', timing);
        // An unsigned timing change is rejected without consuming the nonce.
        server.args["delayMs"] = String(timing + 1);
        handleFirePair(); assert(server.status == 401 && !pendingFire);
        sign(nonce, 'D', 'F', timing); handleFirePair(); assert(pendingFire);
        response = pendingClient.output;
        bool busy_seen = false;
        server.next = [&]() {
            sign(nonce, 'D', 'F', timing); handleFirePair();
            assert(server.status == 409); busy_seen = true;
            // Schedule another terminal specifically during the pair delay.
            if (timing) server.next = [&]() {
                now += 20; auto other = issue(); sign(other, 'H');
                handleFire(); assert(server.status == 409);
            };
        };
        runPendingFire();
        assert(busy_seen && (transmissions == std::vector<char>{'D','F'}));
        body = response->substr(response->find("\r\n\r\n") + 4);
        assert(body.find(timing ? "\"firstAckWaited\":true" : "\"firstAckWaited\":false") != std::string::npos);
        assert_cached(nonce, 200, body, 'D', 'F', timing);
        sign(nonce, 'D', 'F', timing+1); handleFirePair(); assert(server.status == 409);
    }
    reset(); ack_ok = false; nonce = issue(); sign(nonce, 'D','F',5); handleFirePair();
    response = pendingClient.output; runPendingFire();
    body = response->substr(response->find("\r\n\r\n") + 4);
    assert(body.find("\"secondSent\":false") != std::string::npos);
    assert_cached(nonce, 202, body, 'D','F',5);
    assert(transmissions == std::vector<char>{'D'});

    reset(); nonce = issue(); sign(nonce, 'D','F',5); handleFirePair();
    response = pendingClient.output;
    server.next = [] { storage_ok = false; }; // first already persisted, second must not TX
    runPendingFire(); body = response->substr(response->find("\r\n\r\n") + 4);
    assert_cached(nonce, 503, body, 'D','F',5);
    assert(transmissions == std::vector<char>{'D'});

    reset(); nonce = issue(); sign(nonce);
    tm_auth::make_health_mac((const uint8_t *)GATEWAY_AUTH_KEY, sizeof(GATEWAY_AUTH_KEY)-1, mac);
    char mac_hex[65]; tm_auth::mac_to_hex(mac, mac_hex); server.auth = mac_hex;
    handleHealth(); assert(server.status == 200 && server.body.find("\"fireAuth\":\"nonce-v1\"") != std::string::npos);
    assert(nonces.find(nonce.c_str())->state == tm_gateway::State::Issued);
    handleFire(); assert(server.status == 401); // health signature cannot fire

    // Never evict a cached result when the bounded registry fills.
    reset(); for (unsigned i = 0; i < tm_gateway::NONCE_SLOTS; ++i) {
        now += 20; auto item = issue(); sign(item); handleFire(); runPendingFire();
    }
    now += 20; handleNonce(); assert(server.status == 429);
    assert(nonces.find("00000000000000000000000000000001"));

    tm_gateway::NonceRegistry wrap;
    auto start = UINT32_MAX - 10u;
    assert(wrap.issue("00000000000000000000000000000001", start));
    assert(wrap.issue("00000000000000000000000000000002", start + 20u));
    tm_gateway::Entry *entry = nullptr;
    assert(wrap.admit("00000000000000000000000000000001", false,'A',0,0,false,
        start + 9999u, &entry) == tm_gateway::Admission::Start);
    wrap.expire(start + 100000u);
    assert(entry->state == tm_gateway::State::Running); // cannot evict an active fire
    wrap.finish(entry, 502, "failure", start + 100000u);
    wrap.expire(start + 129999u); assert(entry->state == tm_gateway::State::Complete);
    wrap.expire(start + 130000u); assert(entry->state == tm_gateway::State::Empty);
    std::puts("PASS: nonce TTL/throttle/capacity/wrap; signed fields; no replay after reboot/expiry");
    std::puts("PASS: HTTP responsive during TX/pair; concurrent terminals busy; exact cached outcomes; no re-fire");
    std::puts("PASS: storage/TX/ACK/partial pair failures cached; counter persisted before TX; health never fires");
}
"""


if __name__ == "__main__":
    source = (ROOT / "TrapMasterGateway.ino").read_text()
    globals_ = source[source.index("static uint32_t nextCounter"):
                      source.index("static bool networkSettingsSaved")]
    functions = "\n".join(function(source, name) for name in [
        "static bool request_is_authentic", "static bool pair_request_is_authentic",
        "static bool health_request_is_authentic", "static void handleNonce",
        "static void admitFire", "static bool waitForFlag", "static bool sendFire",
        "static void handleFire()", "static bool parse_pair_machine",
        "static bool parse_delay_ms", "static void handleFirePair",
        "static void finishPendingFire", "static void runPendingFire", "static void handleHealth"])
    assert "req_seq" not in source and "persist_request" not in source
    with tempfile.TemporaryDirectory(prefix="tm-nonce-") as directory:
        path = Path(directory)
        (path / "mbedtls").mkdir()
        (path / "mbedtls/md.h").write_text(MD)
        (path / "test.cpp").write_text(STUBS + globals_ + AFTER_GLOBALS + functions + MAIN)
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", directory, "-I", str(ROOT),
                        "-I", str(ROOT.parent / "lora-common"),
                        str(path / "test.cpp"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)