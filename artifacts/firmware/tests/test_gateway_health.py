#!/usr/bin/env python3
"""Exercise real health HTTP retry/debounce/logging helpers with SDK mocks."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("terminal_nonce_fixture", Path(__file__).with_name("test_gateway_nonce.py"))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)

EXTRA = r"""
#include <cerrno>
#include <cstdarg>
#include "gateway_health.h"
constexpr int ESP_ERR_NO_MEM = -2, ESP_ERR_INVALID_ARG = -3;
constexpr int ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME = -4;
constexpr int ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT = -5;
static int mock_errno = 0, mock_tls = 0;
int esp_http_client_get_errno(Client *) { return mock_errno; }
int esp_http_client_get_and_clear_last_tls_error(Client *, int *code, int *flags) {
    *code = mock_tls; *flags = 0; return ESP_OK;
}
const char *esp_err_to_name(int error) { return error == ESP_OK ? "ESP_OK" : "mock-error"; }
static const char *TAG = "test";
static GatewayHealthHistory s_health_history;
static char s_health_url[MAX_URL_LEN], s_health_token[MAX_KEY_LEN];
static GatewayReachability state = GATEWAY_CHECKING;
void set_gateway_state(GatewayReachability value) { state = value; }
static std::vector<std::string> logs;
void log_line(const char *fmt, ...) {
    char text[512]; va_list args; va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args); va_end(args); logs.emplace_back(text);
}
#define ESP_LOGW(tag, ...) ((void)(tag), log_line(__VA_ARGS__))
"""

MAIN = r"""
void reset_http() {
    calls = delays = issued = 0; urls.clear(); macs.clear(); replies.clear();
    mock_errno = mock_tls = 0;
}
void run_check(bool expected) {
    HealthFailure result;
    assert(perform_health_get("http://gateway/health", "same-mac", &result) == expected);
    apply_health_result(expected, result);
}
int main() {
    GatewayRequest scope = {};
    strcpy(scope.gateway_url, "http://gateway");
    strcpy(scope.gateway_token, "synthetic-test-only-key");
    adopt_health_scope(scope);
    assert(state == GATEWAY_CHECKING);
    reset_http();
    replies = {{ESP_ERR_TIMEOUT, 0, ""}, {ESP_OK, 200, ""}};
    run_check(true);
    assert(calls == 2 && delays == 0 && logs.empty()); // dropped SYN recovers immediately
    assert(s_health_history.consecutive == 0 && s_health_history.failed_since_boot == 0);
    adopt_health_scope(scope);
    assert(state == GATEWAY_REACHABLE); // first poll cannot discard prior FIRE evidence
    status = "Machine D fired"; state = GATEWAY_REACHABLE;
    for (int check = 1; check <= 3; ++check) {
        reset_http(); mock_errno = ETIMEDOUT;
        replies = {{ESP_ERR_TIMEOUT, 0, ""}, {ESP_ERR_TIMEOUT, 0, ""}};
        run_check(false);
        assert(calls == 2 && delays == 0 && logs.size() == (size_t)check);
        assert(s_health_history.consecutive == (unsigned)check);
        assert(logs.back().find("reason=timeout") != std::string::npos);
        if (check < 3) assert(state == GATEWAY_REACHABLE && status == "Machine D fired");
    }
    assert(state == GATEWAY_UNREACHABLE && s_health_history.failed_since_boot == 3);
    reset_http(); replies = {{ESP_OK, 200, ""}}; run_check(true);
    assert(state == GATEWAY_REACHABLE && s_health_history.consecutive == 0 &&
           s_health_history.failed_since_boot == 3);
    for (int check = 0; check < 3; ++check) {
        reset_http(); replies = {{ESP_OK, 401, ""}, {ESP_OK, 200, ""}};
        run_check(false); assert(calls == 1); // a final rejection is not a transport retry
        assert(logs.back().find("reason=unauthorized http=401") != std::string::npos);
    }
    assert(state == GATEWAY_AUTH_FAILED && s_health_history.failed_since_boot == 6);
    assert(logs.back().find("failedSinceBoot=6") != std::string::npos);
    reset_http(); mock_tls = ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME;
    replies = {{32774, 0, ""}, {32774, 0, ""}}; run_check(false);
    assert(logs.back().find("reason=DNS") != std::string::npos);
    reset_http(); mock_errno = ECONNREFUSED;
    replies = {{32774, 0, ""}, {32774, 0, ""}}; run_check(false);
    assert(logs.back().find("reason=refused") != std::string::npos);
    s_health_history.success();
    assert(s_health_history.failed_since_boot == 8 && s_health_history.consecutive == 0);
    strcpy(scope.gateway_url, "http://replacement-gateway");
    adopt_health_scope(scope);
    assert(state == GATEWAY_CHECKING && s_health_history.failed_since_boot == 8);
    puts("PASS: 2 s attempts, one immediate retry; 3 logical failures before status changes; boot failure total retained");
    puts("PASS: one-line timeout/refused/DNS/401 diagnostics; success resets streak; HTTP rejection never retried");
}
"""

if __name__ == "__main__":
    source = (ROOT / "main/lora_stub/lora_stub.cpp").read_text()
    types = source[source.index("typedef enum : uint8_t"):source.index("static void set_status")]
    functions = source[source.index("struct HealthFailure"):source.index("static bool fetch_gateway_nonce")]
    # No last-good replacement during polling, and no health-flag FIRE gating.
    worker = fixture.gateway.function(source, "static void gateway_worker")
    assert "set_gateway_state(GATEWAY_CHECKING)" not in worker[:worker.index("if (request.kind == GATEWAY_REQUEST_HEALTH)")]
    for name in ("bool lora_fire_machine(", "bool lora_fire_machine_game(",
                 "bool lora_fire_doublette(", "bool lora_fire_doublette_game("):
        admission = fixture.gateway.function(source, name)
        assert "lora_gateway_state(" not in admission and "GATEWAY_REACHABLE" not in admission
    stubs = fixture.STUBS.replace("assert(config->disable_auto_redirect);",
                                  "assert(config->disable_auto_redirect && config->timeout_ms == 2000);")
    with tempfile.TemporaryDirectory(prefix="tm-health-") as directory:
        path = Path(directory)
        (path / "mbedtls").mkdir()
        (path / "mbedtls/md.h").write_text(fixture.gateway.MD)
        (path / "test.cpp").write_text(stubs + types + EXTRA +
            fixture.gateway.function(source, "static void adopt_health_scope") + functions + MAIN)
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", directory, "-I", str(ROOT / "main/lora_stub"),
                        "-I", str(ROOT.parent / "lora-common"),
                        str(path / "test.cpp"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)