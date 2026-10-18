#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "storage_repair.h"

// FAT-backed, replace-only snapshots of portal data.  This deliberately does
// not contain outboxes: idempotent local actions remain in NVS.
typedef enum {
    OFFLINE_CACHE_ROSTER = 0,
    OFFLINE_CACHE_PRODUCTS,
    OFFLINE_CACHE_HISTORY,
    OFFLINE_CACHE_CREDITS,
    OFFLINE_CACHE_SALES,
    OFFLINE_CACHE_BILLS,
    OFFLINE_CACHE_SECTION_COUNT,
} OfflineCacheSection;

bool offline_cache_mount(void);
const char *offline_cache_path_error();
void offline_cache_load(void);
bool offline_cache_save(OfflineCacheSection section, bool defer = false);
void offline_cache_flush_pending(); // worker context, flush throttled dirty snapshots
bool offline_cache_save_metadata(void);
bool offline_cache_set_manifest_token(OfflineCacheSection section, const char *token);
bool offline_cache_storage_available(void); // readable mount, independent of optional cache health
bool offline_cache_storage_writable(void); // admission gate, including incomplete repair
bool offline_cache_recheck(void);
bool offline_cache_cleanup(void); // cache-only; refuses pending or unverified journal records
bool offline_cache_repair(void); // format only after CRC proof: no durable pending work
bool offline_cache_cleanup_busy(void);
bool offline_cache_storage_warning(char *out, size_t size);
int offline_cache_pending_games(void); // max(RAM,disk), -1 when proof is uncertain
void offline_cache_repair_status(StorageRepairStatus *out);

// Host/unit builds can expose the pure count validator without mounting FAT.
#ifdef TM_OFFLINE_CACHE_TEST_HOOKS
bool offline_cache_test_count_valid(OfflineCacheSection section, int count);
#endif