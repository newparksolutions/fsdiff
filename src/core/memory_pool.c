/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file memory_pool.c
 * @brief Arena-style memory pool implementation
 */

#include "memory_pool.h"
#include <stdlib.h>
#include <string.h>

/* Default allocator wrappers */
static void *default_malloc(size_t size, void *user_data) {
    (void)user_data;
    return malloc(size);
}

static void default_free(void *ptr, void *user_data) {
    (void)user_data;
    free(ptr);
}

static void *default_realloc(void *ptr, size_t size, void *user_data) {
    (void)user_data;
    return realloc(ptr, size);
}

/* Create a new chunk */
static fsd_pool_chunk_t *create_chunk(fsd_memory_pool_t *pool, size_t size) {
    if (pool->max_memory > 0 &&
        pool->total_allocated + size > pool->max_memory) {
        return NULL;  /* Would exceed limit */
    }

    fsd_pool_chunk_t *chunk = pool->allocator.malloc_fn(
        sizeof(fsd_pool_chunk_t), pool->allocator.user_data);
    if (!chunk) return NULL;

    chunk->data = pool->allocator.malloc_fn(size, pool->allocator.user_data);
    if (!chunk->data) {
        pool->allocator.free_fn(chunk, pool->allocator.user_data);
        return NULL;
    }

    chunk->size = size;
    chunk->used = 0;
    chunk->next = NULL;

    pool->total_allocated += size;
    return chunk;
}

fsd_error_t fsd_pool_create(fsd_memory_pool_t **pool_out,
                            size_t chunk_size,
                            size_t max_memory,
                            const fsd_allocator_t *allocator) {
    if (!pool_out || chunk_size == 0) {
        return FSD_ERR_INVALID_ARG;
    }

    /* Use default allocator if none provided */
    fsd_allocator_t alloc;
    if (allocator) {
        alloc = *allocator;
    } else {
        alloc.malloc_fn = default_malloc;
        alloc.free_fn = default_free;
        alloc.realloc_fn = default_realloc;
        alloc.user_data = NULL;
    }

    fsd_memory_pool_t *pool = alloc.malloc_fn(sizeof(fsd_memory_pool_t), alloc.user_data);
    if (!pool) {
        return FSD_ERR_OUT_OF_MEMORY;
    }

    pool->chunks = NULL;
    pool->current = NULL;
    pool->chunk_size = chunk_size;
    pool->total_allocated = 0;
    pool->max_memory = max_memory;
    pool->allocator = alloc;

    /* Create initial chunk */
    fsd_pool_chunk_t *initial = create_chunk(pool, chunk_size);
    if (!initial) {
        alloc.free_fn(pool, alloc.user_data);
        return FSD_ERR_OUT_OF_MEMORY;
    }

    pool->chunks = initial;
    pool->current = initial;

    *pool_out = pool;
    return FSD_SUCCESS;
}

void *fsd_pool_alloc(fsd_memory_pool_t *pool, size_t size) {
    if (!pool || size == 0) return NULL;

    fsd_pool_chunk_t *chunk = pool->current;

    /* Check if current chunk has space. Subtraction-against-free-space
     * form avoids overflow if chunk->used is near SIZE_MAX. */
    if (chunk && size <= chunk->size - chunk->used) {
        void *ptr = chunk->data + chunk->used;
        chunk->used += size;
        return ptr;
    }

    /* Need a new chunk */
    size_t new_size = (size > pool->chunk_size) ? size : pool->chunk_size;
    fsd_pool_chunk_t *new_chunk = create_chunk(pool, new_size);
    if (!new_chunk) return NULL;

    /* Link new chunk */
    if (chunk) {
        chunk->next = new_chunk;
    } else {
        pool->chunks = new_chunk;
    }
    pool->current = new_chunk;

    void *ptr = new_chunk->data;
    new_chunk->used = size;
    return ptr;
}

void *fsd_pool_alloc_aligned(fsd_memory_pool_t *pool, size_t size, size_t alignment) {
    if (!pool || size == 0 || alignment == 0) return NULL;
    if ((alignment & (alignment - 1)) != 0) return NULL;  /* Not power of 2 */

    fsd_pool_chunk_t *chunk = pool->current;

    if (chunk) {
        /* Calculate aligned position */
        uintptr_t addr = (uintptr_t)(chunk->data + chunk->used);
        uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
        size_t padding = aligned - addr;

        /* Overflow-safe: free = chunk->size - chunk->used, then check
         * padding + size <= free without ever adding to chunk->used. */
        size_t free_space = chunk->size - chunk->used;
        if (padding <= free_space && size <= free_space - padding) {
            chunk->used += padding + size;
            return (void *)aligned;
        }
    }

    /* Need new chunk - ensure it's aligned from the start. Reject sizes
     * that would overflow when padded for alignment. */
    if (size > SIZE_MAX - (alignment - 1)) return NULL;
    size_t new_size = size + alignment - 1;
    if (new_size < pool->chunk_size) new_size = pool->chunk_size;

    fsd_pool_chunk_t *new_chunk = create_chunk(pool, new_size);
    if (!new_chunk) return NULL;

    if (chunk) {
        chunk->next = new_chunk;
    } else {
        pool->chunks = new_chunk;
    }
    pool->current = new_chunk;

    /* Align within new chunk */
    uintptr_t addr = (uintptr_t)new_chunk->data;
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    size_t padding = aligned - addr;

    new_chunk->used = padding + size;
    return (void *)aligned;
}

void fsd_pool_reset(fsd_memory_pool_t *pool) {
    if (!pool) return;

    /* Reset all chunks */
    for (fsd_pool_chunk_t *chunk = pool->chunks; chunk; chunk = chunk->next) {
        chunk->used = 0;
    }

    pool->current = pool->chunks;
}

size_t fsd_pool_usage(const fsd_memory_pool_t *pool) {
    if (!pool) return 0;

    size_t used = 0;
    for (fsd_pool_chunk_t *chunk = pool->chunks; chunk; chunk = chunk->next) {
        used += chunk->used;
    }
    return used;
}

void fsd_pool_destroy(fsd_memory_pool_t *pool) {
    if (!pool) return;

    /* Free all chunks */
    fsd_pool_chunk_t *chunk = pool->chunks;
    while (chunk) {
        fsd_pool_chunk_t *next = chunk->next;
        pool->allocator.free_fn(chunk->data, pool->allocator.user_data);
        pool->allocator.free_fn(chunk, pool->allocator.user_data);
        chunk = next;
    }

    pool->allocator.free_fn(pool, pool->allocator.user_data);
}
