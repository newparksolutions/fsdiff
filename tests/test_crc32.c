/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file test_crc32.c
 * @brief CRC32 implementation tests
 */

#include "core/crc32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        return 1; \
    } \
} while(0)

static int test_empty(void) {
    uint8_t data[1] = {0};
    uint32_t crc = fsd_crc32(data, 0);
    TEST_ASSERT(crc == 0, "Empty data should have CRC of 0");
    return 0;
}

static int test_known_values(void) {
    /* Known CRC32 test vectors (using standard CRC32) */
    const char *test_string = "123456789";
    uint32_t crc = fsd_crc32((const uint8_t *)test_string, 9);
    /* Expected: 0xCBF43926 for IEEE CRC32 */
    TEST_ASSERT(crc == 0xCBF43926, "Known value test failed");
    return 0;
}

static int test_incremental(void) {
    const char *data = "Hello, World!";
    size_t len = strlen(data);

    /* Full computation */
    uint32_t full_crc = fsd_crc32((const uint8_t *)data, len);

    /* Incremental computation */
    uint32_t inc_crc = 0;
    for (size_t i = 0; i < len; i++) {
        inc_crc = fsd_crc32_update(inc_crc, (const uint8_t *)&data[i], 1);
    }
    inc_crc = fsd_crc32_final(inc_crc);

    TEST_ASSERT(full_crc == inc_crc, "Incremental CRC should match full CRC");
    return 0;
}

static int test_different_data(void) {
    uint8_t data1[] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint8_t data2[] = {0, 1, 2, 3, 4, 5, 6, 8};  /* Different last byte */

    uint32_t crc1 = fsd_crc32(data1, sizeof(data1));
    uint32_t crc2 = fsd_crc32(data2, sizeof(data2));

    TEST_ASSERT(crc1 != crc2, "Different data should have different CRCs");
    return 0;
}

static int test_block_crc(void) {
    uint8_t block[4096];
    memset(block, 0xAB, sizeof(block));

    uint32_t crc = fsd_crc32(block, sizeof(block));

    /* Should be consistent */
    uint32_t crc2 = fsd_crc32(block, sizeof(block));
    TEST_ASSERT(crc == crc2, "Block CRC should be consistent");

    /* Different block should have different CRC */
    block[0] = 0xAC;
    uint32_t crc3 = fsd_crc32(block, sizeof(block));
    TEST_ASSERT(crc != crc3, "Different block should have different CRC");

    return 0;
}

int main(void) {
    int failures = 0;

    printf("Running CRC32 tests...\n");

    failures += test_empty();
    failures += test_known_values();
    failures += test_incremental();
    failures += test_different_data();
    failures += test_block_crc();

    if (failures == 0) {
        printf("All CRC32 tests passed!\n");
        return 0;
    } else {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
}
