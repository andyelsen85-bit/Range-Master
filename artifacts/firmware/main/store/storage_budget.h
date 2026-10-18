#pragma once
#include <stddef.h>
#include <stdint.h>

// Budget usable FAT capacity, not raw flash (WL/FAT consume part of storage).
static constexpr size_t STORAGE_CLUSTER_BYTES = 4096;
static constexpr unsigned STORAGE_JOURNAL_SLOTS = 200;
static constexpr unsigned STORAGE_ARCHIVE_SLOTS = 20; // accepted history only
static constexpr unsigned STORAGE_FREE_PERCENT = 20;
static constexpr unsigned STORAGE_CACHE_PERCENT = 25;
static constexpr unsigned STORAGE_JOURNAL_PERCENT = 50;
static constexpr size_t storage_allocated(size_t bytes) {
    return (bytes + STORAGE_CLUSTER_BYTES - 1) / STORAGE_CLUSTER_BYTES * STORAGE_CLUSTER_BYTES;
}
