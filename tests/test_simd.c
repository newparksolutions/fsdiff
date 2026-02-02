/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file test_simd.c
 * @brief SIMD implementation tests
 *
 * Validates SIMD implementations (AVX2/NEON) match scalar reference.
 */

#include <fsdiff/fsdiff.h>
#include "../src/simd/simd_dispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        return 1; \
    } \
} while(0)

/* Test is_zero with various patterns */
static int test_is_zero(void) {
    printf("  Testing is_zero...\n");

    /* All zeros */
    uint8_t zeros[4096];
    memset(zeros, 0, sizeof(zeros));
    TEST_ASSERT(g_fsd_simd.is_zero(zeros, sizeof(zeros)),
                "Should detect all-zero block");

    /* One non-zero byte at start */
    zeros[0] = 1;
    TEST_ASSERT(!g_fsd_simd.is_zero(zeros, sizeof(zeros)),
                "Should reject block with non-zero at start");
    zeros[0] = 0;

    /* One non-zero byte in middle */
    zeros[2048] = 1;
    TEST_ASSERT(!g_fsd_simd.is_zero(zeros, sizeof(zeros)),
                "Should reject block with non-zero in middle");
    zeros[2048] = 0;

    /* One non-zero byte at end */
    zeros[4095] = 1;
    TEST_ASSERT(!g_fsd_simd.is_zero(zeros, sizeof(zeros)),
                "Should reject block with non-zero at end");
    zeros[4095] = 0;

    /* Test small sizes */
    TEST_ASSERT(g_fsd_simd.is_zero(zeros, 1), "1-byte zero");
    TEST_ASSERT(g_fsd_simd.is_zero(zeros, 7), "7-byte zero (unaligned)");
    TEST_ASSERT(g_fsd_simd.is_zero(zeros, 16), "16-byte zero");
    TEST_ASSERT(g_fsd_simd.is_zero(zeros, 31), "31-byte zero");
    TEST_ASSERT(g_fsd_simd.is_zero(zeros, 33), "33-byte zero");
    TEST_ASSERT(g_fsd_simd.is_zero(zeros, 512), "512-byte zero");

    printf("    PASS\n");
    return 0;
}

/* Test is_one with various patterns */
static int test_is_one(void) {
    printf("  Testing is_one...\n");

    /* All 0xFF */
    uint8_t ones[4096];
    memset(ones, 0xFF, sizeof(ones));
    TEST_ASSERT(g_fsd_simd.is_one(ones, sizeof(ones)),
                "Should detect all-one block");

    /* One zero byte at start */
    ones[0] = 0;
    TEST_ASSERT(!g_fsd_simd.is_one(ones, sizeof(ones)),
                "Should reject block with zero at start");
    ones[0] = 0xFF;

    /* One zero byte in middle */
    ones[2048] = 0;
    TEST_ASSERT(!g_fsd_simd.is_one(ones, sizeof(ones)),
                "Should reject block with zero in middle");
    ones[2048] = 0xFF;

    /* One zero byte at end */
    ones[4095] = 0;
    TEST_ASSERT(!g_fsd_simd.is_one(ones, sizeof(ones)),
                "Should reject block with zero at end");
    ones[4095] = 0xFF;

    /* One different value (not 0, not 0xFF) */
    ones[1000] = 0x7F;
    TEST_ASSERT(!g_fsd_simd.is_one(ones, sizeof(ones)),
                "Should reject block with non-0xFF value");
    ones[1000] = 0xFF;

    /* Test small sizes */
    TEST_ASSERT(g_fsd_simd.is_one(ones, 1), "1-byte one");
    TEST_ASSERT(g_fsd_simd.is_one(ones, 7), "7-byte one (unaligned)");
    TEST_ASSERT(g_fsd_simd.is_one(ones, 16), "16-byte one");
    TEST_ASSERT(g_fsd_simd.is_one(ones, 31), "31-byte one");
    TEST_ASSERT(g_fsd_simd.is_one(ones, 33), "33-byte one");
    TEST_ASSERT(g_fsd_simd.is_one(ones, 512), "512-byte one");

    printf("    PASS\n");
    return 0;
}

/* Test count_matches with various patterns */
static int test_count_matches(void) {
    printf("  Testing count_matches...\n");

    uint8_t a[4096];
    uint8_t b[4096];

    /* All matching */
    for (size_t i = 0; i < sizeof(a); i++) {
        a[i] = b[i] = (uint8_t)(i & 0xFF);
    }
    size_t count = g_fsd_simd.count_matches(a, b, sizeof(a));
    TEST_ASSERT(count == sizeof(a), "All bytes should match");

    /* None matching */
    for (size_t i = 0; i < sizeof(a); i++) {
        a[i] = (uint8_t)(i & 0xFF);
        b[i] = (uint8_t)((i + 1) & 0xFF);
    }
    count = g_fsd_simd.count_matches(a, b, sizeof(a));
    TEST_ASSERT(count == 0, "No bytes should match");

    /* 50% matching (alternating) */
    for (size_t i = 0; i < sizeof(a); i++) {
        a[i] = (uint8_t)(i & 0xFF);
        b[i] = (i % 2 == 0) ? a[i] : (uint8_t)~a[i];
    }
    count = g_fsd_simd.count_matches(a, b, sizeof(a));
    TEST_ASSERT(count == sizeof(a) / 2, "Half bytes should match");

    /* First half matches, second half differs */
    memset(a, 0xAA, sizeof(a));
    memcpy(b, a, sizeof(a) / 2);
    memset(b + sizeof(a) / 2, 0xBB, sizeof(a) / 2);
    count = g_fsd_simd.count_matches(a, b, sizeof(a));
    TEST_ASSERT(count == sizeof(a) / 2, "First half should match");

    /* Test unaligned sizes */
    TEST_ASSERT(g_fsd_simd.count_matches(a, a, 1) == 1, "1-byte match");
    TEST_ASSERT(g_fsd_simd.count_matches(a, a, 7) == 7, "7-byte match");
    TEST_ASSERT(g_fsd_simd.count_matches(a, a, 15) == 15, "15-byte match");
    TEST_ASSERT(g_fsd_simd.count_matches(a, a, 31) == 31, "31-byte match");
    TEST_ASSERT(g_fsd_simd.count_matches(a, a, 33) == 33, "33-byte match");
    TEST_ASSERT(g_fsd_simd.count_matches(a, a, 100) == 100, "100-byte match");

    /* Test single mismatch at various positions */
    memset(a, 0x55, sizeof(a));
    memcpy(b, a, sizeof(a));

    b[0] = 0x56;
    TEST_ASSERT(g_fsd_simd.count_matches(a, b, sizeof(a)) == sizeof(a) - 1,
                "Mismatch at position 0");
    b[0] = 0x55;

    b[100] = 0x56;
    TEST_ASSERT(g_fsd_simd.count_matches(a, b, sizeof(a)) == sizeof(a) - 1,
                "Mismatch at position 100");
    b[100] = 0x55;

    b[4095] = 0x56;
    TEST_ASSERT(g_fsd_simd.count_matches(a, b, sizeof(a)) == sizeof(a) - 1,
                "Mismatch at position 4095");

    printf("    PASS\n");
    return 0;
}

/* Compare SIMD vs scalar implementations */
static int test_simd_vs_scalar(void) {
    printf("  Testing SIMD vs scalar consistency...\n");

    /* Get current backend */
    fsd_simd_caps_t original_caps = fsd_simd_get_caps();
    const char *original_name = fsd_simd_get_name();

    printf("    Current backend: %s\n", original_name);

    if (original_caps == FSD_SIMD_NONE) {
        printf("    Skipping (already using scalar)\n");
        return 0;
    }

    /* Test data */
    uint8_t data_zero[4096];
    uint8_t data_one[4096];
    uint8_t data_pattern[4096];
    uint8_t data_similar[4096];

    memset(data_zero, 0, sizeof(data_zero));
    memset(data_one, 0xFF, sizeof(data_one));
    for (size_t i = 0; i < sizeof(data_pattern); i++) {
        data_pattern[i] = (uint8_t)(i & 0xFF);
    }
    memcpy(data_similar, data_pattern, sizeof(data_similar));
    data_similar[100] = ~data_similar[100];  /* 1 difference */

    /* Save SIMD results */
    bool simd_is_zero = g_fsd_simd.is_zero(data_zero, sizeof(data_zero));
    bool simd_is_one = g_fsd_simd.is_one(data_one, sizeof(data_one));
    size_t simd_match_all = g_fsd_simd.count_matches(data_pattern, data_pattern, sizeof(data_pattern));
    size_t simd_match_one_diff = g_fsd_simd.count_matches(data_pattern, data_similar, sizeof(data_pattern));

    /* Switch to scalar */
    fsd_simd_force_scalar();
    TEST_ASSERT(g_fsd_simd.caps == FSD_SIMD_NONE, "Should be scalar mode");

    /* Compare scalar results */
    bool scalar_is_zero = g_fsd_simd.is_zero(data_zero, sizeof(data_zero));
    bool scalar_is_one = g_fsd_simd.is_one(data_one, sizeof(data_one));
    size_t scalar_match_all = g_fsd_simd.count_matches(data_pattern, data_pattern, sizeof(data_pattern));
    size_t scalar_match_one_diff = g_fsd_simd.count_matches(data_pattern, data_similar, sizeof(data_pattern));

    TEST_ASSERT(simd_is_zero == scalar_is_zero,
                "is_zero results should match");
    TEST_ASSERT(simd_is_one == scalar_is_one,
                "is_one results should match");
    TEST_ASSERT(simd_match_all == scalar_match_all,
                "count_matches (all) results should match");
    TEST_ASSERT(simd_match_one_diff == scalar_match_one_diff,
                "count_matches (one diff) results should match");

    /* Restore original SIMD settings */
    fsd_simd_init();

    printf("    PASS (SIMD and scalar produce identical results)\n");
    return 0;
}

/* Test edge cases and boundary conditions */
static int test_edge_cases(void) {
    printf("  Testing edge cases...\n");

    /* Empty block (size 0) */
    uint8_t dummy[1] = {0};
    TEST_ASSERT(g_fsd_simd.is_zero(dummy, 0), "Empty block is zero");
    TEST_ASSERT(g_fsd_simd.is_one(dummy, 0), "Empty block is one");
    TEST_ASSERT(g_fsd_simd.count_matches(dummy, dummy, 0) == 0, "Empty match count is 0");

    /* Single byte */
    uint8_t zero = 0;
    uint8_t one = 0xFF;
    uint8_t val = 0x42;

    TEST_ASSERT(g_fsd_simd.is_zero(&zero, 1), "Single zero byte");
    TEST_ASSERT(!g_fsd_simd.is_zero(&one, 1), "Single non-zero byte");
    TEST_ASSERT(!g_fsd_simd.is_zero(&val, 1), "Single value byte");

    TEST_ASSERT(g_fsd_simd.is_one(&one, 1), "Single 0xFF byte");
    TEST_ASSERT(!g_fsd_simd.is_one(&zero, 1), "Single zero is not one");
    TEST_ASSERT(!g_fsd_simd.is_one(&val, 1), "Single value is not one");

    TEST_ASSERT(g_fsd_simd.count_matches(&val, &val, 1) == 1, "Single byte matches itself");
    TEST_ASSERT(g_fsd_simd.count_matches(&zero, &one, 1) == 0, "Different single bytes");

    /* Power-of-two sizes that align with SIMD vectors */
    uint8_t buf[8192];
    memset(buf, 0, sizeof(buf));

    size_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        size_t sz = sizes[i];
        TEST_ASSERT(g_fsd_simd.is_zero(buf, sz), "Aligned size is_zero");
        TEST_ASSERT(g_fsd_simd.count_matches(buf, buf, sz) == sz, "Aligned size count");
    }

    memset(buf, 0xFF, sizeof(buf));
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        size_t sz = sizes[i];
        TEST_ASSERT(g_fsd_simd.is_one(buf, sz), "Aligned size is_one");
    }

    /* Non-aligned sizes */
    size_t unaligned[] = {9, 17, 31, 63, 127, 255, 513, 1000, 4095};
    memset(buf, 0, sizeof(buf));
    for (size_t i = 0; i < sizeof(unaligned) / sizeof(unaligned[0]); i++) {
        size_t sz = unaligned[i];
        TEST_ASSERT(g_fsd_simd.is_zero(buf, sz), "Unaligned size is_zero");
        TEST_ASSERT(g_fsd_simd.count_matches(buf, buf, sz) == sz, "Unaligned count");
    }

    memset(buf, 0xFF, sizeof(buf));
    for (size_t i = 0; i < sizeof(unaligned) / sizeof(unaligned[0]); i++) {
        size_t sz = unaligned[i];
        TEST_ASSERT(g_fsd_simd.is_one(buf, sz), "Unaligned size is_one");
    }

    printf("    PASS\n");
    return 0;
}

/* Test with random data to catch any SIMD bugs */
static int test_random_data(void) {
    printf("  Testing with random data...\n");

    uint8_t a[4096];
    uint8_t b[4096];

    /* Generate pseudo-random data */
    unsigned int seed = 12345;
    for (size_t i = 0; i < sizeof(a); i++) {
        seed = seed * 1103515245 + 12345;
        a[i] = (uint8_t)(seed >> 16);
    }

    /* Copy with some modifications */
    memcpy(b, a, sizeof(b));

    /* Make 100 random positions different */
    seed = 67890;
    size_t expected_matches = sizeof(a);
    for (int i = 0; i < 100; i++) {
        seed = seed * 1103515245 + 12345;
        size_t pos = (seed >> 16) % sizeof(a);
        if (b[pos] == a[pos]) {
            b[pos] = (uint8_t)~a[pos];
            expected_matches--;
        }
    }

    size_t count = g_fsd_simd.count_matches(a, b, sizeof(a));
    TEST_ASSERT(count == expected_matches,
                "Should count correct matches in random data");

    /* Neither should be all zeros or all ones */
    TEST_ASSERT(!g_fsd_simd.is_zero(a, sizeof(a)), "Random data not all zero");
    TEST_ASSERT(!g_fsd_simd.is_one(a, sizeof(a)), "Random data not all one");

    printf("    PASS\n");
    return 0;
}

int main(void) {
    printf("Running SIMD tests...\n");

    fsd_init();

    printf("SIMD backend: %s (caps=0x%x)\n",
           fsd_simd_get_name(), fsd_simd_get_caps());

    int failures = 0;
    failures += test_is_zero();
    failures += test_is_one();
    failures += test_count_matches();
    failures += test_simd_vs_scalar();
    failures += test_edge_cases();
    failures += test_random_data();

    fsd_cleanup();

    if (failures == 0) {
        printf("All SIMD tests passed!\n");
        return 0;
    } else {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
}
