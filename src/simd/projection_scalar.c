/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file projection_scalar.c
 * @brief Scalar fallback implementations for SIMD operations
 */

#include "simd_dispatch.h"
#include <string.h>

bool fsd_scalar_is_zero(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;

    /* Process 8 bytes at a time */
    const uint64_t *words = (const uint64_t *)data;
    size_t word_count = len / 8;

    for (size_t i = 0; i < word_count; i++) {
        if (words[i] != 0) {
            return false;
        }
    }

    /* Check remaining bytes */
    for (size_t i = word_count * 8; i < len; i++) {
        if (bytes[i] != 0) {
            return false;
        }
    }

    return true;
}

bool fsd_scalar_is_one(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;

    /* Process 8 bytes at a time */
    const uint64_t *words = (const uint64_t *)data;
    size_t word_count = len / 8;
    const uint64_t all_ones = 0xFFFFFFFFFFFFFFFFULL;

    for (size_t i = 0; i < word_count; i++) {
        if (words[i] != all_ones) {
            return false;
        }
    }

    /* Check remaining bytes */
    for (size_t i = word_count * 8; i < len; i++) {
        if (bytes[i] != 0xFF) {
            return false;
        }
    }

    return true;
}

size_t fsd_scalar_count_matches(const uint8_t *a, const uint8_t *b, size_t len) {
    size_t count = 0;

    /* Process 8 bytes at a time - XOR then count zeros */
    const uint64_t *wa = (const uint64_t *)a;
    const uint64_t *wb = (const uint64_t *)b;
    size_t word_count = len / 8;

    for (size_t i = 0; i < word_count; i++) {
        uint64_t diff = wa[i] ^ wb[i];
        if (diff == 0) {
            count += 8;
        } else {
            /* Count zero bytes in diff */
            for (int j = 0; j < 8; j++) {
                if ((diff & 0xFF) == 0) {
                    count++;
                }
                diff >>= 8;
            }
        }
    }

    /* Handle remaining bytes */
    for (size_t i = word_count * 8; i < len; i++) {
        if (a[i] == b[i]) {
            count++;
        }
    }

    return count;
}
