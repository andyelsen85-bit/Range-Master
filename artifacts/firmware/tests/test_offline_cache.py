#!/usr/bin/env python3
"""Compile actual cache + sync publication code with fault-injected host mocks.

No physical flash, filesystem, server, or NVS is accessed.
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


STUBS = r"""
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <sys/types.h>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include "game_store.h"
#include "offline_cache.h"
GameStore g_store = {};
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_NVS_NOT_FOUND = 1;
constexpr int ESP_ERR_TIMEOUT = 2, ESP_ERR_INVALID_STATE = 3, ESP_ERR_NO_MEM = 4;
using wl_handle_t = int;
constexpr int WL_INVALID_HANDLE = -1;
using SemaphoreHandle_t = void *;
using nvs_handle_t = int;
constexpr int NVS_READWRITE = 1, portMAX_DELAY = 0;
constexpr int MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_INTERNAL = 4;
static std::vector<std::string> logs, operations;
void log_message(const char *, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void log_message(const char *, const char *fmt, ...) {
    char text[512]; va_list args; va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args); va_end(args); logs.emplace_back(text);
}
#define ESP_LOGI log_message
#define ESP_LOGW log_message
#define ESP_LOGE log_message
#define ESP_LOGD log_message
#define configASSERT assert
#define portENTER_CRITICAL(...) ((void)0)
#define portEXIT_CRITICAL(...) ((void)0)
void *xSemaphoreCreateMutex() { return (void *)1; }
void xSemaphoreTake(void *, int) {}
void xSemaphoreGive(void *) {}
void *heap_caps_malloc(size_t n, int) { return malloc(n); }
void heap_caps_free(void *p) { free(p); }
size_t heap_caps_get_free_size(int) { return 1000000; }
size_t heap_caps_get_largest_free_block(int) { return 1000000; }
const char *esp_err_to_name(int err) { return err == ESP_OK ? "ESP_OK" : "ESP_FAIL"; }
uint32_t esp_rom_crc32_le(uint32_t, const uint8_t *, size_t) { return 1; }
struct esp_vfs_fat_mount_config_t {
    bool format_if_mount_failed; int max_files; size_t allocation_unit_size;
};
struct esp_partition_t { uint32_t address, size; };
constexpr int ESP_PARTITION_TYPE_DATA = 1, ESP_PARTITION_SUBTYPE_DATA_FAT = 2;
const esp_partition_t *esp_partition_find_first(int, int, const char *label) {
    assert(std::string(label) == "storage");
    static esp_partition_t partition = {0x680000, 0x980000}; return &partition;
}
static uint64_t total_bytes = 0x970000, free_bytes = 0x900000;
static bool fresh = true, probe_fails = false, repaired_probe_fails = false;
static int mount_error = 0, format_error = 0, unmount_error = 0;
static int write_error = 0, fsync_error = 0, close_error = 0;
static int mounts = 0, formats = 0, unmounts = 0, nvs_saves = 0;
static std::map<std::string, std::vector<uint8_t>> files;
static std::map<int, std::string> descriptors;
static int next_fd = 1;
int nvs_open(const char *, int, int *handle) { *handle = 1; return ESP_OK; }
int nvs_get_u8(int, const char *, uint8_t *value) {
    *value = 3; return fresh ? ESP_ERR_NVS_NOT_FOUND : ESP_OK;
}
int nvs_set_u8(int, const char *, uint8_t value) { assert(value == 3); return ESP_OK; }
int nvs_commit(int) { return ESP_OK; }
void nvs_close(int) {}
int esp_vfs_fat_spiflash_mount_rw_wl(const char *, const char *label,
                                  const esp_vfs_fat_mount_config_t *cfg, int *handle) {
    assert(std::string(label) == "storage");
    assert(cfg->allocation_unit_size == 4096 && cfg->max_files == 12);
    ++mounts; operations.push_back("mount");
    if (mount_error) return mount_error;
    *handle = 9; return ESP_OK;
}
int esp_vfs_fat_spiflash_unmount_rw_wl(const char *, int handle) {
    assert(handle == 9); ++unmounts; operations.push_back("unmount"); return unmount_error;
}
int esp_vfs_fat_spiflash_format_cfg_rw_wl(const char *, const char *label,
                                        esp_vfs_fat_mount_config_t *cfg) {
    assert(std::string(label) == "storage");
    assert(cfg->allocation_unit_size == 4096 && cfg->max_files == 12);
    ++formats; operations.push_back("format");
    if (format_error) return format_error;
    files.clear(); total_bytes = 0x970000; free_bytes = 0x900000;
    probe_fails = repaired_probe_fails; return ESP_OK;
}
int esp_vfs_fat_info(const char *, uint64_t *total, uint64_t *available) {
    *total = total_bytes; *available = free_bytes; return ESP_OK;
}
size_t wl_sector_size(int) { return CONFIG_WL_SECTOR_SIZE; }
int tm_open(const char *path, int flags, ...) {
    int fd = next_fd++; descriptors[fd] = path;
    if (flags & O_TRUNC) files[path].clear();
    return fd;
}
ssize_t tm_write(int fd, const void *bytes, size_t length) {
    if (write_error || (probe_fails && descriptors[fd] == "/fatfs/probe.tmp")) {
        errno = write_error ? write_error : ENOSPC; return -1;
    }
    auto &out = files[descriptors[fd]];
    const auto *begin = (const uint8_t *)bytes; out.insert(out.end(), begin, begin + length);
    return (ssize_t)length;
}
ssize_t tm_read(int, void *, size_t) { errno = ENOENT; return -1; }
int tm_fsync(int) { if (fsync_error) { errno = fsync_error; return -1; } return 0; }
int tm_close(int fd) {
    descriptors.erase(fd);
    if (close_error) { errno = close_error; return -1; }
    return 0;
}
int tm_unlink(const char *path) { files.erase(path); errno = EINVAL; return 0; }
int tm_access(const char *path, int) {
    if (files.count(path)) return 0;
    errno = ENOENT; return -1;
}
int tm_rename(const char *old, const char *name) {
    if (files.count(name)) { errno = EEXIST; return -1; }
    files[name] = std::move(files[old]); files.erase(old); return 0;
}
int tm_mkdir(const char *, int) { return 0; }
void store_rebuild_bill_projection() {}
void store_reconcile_lineup_after_cache_load() {}
void game_store_save() { ++nvs_saves; }
void store_apply_portal_roster(const PortalSpieler *players, int count) {
    memcpy(g_store.portalSpieler, players, count * sizeof(*players));
    g_store.portalSpielerCount = count;
}
#define open tm_open
#define write tm_write
#define read tm_read
#define fsync tm_fsync
#define close tm_close
#define unlink tm_unlink
#define access tm_access
#define rename tm_rename
#define mkdir tm_mkdir
"""

SYNC_STUBS = r"""
using TickType_t = uint32_t;
static char s_last_error[128];
struct SyncManifest { bool available; char token[OFFLINE_CACHE_SECTION_COUNT][CACHE_MANIFEST_TOKEN_LEN]; };
bool fetch_manifest(SyncManifest *manifest) {
    *manifest = {}; manifest->available = true;
    for (auto &token : manifest->token) strcpy(token, "new-server-revision");
    return true;
}
bool sync_commit_begin(const char *, TickType_t *started) { *started = 0; return true; }
void sync_commit_end(const char *, TickType_t) {}
bool cop_wifi_is_connected() { return true; }
bool wait_for_trusted_day() { return true; }
void set_http_error(const char *, const char *, const char *) {}
static int server_error = ESP_OK;
int http_push_spieler_updates() { return ESP_OK; }
int http_push_verkauf_events() { return ESP_OK; }
int http_push_kredit_events() { return server_error; }
int http_push_payment_events() { return ESP_OK; }
int http_push_pending_games() { return ESP_OK; }
int http_backup_config_impl(bool) { return ESP_OK; }
int http_fetch_spieler(PortalSpieler *out, int, int *count) {
    *count = 1; out[0] = {}; out[0].id = 123; return ESP_OK;
}
static void cache_pulled_section(OfflineCacheSection);
int http_fetch_produkte() { cache_pulled_section(OFFLINE_CACHE_PRODUCTS); return ESP_OK; }
int http_fetch_bill_day_summary() { cache_pulled_section(OFFLINE_CACHE_BILLS); return ESP_OK; }
int http_pull_kredite() { cache_pulled_section(OFFLINE_CACHE_CREDITS); return ESP_OK; }
int http_pull_verkaeufe() { cache_pulled_section(OFFLINE_CACHE_SALES); return ESP_OK; }
void cJSON_Delete(void *) {}
"""

MAIN = r"""
bool logged(const std::string &text) {
    return std::any_of(logs.begin(), logs.end(),
        [&](const std::string &line) { return line.find(text) != std::string::npos; });
}
void reset() {
    g_store = {}; s_mounted = s_mount_attempted = false; s_wl = WL_INVALID_HANDLE;
    s_failed_sections = 0; s_metadata_failed = false;
    total_bytes = 0x970000; free_bytes = 0x900000;
    fresh = true; probe_fails = repaired_probe_fails = false;
    mount_error = format_error = unmount_error = 0;
    write_error = fsync_error = close_error = 0;
    mounts = formats = unmounts = nvs_saves = 0; server_error = ESP_OK;
    logs.clear(); operations.clear(); files.clear(); descriptors.clear();
}
int main() {
    reset(); fresh = false;
    assert(offline_cache_mount() && formats == 0);
    assert(logged("total=9895936") && logged("write(16): ret=16 errno=0"));
    assert(logged("fsync: ret=0 errno=0") && logged("close: ret=0 errno=0"));
    assert(offline_cache_mount() && mounts == 1);

    for (bool is_fresh : {false, true}) {
        reset(); fresh = is_fresh; total_bytes = free_bytes = 0;
        g_store.pendingKreditEventCount = 7;
        assert(offline_cache_mount() && formats == 1 && mounts == 2);
        assert((operations == std::vector<std::string>{"mount","unmount","format","mount"}));
        assert(logged("Deliberate FAT cache rebuild") && logged("write(16): ret=16 errno=0"));
        assert(g_store.pendingKreditEventCount == 7);
    }
    reset(); total_bytes = 8192; free_bytes = 4096;
    assert(offline_cache_mount() && formats == 1);
    reset(); free_bytes = 0; assert(offline_cache_mount() && formats == 1);
    reset(); probe_fails = true;
    assert(offline_cache_mount() && formats == 1);
    assert(logged("write(16): ret=-1 errno=28") && logged("fsync: ret=0 errno=0"));
    reset(); fresh = false; probe_fails = true;
    files["/fatfs/tmcache/roster.bin"] = {1}; // genuinely established cache
    assert(!offline_cache_mount() && formats == 0 && !g_store.offlineCacheHealthy);
    assert(!offline_cache_mount() && mounts == 1);
    reset(); fresh = false; probe_fails = true; // old marker but every snapshot write failed
    assert(offline_cache_mount() && formats == 1);
    assert(logged("empty_cache=1 fresh_volume=1"));
    reset(); probe_fails = repaired_probe_fails = true;
    assert(!offline_cache_mount() && formats == 1 && mounts == 2);
    assert(!offline_cache_mount() && formats == 1);
    for (bool fail_close : {false, true}) {
        reset();
        if (fail_close) close_error = EBADF; else fsync_error = EIO;
        assert(!offline_cache_mount() && formats == 1);
        assert(logged(fail_close ? "FAT self-test close: ret=-1 errno=9" :
                                  "FAT self-test fsync: ret=-1 errno=5"));
    }
    reset(); total_bytes = 0; format_error = ESP_FAIL;
    assert(!offline_cache_mount() && formats == 1 && mounts == 1);
    reset(); total_bytes = 0; unmount_error = ESP_FAIL;
    assert(!offline_cache_mount() && formats == 0);
    reset(); fresh = false; mount_error = ESP_FAIL;
    assert(!offline_cache_mount() && !offline_cache_mount() && mounts == 1);

    const char bytes[] = "snapshot";
    reset(); assert(offline_cache_mount());
    write_error = ENOSPC; close_error = EBADF;
    assert(!write_file("roster", 0, bytes, sizeof(bytes)));
    assert(logged("write(header) failed: ret=-1 remaining=16 errno=28"));
    assert(logged("close failed: ret=-1 errno=9")); // not stale ENOSPC
    reset(); assert(offline_cache_mount()); fsync_error = EIO;
    assert(!write_file("history", 2, bytes, sizeof(bytes)));
    assert(logged("fsync failed: ret=-1 errno=5"));
    assert(!logged("close failed"));
    reset(); assert(offline_cache_mount()); close_error = EBADF;
    assert(!write_file("history", 2, bytes, sizeof(bytes)));
    assert(logged("close failed: ret=-1 errno=9"));
    reset(); assert(offline_cache_mount());
    assert(write_file("roster", 0, bytes, sizeof(bytes)));
    assert(write_file("roster", 0, bytes, sizeof(bytes))); // FatFs replacement
    assert(files.count("/fatfs/tmcache/roster.bin") == 1);

    reset(); assert(offline_cache_mount()); write_error = ENOSPC;
    assert(!offline_cache_save(OFFLINE_CACHE_ROSTER) && !g_store.offlineCacheHealthy);
    assert(!offline_cache_set_manifest_token(OFFLINE_CACHE_ROSTER, "new-token"));
    assert(g_store.cacheManifestTokens[OFFLINE_CACHE_ROSTER][0] == '\0');
    write_error = 0;
    assert(offline_cache_save(OFFLINE_CACHE_PRODUCTS) && !g_store.offlineCacheHealthy);
    assert(offline_cache_save(OFFLINE_CACHE_ROSTER) && g_store.offlineCacheHealthy);
    fsync_error = EIO; assert(!offline_cache_save_metadata() && !g_store.offlineCacheHealthy);
    fsync_error = 0;
    assert(offline_cache_save(OFFLINE_CACHE_ROSTER) && !g_store.offlineCacheHealthy);
    assert(offline_cache_save_metadata() && g_store.offlineCacheHealthy);
    fsync_error = EIO;
    assert(!offline_cache_set_manifest_token(OFFLINE_CACHE_ROSTER, "new-token"));
    assert(g_store.cacheManifestTokens[OFFLINE_CACHE_ROSTER][0] == '\0');
    assert(!g_store.offlineCacheHealthy);
    reset(); assert(offline_cache_mount());
    for (int i = 0; i < OFFLINE_CACHE_SECTION_COUNT; ++i) {
        std::string token = "sha256:" + std::string(64, (char)('a' + i));
        assert(offline_cache_set_manifest_token((OfflineCacheSection)i, token.c_str()));
    }
    const auto &meta_file = files.at("/fatfs/tmcache/meta.bin");
    assert(meta_file.size() == sizeof(CacheEnvelope) + sizeof(MetaPayload));
    MetaPayload metadata = {};
    memcpy(&metadata, meta_file.data() + sizeof(CacheEnvelope), sizeof(metadata));
    for (int i = 0; i < OFFLINE_CACHE_SECTION_COUNT; ++i)
        assert(strcmp(metadata.tokens[i], g_store.cacheManifestTokens[i]) == 0);

    reset(); assert(offline_cache_mount()); write_error = ENOSPC;
    assert(http_sync_all_impl() == ESP_OK);
    assert(g_store.portalSpielerCount == 1 && g_store.portalSpieler[0].id == 123);
    assert(g_store.historyCount == 1 && nvs_saves >= 3);
    assert(g_store.lastSuccessfulSyncAt > 0 && !g_store.offlineCacheHealthy);
    assert(logged("Full sync succeeded; cache metadata unavailable"));
    assert(http_sync_billing_impl() == ESP_OK && !g_store.offlineCacheHealthy);
    server_error = ESP_FAIL;
    assert(http_sync_all_impl() == ESP_FAIL); // actual server failures still fail
    reset(); probe_fails = repaired_probe_fails = true;
    assert(http_sync_all_impl() == ESP_OK && !g_store.offlineCacheHealthy);
    assert(g_store.portalSpielerCount == 1 && mounts == 2 && formats == 1);
    std::puts("PASS: capacity + 16-byte diagnostics; fresh recovery; no healthy-volume erase or mount storms");
    std::puts("PASS: write/fsync/close errors captured separately; atomic replacement; section/metadata health");
    std::puts("PASS: failed FAT leaves roster/history applied + NVS save, full/billing sync succeed; server errors fail");
}
"""


if __name__ == "__main__":
    cache = (ROOT / "main/store/offline_cache.cpp").read_text()
    sync = (ROOT / "main/net/http_sync.cpp").read_text()
    cache_body = re.sub(r"^#include.*\n", "", cache, flags=re.MULTILINE)
    # Exercise the real history publication/return path after successful parsing.
    history = function(sync, "esp_err_t http_fetch_spielhistorie(void)")
    tail = history[history.rindex("    cJSON_Delete(root);"):]
    history_test = (
        "int http_fetch_spielhistorie() { void *root=nullptr; int count=1;"
        "auto *staged=(FinishedGame *)calloc(MAX_HISTORY,sizeof(FinishedGame));\n"
        + tail)
    helpers = "\n".join(function(sync, signature) for signature in [
        "static bool manifest_changed", "static void commit_manifest_token",
        "static void cache_pulled_section"])
    cycles = "\n".join(function(sync, signature) for signature in [
        "static esp_err_t http_sync_billing_impl", "static esp_err_t http_sync_all_impl"])
    # Guard every other parsed-data path against reintroducing cache-fatal code.
    for section in ("PRODUCTS", "CREDITS", "SALES", "BILLS", "HISTORY", "ROSTER"):
        assert f"cache_pulled_section(OFFLINE_CACHE_{section})" in sync
    assert "FAT cache was not durable" not in sync
    assert "g_store.offlineCacheHealthy = true" not in sync
    with tempfile.TemporaryDirectory(prefix="tm-offline-cache-") as directory:
        cpp = Path(directory) / "test.cpp"
        cpp.write_text(STUBS + cache_body + SYNC_STUBS + helpers +
                       history_test + cycles + MAIN)
        for sector_size in (4096, 512):
            binary = Path(directory) / f"test-{sector_size}"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            f"-DCONFIG_WL_SECTOR_SIZE={sector_size}",
                            "-I", str(ROOT / "main/store"), str(cpp), "-o", str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)