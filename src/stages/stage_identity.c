/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file stage_identity.c
 * @brief Stage 1: Identity matching implementation
 */

#include "stage_identity.h"
#include "../core/crc32.h"
#include "../simd/simd_dispatch.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Check if block is all zeros */
static int is_zero_block(const uint8_t *block, size_t size) {
    /* Use SIMD dispatch if initialized */
    if (g_fsd_simd.is_zero) {
        return g_fsd_simd.is_zero(block, size);
    }

    /* Scalar fallback */
    const uint64_t *ptr = (const uint64_t *)block;
    size_t count = size / sizeof(uint64_t);

    for (size_t i = 0; i < count; i++) {
        if (ptr[i] != 0) return 0;
    }

    /* Check remaining bytes */
    const uint8_t *tail = block + (count * sizeof(uint64_t));
    size_t remaining = size % sizeof(uint64_t);
    for (size_t i = 0; i < remaining; i++) {
        if (tail[i] != 0) return 0;
    }

    return 1;
}

/* Check if block is all 0xFF */
static int is_one_block(const uint8_t *block, size_t size) {
    /* Use SIMD dispatch if initialized */
    if (g_fsd_simd.is_one) {
        return g_fsd_simd.is_one(block, size);
    }

    /* Scalar fallback */
    const uint64_t *ptr = (const uint64_t *)block;
    const uint64_t all_ones = ~0ULL;
    size_t count = size / sizeof(uint64_t);

    for (size_t i = 0; i < count; i++) {
        if (ptr[i] != all_ones) return 0;
    }

    /* Check remaining bytes */
    const uint8_t *tail = block + (count * sizeof(uint64_t));
    size_t remaining = size % sizeof(uint64_t);
    for (size_t i = 0; i < remaining; i++) {
        if (tail[i] != 0xFF) return 0;
    }

    return 1;
}

fsd_error_t fsd_identity_stage_create(fsd_identity_stage_t **stage_out,
                                      size_t block_size) {
    if (!stage_out || block_size == 0) {
        return FSD_ERR_INVALID_ARG;
    }

    fsd_identity_stage_t *stage = calloc(1, sizeof(fsd_identity_stage_t));
    if (!stage) {
        return FSD_ERR_OUT_OF_MEMORY;
    }

    stage->block_size = block_size;

    *stage_out = stage;
    return FSD_SUCCESS;
}

fsd_error_t fsd_identity_stage_run(fsd_identity_stage_t *stage,
                                   fsd_block_tracker_t *tracker,
                                   const uint8_t *src,
                                   uint64_t src_blocks,
                                   const uint8_t *dest,
                                   uint64_t dest_blocks) {
    if (!stage || !tracker || !dest) {
        return FSD_ERR_INVALID_ARG;
    }

    size_t block_size = stage->block_size;
    uint64_t common_blocks = (src_blocks < dest_blocks) ? src_blocks : dest_blocks;

    /* Counters for verbose output */
    uint64_t identity_count = 0;
    uint64_t zero_count = 0;
    uint64_t one_count = 0;
    uint64_t unmatched_count = 0;

    if (stage->verbose) {
        fprintf(stderr, "[Identity] Processing %lu common blocks, %lu dest-only blocks\n",
                (unsigned long)common_blocks, (unsigned long)(dest_blocks - common_blocks));
    }

    /* Process blocks that exist in both source and destination */
    for (uint64_t i = 0; i < common_blocks; i++) {
        const uint8_t *src_block = src + (i * block_size);
        const uint8_t *dest_block = dest + (i * block_size);

        /* Progress update */
        if (stage->verbose && (i % 100000 == 0 || i == common_blocks - 1)) {
            int pct = (dest_blocks > 0) ? (int)((i * 100) / dest_blocks) : 100;
            fprintf(stderr, "\r[Identity] Block %lu/%lu (%d%%)",
                    (unsigned long)i, (unsigned long)dest_blocks, pct);
            fflush(stderr);
        }

        /* Check for special blocks (zero/one) first - these are more efficient
         * to encode than identity matches since they don't require source data */
        if (is_zero_block(dest_block, block_size)) {
            fsd_block_tracker_set_match(tracker, i, FSD_MATCH_ZERO, 0);
            zero_count++;
            continue;
        }

        if (is_one_block(dest_block, block_size)) {
            fsd_block_tracker_set_match(tracker, i, FSD_MATCH_ONE, 0);
            one_count++;
            continue;
        }

        /* Check for identity match (most common case for non-special blocks) */
        if (memcmp(src_block, dest_block, block_size) == 0) {
            fsd_block_tracker_set_match(tracker, i, FSD_MATCH_IDENTITY, i);
            identity_count++;
            continue;
        }

        /* Compute and store CRC32 for later stages */
        uint32_t crc = fsd_crc32(dest_block, block_size);
        fsd_block_tracker_set_crc32(tracker, i, crc);
        unmatched_count++;
    }

    /* Process blocks that only exist in destination (beyond source size) */
    for (uint64_t i = common_blocks; i < dest_blocks; i++) {
        const uint8_t *dest_block = dest + (i * block_size);

        /* Progress update */
        if (stage->verbose && (i % 100000 == 0 || i == dest_blocks - 1)) {
            int pct = (dest_blocks > 0) ? (int)((i * 100) / dest_blocks) : 100;
            fprintf(stderr, "\r[Identity] Block %lu/%lu (%d%%)",
                    (unsigned long)i, (unsigned long)dest_blocks, pct);
            fflush(stderr);
        }

        /* Check for special blocks */
        if (is_zero_block(dest_block, block_size)) {
            fsd_block_tracker_set_match(tracker, i, FSD_MATCH_ZERO, 0);
            zero_count++;
            continue;
        }

        if (is_one_block(dest_block, block_size)) {
            fsd_block_tracker_set_match(tracker, i, FSD_MATCH_ONE, 0);
            one_count++;
            continue;
        }

        /* Compute CRC32 for relocation stage */
        uint32_t crc = fsd_crc32(dest_block, block_size);
        fsd_block_tracker_set_crc32(tracker, i, crc);
        unmatched_count++;
    }

    if (stage->verbose) {
        fprintf(stderr, "\n[Identity] Complete: identity=%lu, zero=%lu, one=%lu, unmatched=%lu\n",
                (unsigned long)identity_count, (unsigned long)zero_count,
                (unsigned long)one_count, (unsigned long)unmatched_count);
    }

    return FSD_SUCCESS;
}

void fsd_identity_stage_set_verbose(fsd_identity_stage_t *stage, int verbose) {
    if (stage) {
        stage->verbose = verbose;
    }
}

void fsd_identity_stage_destroy(fsd_identity_stage_t *stage) {
    free(stage);
}
