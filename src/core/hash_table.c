/**
 * @file hash_table.c
 * @brief Hash table implementation for block lookup
 */

#include "hash_table.h"
#include <stdlib.h>
#include <string.h>

/* Load factor target: 0.75 */
#define LOAD_FACTOR_NUM   3
#define LOAD_FACTOR_DENOM 4

/* Minimum bucket count */
#define MIN_BUCKETS 1024

/* Hash function to map CRC32 to bucket index */
static inline size_t hash_to_bucket(uint32_t crc32, size_t bucket_count) {
    /* CRC32 is already well-distributed, use simple modulo */
    return crc32 % bucket_count;
}

/* Find next power of 2 >= n */
static size_t next_power_of_2(size_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
#if SIZE_MAX > 0xFFFFFFFF
    n |= n >> 32;
#endif
    return n + 1;
}

fsd_error_t fsd_hash_table_create(fsd_hash_table_t **table_out,
                                  uint64_t expected_entries,
                                  fsd_memory_pool_t *pool) {
    if (!table_out || !pool) {
        return FSD_ERR_INVALID_ARG;
    }

    /* Calculate bucket count for target load factor */
    size_t bucket_count = (expected_entries * LOAD_FACTOR_DENOM) / LOAD_FACTOR_NUM;
    if (bucket_count < MIN_BUCKETS) {
        bucket_count = MIN_BUCKETS;
    }
    bucket_count = next_power_of_2(bucket_count);

    fsd_hash_table_t *table = fsd_pool_alloc(pool, sizeof(fsd_hash_table_t));
    if (!table) {
        return FSD_ERR_OUT_OF_MEMORY;
    }

    table->buckets = fsd_pool_alloc(pool, bucket_count * sizeof(fsd_hash_entry_t *));
    if (!table->buckets) {
        return FSD_ERR_OUT_OF_MEMORY;
    }

    memset(table->buckets, 0, bucket_count * sizeof(fsd_hash_entry_t *));
    table->bucket_count = bucket_count;
    table->entry_count = 0;
    table->pool = pool;

    *table_out = table;
    return FSD_SUCCESS;
}

void fsd_hash_table_insert(fsd_hash_table_t *table,
                           uint32_t crc32,
                           uint64_t block_index) {
    if (!table) return;

    fsd_hash_entry_t *entry = fsd_pool_alloc(table->pool, sizeof(fsd_hash_entry_t));
    if (!entry) return;  /* Allocation failure - silently skip */

    entry->crc32 = crc32;
    entry->block_index = block_index;

    /* Insert at head of chain */
    size_t bucket = hash_to_bucket(crc32, table->bucket_count);
    entry->next = table->buckets[bucket];
    table->buckets[bucket] = entry;

    table->entry_count++;
}

const fsd_hash_entry_t *fsd_hash_table_lookup(const fsd_hash_table_t *table,
                                               uint32_t crc32) {
    if (!table) return NULL;

    size_t bucket = hash_to_bucket(crc32, table->bucket_count);
    fsd_hash_entry_t *entry = table->buckets[bucket];

    /* Find first entry with matching CRC32 */
    while (entry) {
        if (entry->crc32 == crc32) {
            return entry;
        }
        entry = entry->next;
    }

    return NULL;
}

size_t fsd_hash_table_count(const fsd_hash_table_t *table) {
    return table ? table->entry_count : 0;
}

void fsd_hash_table_destroy(fsd_hash_table_t *table) {
    /* Entries and table are allocated from pool, nothing to free here */
    (void)table;
}
