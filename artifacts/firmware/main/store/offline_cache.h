#pragma once

#include <stdbool.h>
#include <stdint.h>

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
void offline_cache_load(void);
bool offline_cache_save(OfflineCacheSection section);
bool offline_cache_save_metadata(void);
bool offline_cache_set_manifest_token(OfflineCacheSection section, const char *token);

// Host/unit builds can expose the pure count validator without mounting FAT.
#ifdef TM_OFFLINE_CACHE_TEST_HOOKS
bool offline_cache_test_count_valid(OfflineCacheSection section, int count);
#endif