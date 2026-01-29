/**
 * @file hash_table.h
 * @brief CRC32 hash table for block lookup during relocation matching
 */

#ifndef FSDIFF_HASH_TABLE_H
#define FSDIFF_HASH_TABLE_H

#include <fsdiff/types.h>
#include <fsdiff/error.h>
#include "memory_pool.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Hash table entry for CRC32-based block lookup */
typedef struct fsd_hash_entry {
    uint32_t crc32;                  /**< CRC32 of block */
    uint64_t block_index;            /**< Block index in source image */
    struct fsd_hash_entry *next;     /**< Next entry in chain */
} fsd_hash_entry_t;

/** Hash table for block lookup */
typedef struct fsd_hash_table {
    fsd_hash_entry_t **buckets;      /**< Array of bucket heads */
    size_t bucket_count;             /**< Number of buckets */
    size_t entry_count;              /**< Total entries inserted */
    fsd_memory_pool_t *pool;         /**< Pool for entry allocation */
} fsd_hash_table_t;

/**
 * Create a hash table sized for expected entries.
 *
 * @param table_out        Output pointer for table
 * @param expected_entries Expected number of entries
 * @param pool             Memory pool for allocations
 * @return                 FSD_SUCCESS or error code
 */
fsd_error_t fsd_hash_table_create(fsd_hash_table_t **table_out,
                                  uint64_t expected_entries,
                                  fsd_memory_pool_t *pool);

/**
 * Insert a block index with its CRC32.
 *
 * @param table        Hash table
 * @param crc32        CRC32 of the block
 * @param block_index  Block index to store
 */
void fsd_hash_table_insert(fsd_hash_table_t *table,
                           uint32_t crc32,
                           uint64_t block_index);

/**
 * Look up entries by CRC32.
 *
 * @param table  Hash table
 * @param crc32  CRC32 to search for
 * @return       First entry in chain (may be NULL), follow ->next for more
 */
const fsd_hash_entry_t *fsd_hash_table_lookup(const fsd_hash_table_t *table,
                                               uint32_t crc32);

/**
 * Get number of entries in the table.
 *
 * @param table  Hash table
 * @return       Entry count
 */
size_t fsd_hash_table_count(const fsd_hash_table_t *table);

/**
 * Destroy the hash table.
 *
 * Note: Entries are allocated from the pool, so they are freed when the
 * pool is destroyed. This only frees the bucket array.
 *
 * @param table  Hash table (NULL is safe)
 */
void fsd_hash_table_destroy(fsd_hash_table_t *table);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_HASH_TABLE_H */
