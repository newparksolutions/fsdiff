/**
 * @file projection_avx2.c
 * @brief AVX2 optimized implementations for x86-64
 */

#ifdef FSDIFF_HAS_AVX2
#define FSDIFF_AVX2_ENABLED 1

#include "simd_dispatch.h"
#include <immintrin.h>
#include <string.h>

bool fsd_avx2_is_zero(const void *data, size_t len) {
    const __m256i *vdata = (const __m256i *)data;
    const __m256i zero = _mm256_setzero_si256();

    /* Process 32 bytes at a time */
    size_t vec_count = len / 32;

    for (size_t i = 0; i < vec_count; i++) {
        __m256i v = _mm256_loadu_si256(&vdata[i]);
        __m256i cmp = _mm256_cmpeq_epi8(v, zero);
        int mask = _mm256_movemask_epi8(cmp);
        if (mask != (int)0xFFFFFFFF) {
            return false;
        }
    }

    /* Check remaining bytes */
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = vec_count * 32; i < len; i++) {
        if (bytes[i] != 0) {
            return false;
        }
    }

    return true;
}

bool fsd_avx2_is_one(const void *data, size_t len) {
    const __m256i *vdata = (const __m256i *)data;
    const __m256i ones = _mm256_set1_epi8((char)0xFF);

    /* Process 32 bytes at a time */
    size_t vec_count = len / 32;

    for (size_t i = 0; i < vec_count; i++) {
        __m256i v = _mm256_loadu_si256(&vdata[i]);
        __m256i cmp = _mm256_cmpeq_epi8(v, ones);
        int mask = _mm256_movemask_epi8(cmp);
        if (mask != (int)0xFFFFFFFF) {
            return false;
        }
    }

    /* Check remaining bytes */
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = vec_count * 32; i < len; i++) {
        if (bytes[i] != 0xFF) {
            return false;
        }
    }

    return true;
}

size_t fsd_avx2_count_matches(const uint8_t *a, const uint8_t *b, size_t len) {
    const __m256i *va = (const __m256i *)a;
    const __m256i *vb = (const __m256i *)b;

    size_t count = 0;
    size_t vec_count = len / 32;

    for (size_t i = 0; i < vec_count; i++) {
        __m256i xa = _mm256_loadu_si256(&va[i]);
        __m256i xb = _mm256_loadu_si256(&vb[i]);

        /* Compare bytes: 0xFF where equal, 0x00 where different */
        __m256i cmp = _mm256_cmpeq_epi8(xa, xb);

        /* Extract mask: bit set where bytes match */
        unsigned int mask = (unsigned int)_mm256_movemask_epi8(cmp);

        /* Count set bits = number of matching bytes */
#if defined(_MSC_VER)
        count += (size_t)__popcnt(mask);  /* MSVC intrinsic */
#elif defined(__GNUC__) || defined(__clang__)
        count += (size_t)__builtin_popcount(mask);  /* GCC/Clang intrinsic */
#else
        /* Portable fallback (should never happen with AVX2 support) */
        while (mask) {
            count += mask & 1;
            mask >>= 1;
        }
#endif
    }

    /* Handle remaining bytes */
    for (size_t i = vec_count * 32; i < len; i++) {
        if (a[i] == b[i]) {
            count++;
        }
    }

    return count;
}

#endif /* FSDIFF_HAS_AVX2 */

/* Ensure non-empty translation unit */
#ifndef FSDIFF_AVX2_ENABLED
typedef int fsdiff_avx2_placeholder_t;
#endif
