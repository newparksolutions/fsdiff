/**
 * @file test_memory_pool.c
 * @brief Memory pool tests
 */

#include "core/memory_pool.h"
#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        return 1; \
    } \
} while(0)

static int test_create_destroy(void) {
    fsd_memory_pool_t *pool = NULL;
    fsd_error_t err = fsd_pool_create(&pool, 4096, 0, NULL);
    TEST_ASSERT(err == FSD_SUCCESS, "Pool creation should succeed");
    TEST_ASSERT(pool != NULL, "Pool should not be NULL");

    fsd_pool_destroy(pool);
    return 0;
}

static int test_basic_alloc(void) {
    fsd_memory_pool_t *pool = NULL;
    fsd_pool_create(&pool, 4096, 0, NULL);

    void *ptr = fsd_pool_alloc(pool, 100);
    TEST_ASSERT(ptr != NULL, "Small allocation should succeed");

    /* Write to memory to ensure it's valid */
    memset(ptr, 0xAB, 100);

    fsd_pool_destroy(pool);
    return 0;
}

static int test_multiple_allocs(void) {
    fsd_memory_pool_t *pool = NULL;
    fsd_pool_create(&pool, 4096, 0, NULL);

    void *ptrs[100];
    for (int i = 0; i < 100; i++) {
        ptrs[i] = fsd_pool_alloc(pool, 64);
        TEST_ASSERT(ptrs[i] != NULL, "Multiple allocations should succeed");
        memset(ptrs[i], i, 64);
    }

    /* Verify data wasn't corrupted */
    for (int i = 0; i < 100; i++) {
        uint8_t *data = (uint8_t *)ptrs[i];
        for (int j = 0; j < 64; j++) {
            TEST_ASSERT(data[j] == (uint8_t)i, "Data should not be corrupted");
        }
    }

    fsd_pool_destroy(pool);
    return 0;
}

static int test_large_alloc(void) {
    fsd_memory_pool_t *pool = NULL;
    fsd_pool_create(&pool, 4096, 0, NULL);

    /* Allocate larger than chunk size */
    void *ptr = fsd_pool_alloc(pool, 8192);
    TEST_ASSERT(ptr != NULL, "Large allocation should succeed");

    memset(ptr, 0xCD, 8192);

    fsd_pool_destroy(pool);
    return 0;
}

static int test_aligned_alloc(void) {
    fsd_memory_pool_t *pool = NULL;
    fsd_pool_create(&pool, 4096, 0, NULL);

    void *ptr = fsd_pool_alloc_aligned(pool, 256, 64);
    TEST_ASSERT(ptr != NULL, "Aligned allocation should succeed");
    TEST_ASSERT(((uintptr_t)ptr % 64) == 0, "Allocation should be 64-byte aligned");

    fsd_pool_destroy(pool);
    return 0;
}

static int test_reset(void) {
    fsd_memory_pool_t *pool = NULL;
    fsd_pool_create(&pool, 4096, 0, NULL);

    /* Allocate some memory */
    fsd_pool_alloc(pool, 1000);
    fsd_pool_alloc(pool, 2000);
    fsd_pool_alloc(pool, 500);

    /* Reset the pool */
    fsd_pool_reset(pool);

    /* Should be able to allocate again */
    void *ptr = fsd_pool_alloc(pool, 3000);
    TEST_ASSERT(ptr != NULL, "Allocation after reset should succeed");

    fsd_pool_destroy(pool);
    return 0;
}

int main(void) {
    int failures = 0;

    printf("Running memory pool tests...\n");

    failures += test_create_destroy();
    failures += test_basic_alloc();
    failures += test_multiple_allocs();
    failures += test_large_alloc();
    failures += test_aligned_alloc();
    failures += test_reset();

    if (failures == 0) {
        printf("All memory pool tests passed!\n");
        return 0;
    } else {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
}
