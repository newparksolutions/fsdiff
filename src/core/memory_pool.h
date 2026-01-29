/**
 * @file memory_pool.h
 * @brief Arena-style memory pool for efficient batch allocations
 */

#ifndef FSDIFF_MEMORY_POOL_H
#define FSDIFF_MEMORY_POOL_H

#include <fsdiff/types.h>
#include <fsdiff/error.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Memory pool chunk */
typedef struct fsd_pool_chunk {
    uint8_t *data;
    size_t size;
    size_t used;
    struct fsd_pool_chunk *next;
} fsd_pool_chunk_t;

/** Memory pool handle */
typedef struct fsd_memory_pool {
    fsd_pool_chunk_t *chunks;      /**< Linked list of chunks */
    fsd_pool_chunk_t *current;     /**< Current chunk for allocation */
    size_t chunk_size;             /**< Size of each chunk */
    size_t total_allocated;        /**< Total bytes allocated */
    size_t max_memory;             /**< Maximum allowed (0 = unlimited) */
    fsd_allocator_t allocator;     /**< Underlying allocator */
} fsd_memory_pool_t;

/**
 * Create a memory pool.
 *
 * @param pool_out     Output pointer for pool
 * @param chunk_size   Size of each chunk (recommend: 64 KiB - 1 MiB)
 * @param max_memory   Maximum total memory (0 = unlimited)
 * @param allocator    Custom allocator (NULL for default)
 * @return             FSD_SUCCESS or error code
 */
fsd_error_t fsd_pool_create(fsd_memory_pool_t **pool_out,
                            size_t chunk_size,
                            size_t max_memory,
                            const fsd_allocator_t *allocator);

/**
 * Allocate memory from the pool.
 *
 * @param pool  Memory pool
 * @param size  Bytes to allocate
 * @return      Pointer to allocated memory, or NULL on failure
 */
void *fsd_pool_alloc(fsd_memory_pool_t *pool, size_t size);

/**
 * Allocate aligned memory from the pool.
 *
 * @param pool       Memory pool
 * @param size       Bytes to allocate
 * @param alignment  Required alignment (must be power of 2)
 * @return           Pointer to aligned memory, or NULL on failure
 */
void *fsd_pool_alloc_aligned(fsd_memory_pool_t *pool, size_t size, size_t alignment);

/**
 * Reset the pool for reuse.
 *
 * This keeps allocated chunks but marks them as empty.
 *
 * @param pool  Memory pool
 */
void fsd_pool_reset(fsd_memory_pool_t *pool);

/**
 * Get total bytes allocated from the pool.
 *
 * @param pool  Memory pool
 * @return      Total bytes allocated
 */
size_t fsd_pool_usage(const fsd_memory_pool_t *pool);

/**
 * Destroy the pool and free all memory.
 *
 * @param pool  Memory pool (NULL is safe)
 */
void fsd_pool_destroy(fsd_memory_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_MEMORY_POOL_H */
