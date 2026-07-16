/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file simd_scalar.c
 * @brief Scalar fallback implementations for SIMD operations
 */

#include "simd_dispatch.h"
#include <string.h>

bool fsd_scalar_is_zero(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;

    /* Process 8 bytes at a time. Load via memcpy rather than a uint64_t*
     * cast: callers may pass unaligned pointers (the partial stage scans
     * every byte offset), so a direct cast is a strict-aliasing violation
     * and faults on targets that require aligned 64-bit loads (e.g. ARM32). */
    size_t word_count = len / 8;

    for (size_t i = 0; i < word_count; i++) {
        uint64_t word;
        memcpy(&word, bytes + i * 8, sizeof(word));
        if (word != 0) {
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

    /* Process 8 bytes at a time; load via memcpy (see fsd_scalar_is_zero). */
    size_t word_count = len / 8;
    const uint64_t all_ones = 0xFFFFFFFFFFFFFFFFULL;

    for (size_t i = 0; i < word_count; i++) {
        uint64_t word;
        memcpy(&word, bytes + i * 8, sizeof(word));
        if (word != all_ones) {
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

    /* Process 8 bytes at a time - XOR then count zeros. Load via memcpy
     * rather than a uint64_t* cast: this function is called with unaligned
     * pointers (the partial stage scans every byte offset around a block),
     * so a cast is a strict-aliasing violation and faults on targets that
     * require aligned 64-bit loads (e.g. ARM32). */
    size_t word_count = len / 8;

    for (size_t i = 0; i < word_count; i++) {
        uint64_t wa_i, wb_i;
        memcpy(&wa_i, a + i * 8, sizeof(wa_i));
        memcpy(&wb_i, b + i * 8, sizeof(wb_i));
        uint64_t diff = wa_i ^ wb_i;
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
