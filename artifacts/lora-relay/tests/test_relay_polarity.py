#!/usr/bin/env python3
"""Host regression checks for actual relay handlers, with mocked GPIO/radio/NVS.

Run: python artifacts/lora-relay/tests/test_relay_polarity.py
Requires g++. This checks control flow, not real AES, RF, or electrical behavior.
No private configuration or key files are loaded.
"""

from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source: str, name: str) -> str:
    match = re.search(r"(?:static )?(?:void|bool) " + name + r"\(", source)
    assert match, f"Missing function: {name}"
    opening = source.index("{", match.start())
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


STUBS = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#define HIGH 1
#define LOW 0
#define TM_MACHINE_ID 'A'
#define TM_RELAY_GPIO 4
#define TM_ENABLE_UNENCRYPTED_BENCH_TEST
#define TM_BENCH_INTERLOCK_GPIO 3
static std::vector<std::string> trace;
static int output = LOW;
static int interlock = HIGH;
static bool saveOK = true;
static bool decryptOK = true;
static uint32_t storedCounter = 41;
static uint32_t lastAcceptedCounter = 41;
static volatile bool firePending = false;
static uint32_t pendingCounter = 0;
static char machineId = 'A';
void digitalWrite(int pin, int level) {
    assert(pin == TM_RELAY_GPIO);
    output = level;
    trace.push_back(level == HIGH ? "HIGH" : "LOW");
}
int digitalRead(int pin) { assert(pin == TM_BENCH_INTERLOCK_GPIO); return interlock; }
void delay(unsigned long ms) { assert(ms == 300); trace.push_back("wait"); }
struct {
    void println(const char *) {}
    template <typename... Args> void printf(const char *, Args...) {}
} Serial;
struct {
    size_t putULong(const char *key, uint32_t counter) {
        assert(std::string(key) == "last_counter");
        trace.push_back("save");
        if (!saveOK) return 0;
        storedCounter = counter;
        return sizeof(uint32_t);
    }
} preferences;
struct {
    void Rx(int) {}
    void Sleep() {}
    void Send(uint8_t *, size_t) { trace.push_back("send"); }
} Radio;
namespace tm_protocol {
    constexpr uint8_t CMD_FIRE = 1, CMD_ACK = 2;
    constexpr size_t FRAME_LEN = 32;
    struct DecodedPacket { char machine; uint8_t command; uint32_t counter; };
    static DecodedPacket packet = {'A', CMD_FIRE, 42};
    bool decrypt(uint8_t *, uint16_t, DecodedPacket *decoded) {
        *decoded = packet;
        return decryptOK;
    }
    bool encrypt(char machine, uint8_t command, uint32_t counter, uint8_t *) {
        assert(machine == 'A' && command == CMD_ACK);
        assert(counter == 0 || counter == storedCounter);
        trace.push_back("ACK");
        return true;
    }
}
"""

TESTS = r"""
void reset() {
    trace.clear(); output = LOW; interlock = HIGH; saveOK = decryptOK = true;
    storedCounter = lastAcceptedCounter = 41; firePending = false;
    pendingCounter = 0; machineId = 'A';
    tm_protocol::packet = {'A', tm_protocol::CMD_FIRE, 42};
}
void noFire() {
    assert(output == LOW && !firePending);
    for (const auto &event : trace) assert(event != "HIGH" && event != "ACK");
}
void receive() {
    uint8_t frame[tm_protocol::FRAME_LEN] = {};
    onRxDone(frame, sizeof(frame), -50, 5);
}
int main() {
    reset(); decryptOK = false; receive(); noFire();
    reset(); tm_protocol::packet.machine = 'B'; receive(); noFire();
    reset(); tm_protocol::packet.command = tm_protocol::CMD_ACK; receive(); noFire();
    reset(); tm_protocol::packet.counter = 41; receive(); noFire();

    reset(); receive();
    assert(firePending && output == LOW);
    firePending = false; fireRelayAndAck(pendingCounter);
    assert((trace == std::vector<std::string>{"save","HIGH","wait","LOW","ACK","send"}));
    assert(output == LOW && lastAcceptedCounter == 42 && storedCounter == 42);
    trace.clear(); receive(); noFire();  // replay after acceptance

    reset(); saveOK = false; receive();
    firePending = false; fireRelayAndAck(pendingCounter);
    noFire();
    assert(lastAcceptedCounter == 41 && storedCounter == 41);

    uint8_t bench[] = {'T','B','A'};
    reset(); decryptOK = false; interlock = HIGH;
    onRxDone(bench, sizeof(bench), -50, 5); noFire();
    reset(); decryptOK = false; interlock = LOW;
    onRxDone(bench, sizeof(bench), -50, 5);
    assert(firePending && pendingCounter == 0 && output == LOW);
    firePending = false; fireRelayAndAck(pendingCounter);
    assert((trace == std::vector<std::string>{"HIGH","wait","LOW","ACK","send"}));
    assert(lastAcceptedCounter == 41 && storedCounter == 41 && output == LOW);
    reset(); decryptOK = false; interlock = LOW; bench[2] = 'B';
    onRxDone(bench, sizeof(bench), -50, 5); noFire();

    reset(); output = HIGH; onTxTimeout(); noFire();
    reset(); output = HIGH; onTxDone(); noFire();
#ifdef GENERIC_RECEIVER
    reset(); machineId = 0; receive(); noFire();
    reset(); machineId = 0; fireRelayAndAck(42); noFire();
#endif
    std::puts("GPIO, pulse duration, rejection, interlock, and save-before-fire checks passed");
}
"""


def check(path: Path, generic: bool) -> None:
    source = path.read_text()
    assert re.search(r"#define TM_RELAY_ACTIVE_LEVEL HIGH\b", source)
    assert re.search(r"#define TM_RELAY_PULSE_MS 300\b", source)
    assert "!TM_RELAY_ACTIVE_LEVEL" not in source
    boot = re.sub(r"//[^\n]*", "", function(source, "setup"))
    assert boot.index("relay_force_inactive();") < boot.index("Serial.begin")
    assert boot.index("relay_force_inactive();") < boot.index("initRadio();")
    handlers = ["relay_force_inactive", "onTxDone", "onTxTimeout",
                "onRxDone", "fireRelayAndAck"]
    if generic:
        handlers.insert(1, "validMachineId")
    body = "\n".join(function(source, name) for name in handlers)
    defines = "#define TM_RELAY_ACTIVE_LEVEL HIGH\n#define TM_RELAY_PULSE_MS 300\n"
    if generic:
        defines += "#define GENERIC_RECEIVER\n"
    with tempfile.TemporaryDirectory(prefix="tm-relay-polarity-") as directory:
        cpp = Path(directory) / "test.cpp"
        executable = Path(directory) / "test"
        cpp.write_text(STUBS + defines + body + TESTS)
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", str(cpp),
                        "-o", str(executable)], check=True)
        print(path.relative_to(ROOT), flush=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    for template in [ROOT / "TrapMasterRelay.config.example.h",
                     ROOT / "TrapMasterRelay/TrapMasterRelay.config.example.h",
                     ROOT / "create-local-config.sh"]:
        assert "#define TM_RELAY_ACTIVE_LEVEL HIGH" in template.read_text()
        assert "#define TM_RELAY_ACTIVE_LEVEL LOW" not in template.read_text()
    check(ROOT / "TrapMasterRelay.ino", generic=False)
    check(ROOT / "TrapMasterRelayGeneric/TrapMasterRelayGeneric.ino", generic=True)