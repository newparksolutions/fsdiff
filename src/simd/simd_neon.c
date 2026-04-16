/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file simd_neon.c
 * @brief ARM NEON optimized implementations
 */

#ifdef FSDIFF_HAS_NEON

#include "simd_dispatch.h"
#include <arm_neon.h>
#include <string.h>

bool fsd_neon_is_zero(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    const uint8x16_t zero = vdupq_n_u8(0);

    /* Process 16 bytes at a time */
    size_t vec_count = len / 16;

    for (size_t i = 0; i < vec_count; i++) {
        uint8x16_t v = vld1q_u8(bytes + i * 16);
        uint8x16_t cmp = vceqq_u8(v, zero);

        /* Check if all bytes matched */
        uint64x2_t as64 = vreinterpretq_u64_u8(cmp);
        if (vgetq_lane_u64(as64, 0) != 0xFFFFFFFFFFFFFFFFULL ||
            vgetq_lane_u64(as64, 1) != 0xFFFFFFFFFFFFFFFFULL) {
            return false;
        }
    }

    /* Check remaining bytes */
    for (size_t i = vec_count * 16; i < len; i++) {
        if (bytes[i] != 0) {
            return false;
        }
    }

    return true;
}

bool fsd_neon_is_one(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    const uint8x16_t ones = vdupq_n_u8(0xFF);

    /* Process 16 bytes at a time */
    size_t vec_count = len / 16;

    for (size_t i = 0; i < vec_count; i++) {
        uint8x16_t v = vld1q_u8(bytes + i * 16);
        uint8x16_t cmp = vceqq_u8(v, ones);

        /* Check if all bytes matched */
        uint64x2_t as64 = vreinterpretq_u64_u8(cmp);
        if (vgetq_lane_u64(as64, 0) != 0xFFFFFFFFFFFFFFFFULL ||
            vgetq_lane_u64(as64, 1) != 0xFFFFFFFFFFFFFFFFULL) {
            return false;
        }
    }

    /* Check remaining bytes */
    for (size_t i = vec_count * 16; i < len; i++) {
        if (bytes[i] != 0xFF) {
            return false;
        }
    }

    return true;
}

size_t fsd_neon_count_matches(const uint8_t *a, const uint8_t *b, size_t len) {
    size_t count = 0;
    size_t vec_count = len / 16;

    for (size_t i = 0; i < vec_count; i++) {
        uint8x16_t va = vld1q_u8(a + i * 16);
        uint8x16_t vb = vld1q_u8(b + i * 16);

        /* Compare bytes: 0xFF where equal, 0x00 where different */
        uint8x16_t cmp = vceqq_u8(va, vb);

        /* Count matching bytes using horizontal add */
        /* Each 0xFF = -1 as signed, so we negate to get 1 for matches */
        /* Or: treat 0xFF as 1 by right-shifting by 7 (gets 0x01 from 0xFF) */
        uint8x16_t ones = vshrq_n_u8(cmp, 7);

        /* Horizontal sum: add all bytes in the vector */
        uint64x2_t sum64 = vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(ones)));
        count += vgetq_lane_u64(sum64, 0) + vgetq_lane_u64(sum64, 1);
    }

    /* Handle remaining bytes */
    for (size_t i = vec_count * 16; i < len; i++) {
        if (a[i] == b[i]) {
            count++;
        }
    }

    return count;
}

#define FSDIFF_NEON_ENABLED 1
#endif /* FSDIFF_HAS_NEON */

/* Ensure non-empty translation unit */
#ifndef FSDIFF_NEON_ENABLED
typedef int fsdiff_neon_placeholder_t;
#endif
