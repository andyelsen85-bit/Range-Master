#!/usr/bin/env python3
"""Compile actual web form handlers with HTTP/NVS mocks; requires g++."""
from html.parser import HTMLParser
from pathlib import Path
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parents[1] / "main/net/web_config.cpp"

STUBS = r"""
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1;
constexpr int HTTPD_400_BAD_REQUEST = 400, HTTPD_500_INTERNAL_SERVER_ERROR = 500;
constexpr int HTTPD_RESP_USE_STRLEN = -1;
#define MAX_URL_LEN 256
#define MAX_KEY_LEN 256
#define ESP_LOGI(...) ((void)0)
struct Store {
    char apiUrl[256], apiKey[256], gatewayUrl[256], gatewayToken[256];
    char configBackupStatus[256], lastConfigBackupAt[256];
} g_store = {};
static int saves = 0;
void game_store_save() { ++saves; }
struct httpd_req_t {
    void *user_ctx = nullptr;
    int content_len = 0;
    std::string body, response;
    bool ended = false;
    int endings = 0;
};
int httpd_resp_set_type(httpd_req_t *, const char *) { return ESP_OK; }
int httpd_resp_set_hdr(httpd_req_t *, const char *, const char *) { return ESP_OK; }
int httpd_resp_send_chunk(httpd_req_t *req, const char *data, size_t length) {
    if (length == 0) { req->ended = true; ++req->endings; return ESP_OK; }
    if (req->ended) return ESP_FAIL;
    req->response.append(data, length);
    return ESP_OK;
}
int httpd_resp_sendstr_chunk(httpd_req_t *req, const char *data) {
    return httpd_resp_send_chunk(req, data, data ? strlen(data) : 0);
}
int httpd_resp_send(httpd_req_t *req, const char *data, int length) {
    req->response.assign(data, length < 0 ? strlen(data) : length);
    return ESP_OK;
}
int httpd_resp_send_err(httpd_req_t *, int, const char *) { return ESP_OK; }
int httpd_req_recv(httpd_req_t *req, char *body, int length) {
    assert(length == int(req->body.size()));
    memcpy(body, req->body.data(), length);
    return length;
}
"""

MAIN = r"""
void capture(const std::string &directory, const char *name) {
    httpd_req_t req;
    assert(root_get_handler(&req) == ESP_OK);
    assert(req.endings == 1);
    assert(req.response.find("</body></html>") != std::string::npos);
    assert(req.response.find("Speichern</button>") != std::string::npos);
    std::ofstream(directory + "/" + name) << req.response;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    capture(argv[1], "empty.html");
    strcpy(g_store.apiUrl, "https://portal.example");
    strcpy(g_store.apiKey, "fake-key");
    capture(argv[1], "new.html");
    strcpy(g_store.gatewayUrl, "http://192.168.1.50");
    strcpy(g_store.gatewayToken, "fake-gateway-key");
    capture(argv[1], "gateway.html");
    strcpy(g_store.apiKey, "'\"><test>&");
    strcpy(g_store.gatewayToken, "'\"><test>&");
    memset(g_store.configBackupStatus, '&', 250);
    capture(argv[1], "escaped.html");
    httpd_req_t post;
    post.body = "apiUrl=https%3A%2F%2Fsecond.example&apiKey=second-test-key&gatewayUrl=&gatewayKey=";
    post.content_len = post.body.size();
    assert(save_post_handler(&post) == ESP_OK && saves == 1);
    assert(std::string(g_store.apiUrl) == "https://second.example");
    assert(std::string(g_store.apiKey) == "second-test-key");
    assert(g_store.gatewayUrl[0] == '\0' && g_store.gatewayToken[0] == '\0');
    assert(post.response.find("Gespeichert!") != std::string::npos);
    capture(argv[1], "saved.html");
}
"""


class Form(HTMLParser):
    def __init__(self, html):
        super().__init__(convert_charrefs=True)
        self.inputs = {}
        self.save_forms = 0
        self.submit_buttons = 0
        self.feed(html)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "input":
            self.inputs[attrs["name"]] = attrs.get("value", "")
        if tag == "form" and attrs.get("action") == "/save":
            assert attrs.get("method").upper() == "POST"
            self.save_forms += 1
        if tag == "button" and attrs.get("type") == "submit":
            self.submit_buttons += 1
        assert tag != "test", "Unescaped value injected HTML"


if __name__ == "__main__":
    source = SOURCE.read_text()
    rendering = source[source.index("// ── Helpers"):source.index("static esp_err_t action_result")]
    saving = source[source.index("static esp_err_t save_post_handler"):source.index("// ── URI table")]
    with tempfile.TemporaryDirectory(prefix="tm-web-config-") as directory:
        directory = Path(directory)
        cpp = directory / "test.cpp"
        cpp.write_text(STUBS + rendering + saving + MAIN)
        binary = directory / "test"
        subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra",
                        str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary), str(directory)], check=True)
        for name in ["empty", "new", "gateway", "escaped", "saved"]:
            form = Form((directory / f"{name}.html").read_text())
            assert form.save_forms == 1 and form.submit_buttons == 1
            assert all(key in form.inputs for key in
                       ["apiUrl", "apiKey", "gatewayUrl", "gatewayKey"])
            if name == "gateway":
                assert form.inputs["gatewayUrl"] == "http://192.168.1.50"
                assert form.inputs["gatewayKey"] == "fake-gateway-key"
            if name == "escaped":
                assert form.inputs["apiKey"] == "'\"><test>&"
                assert form.inputs["gatewayKey"] == "'\"><test>&"
            if name == "saved":
                assert form.inputs["apiKey"] == "second-test-key"
            print(f"PASS: {name} form complete, correctly rendered, and save button present")
        print("PASS: API settings POST calls persistence and returns saved confirmation")