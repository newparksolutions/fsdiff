/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file stage_partial.c
 * @brief Stage 3: Local search partial matching implementation
 *
 * This stage finds approximate matches by searching nearby offsets:
 * 1. For each unmatched block, search offsets -32768 to +32768
 * 2. If match found, extend to adjacent unmatched blocks
 * 3. If no match but previous block was relocated, search relative to that
 */

#include "stage_partial.h"
#include "../simd/simd_dispatch.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Search range for partial matching */
#define SEARCH_RANGE 32768

struct fsd_partial_stage {
    size_t block_size;
    float threshold;        /* Fraction of bytes that must match (0.0-1.0) */
    uint64_t src_blocks;
    uint64_t src_size;
    int verbose;
};

/**
 * Count matching bytes between two blocks using SIMD when available.
 *
 * @param a     First block
 * @param b     Second block
 * @param size  Block size in bytes
 * @return      Number of matching bytes
 */
static size_t count_matching_bytes(const uint8_t *a, const uint8_t *b, size_t size) {
    if (g_fsd_simd.count_matches) {
        return g_fsd_simd.count_matches(a, b, size);
    }

    /* Scalar fallback */
    size_t count = 0;
    for (size_t i = 0; i < size; i++) {
        if (a[i] == b[i]) {
            count++;
        }
    }
    return count;
}

fsd_error_t fsd_partial_stage_create(fsd_partial_stage_t **stage_out,
                                     size_t block_size,
                                     float threshold) {
    if (!stage_out || block_size == 0) {
        return FSD_ERR_INVALID_ARG;
    }

    fsd_partial_stage_t *stage = calloc(1, sizeof(fsd_partial_stage_t));
    if (!stage) {
        return FSD_ERR_OUT_OF_MEMORY;
    }

    stage->block_size = block_size;
    stage->threshold = (threshold > 0.0f && threshold < 1.0f) ? threshold : 0.5f;
    stage->src_blocks = 0;
    stage->src_size = 0;

    *stage_out = stage;
    return FSD_SUCCESS;
}

fsd_error_t fsd_partial_stage_build_index(fsd_partial_stage_t *stage,
                                          const uint8_t *src,
                                          uint64_t src_blocks) {
    if (!stage || !src) {
        return FSD_ERR_INVALID_ARG;
    }

    stage->src_blocks = src_blocks;
    stage->src_size = src_blocks * stage->block_size;

    if (stage->verbose) {
        fprintf(stderr, "[Partial] Index built for %lu source blocks (%zu bytes)\n",
                (unsigned long)src_blocks, stage->src_size);
    }

    /* No index needed for local search approach */
    return FSD_SUCCESS;
}

/**
 * Search for best matching source position within an offset range.
 *
 * @param src              Source data
 * @param src_size         Source data size
 * @param dest_block       Destination block to match
 * @param block_size       Block size
 * @param center_pos       Center byte position to search around
 * @param min_offset       Minimum offset from center (can be negative)
 * @param max_offset       Maximum offset from center
 * @param threshold_count  Minimum matching bytes required
 * @param best_count_out   Output: best match count found
 * @return                 Best offset found, or -1 if no match above threshold
 */
static int64_t find_best_match(const uint8_t *src, size_t src_size,
                                const uint8_t *dest_block, size_t block_size,
                                int64_t center_pos, int64_t min_offset, int64_t max_offset,
                                size_t threshold_count, size_t *best_count_out) {
    int64_t best_offset = -1;
    /* Initialize to threshold-1 so matches at exactly threshold are accepted */
    size_t best_count = (threshold_count > 0) ? threshold_count - 1 : 0;

    for (int64_t offset = min_offset; offset <= max_offset; offset++) {
        int64_t src_pos = center_pos + offset;

        /* Bounds check */
        if (src_pos < 0 || (size_t)(src_pos + block_size) > src_size) {
            continue;
        }

        size_t match_count = count_matching_bytes(src + src_pos, dest_block, block_size);

        if (match_count > best_count) {
            best_count = match_count;
            best_offset = offset;
        }
    }

    if (best_count_out) {
        *best_count_out = best_count;
    }
    return best_offset;
}

fsd_error_t fsd_partial_stage_run(fsd_partial_stage_t *stage,
                                  fsd_block_tracker_t *tracker,
                                  const uint8_t *src,
                                  const uint8_t *dest,
                                  fsd_memory_pool_t *delta_pool) {
    if (!stage || !tracker || !src || !dest) {
        return FSD_ERR_INVALID_ARG;
    }

    size_t block_size = stage->block_size;
    size_t threshold_count = (size_t)(stage->threshold * block_size);
    size_t src_size = stage->src_size;

    uint64_t unmatched_total = fsd_block_tracker_unmatched_count(tracker);
    uint64_t unmatched_processed = 0;
    uint64_t matches_found = 0;
    uint64_t extended_matches = 0;

    if (stage->verbose) {
        fprintf(stderr, "[Partial] Processing %lu unmatched blocks (threshold: %zu/%zu bytes)\n",
                (unsigned long)unmatched_total, threshold_count, block_size);
        fprintf(stderr, "[Partial] Search range: +/-%d bytes\n", SEARCH_RANGE);
    }

    /* Track previous block's relocation info */
    bool prev_was_relocated = false;
    int64_t prev_src_pos = 0;

    for (uint64_t dest_idx = 0; dest_idx < tracker->count; dest_idx++) {
        /* Check previous block state before processing current */
        if (dest_idx > 0) {
            const fsd_block_state_t *prev = fsd_block_tracker_get(tracker, dest_idx - 1);
            if (prev->match_type == FSD_MATCH_RELOCATE ||
                prev->match_type == FSD_MATCH_PARTIAL) {
                prev_was_relocated = true;
                /* Include byte_offset for partial matches */
                prev_src_pos = prev->src_index * block_size + prev->byte_offset;
            } else {
                prev_was_relocated = false;
            }
        }

        if (!fsd_block_tracker_is_unmatched(tracker, dest_idx)) {
            continue;
        }

        unmatched_processed++;

        /* Progress update */
        if (stage->verbose && (unmatched_processed % 100 == 0 ||
            unmatched_processed == unmatched_total)) {
            int pct = (unmatched_total > 0) ? (int)((unmatched_processed * 100) / unmatched_total) : 100;
            fprintf(stderr, "\r[Partial] Block %lu/%lu (%d%%) - %lu matches, %lu extended",
                    (unsigned long)unmatched_processed,
                    (unsigned long)unmatched_total,
                    pct,
                    (unsigned long)matches_found,
                    (unsigned long)extended_matches);
            fflush(stderr);
        }

        const uint8_t *dest_block = dest + (dest_idx * block_size);
        int64_t dest_pos = dest_idx * block_size;

        /* Stage 1: Search around current position */
        size_t best_count = 0;
        int64_t best_offset = find_best_match(src, src_size, dest_block, block_size,
                                               dest_pos, -SEARCH_RANGE, SEARCH_RANGE,
                                               threshold_count, &best_count);

        /* Stage 2: If no match and previous was relocated, search relative to that */
        if (best_count < threshold_count && prev_was_relocated) {
            int64_t reloc_base = prev_src_pos + block_size;
            size_t reloc_count = 0;
            int64_t reloc_offset = find_best_match(src, src_size, dest_block, block_size,
                                                    reloc_base, 0, SEARCH_RANGE,
                                                    threshold_count, &reloc_count);
            if (reloc_count >= threshold_count && reloc_count > best_count) {
                best_offset = (reloc_base + reloc_offset) - dest_pos;
                best_count = reloc_count;
            }
        }

        if (best_count >= threshold_count) {
            /* Found a match */
            int64_t src_pos = dest_pos + best_offset;
            uint64_t src_block_idx = src_pos / block_size;
            int64_t byte_offset = src_pos % block_size;

            /* Compute delta */
            uint8_t *delta = fsd_pool_alloc(delta_pool, block_size);
            if (delta) {
                const uint8_t *src_block = src + src_pos;
                for (size_t i = 0; i < block_size; i++) {
                    delta[i] = dest_block[i] - src_block[i];
                }

                fsd_block_tracker_set_match(tracker, dest_idx,
                                            FSD_MATCH_PARTIAL, src_block_idx);
                fsd_block_tracker_set_delta(tracker, dest_idx,
                                            byte_offset, delta, block_size);
                matches_found++;

                /* Try to extend match to subsequent unmatched blocks */
                for (uint64_t ext_idx = dest_idx + 1; ext_idx < tracker->count; ext_idx++) {
                    if (!fsd_block_tracker_is_unmatched(tracker, ext_idx)) {
                        break;
                    }

                    int64_t ext_src_pos = (ext_idx * block_size) + best_offset;
                    if (ext_src_pos < 0 || (size_t)(ext_src_pos + block_size) > src_size) {
                        break;
                    }

                    const uint8_t *ext_dest_block = dest + (ext_idx * block_size);
                    const uint8_t *ext_src_block = src + ext_src_pos;

                    size_t ext_count = count_matching_bytes(ext_src_block, ext_dest_block, block_size);
                    if (ext_count < threshold_count) {
                        break;
                    }

                    /* Extend the match */
                    uint8_t *ext_delta = fsd_pool_alloc(delta_pool, block_size);
                    if (!ext_delta) {
                        break;
                    }

                    for (size_t i = 0; i < block_size; i++) {
                        ext_delta[i] = ext_dest_block[i] - ext_src_block[i];
                    }

                    uint64_t ext_src_block_idx = ext_src_pos / block_size;
                    int64_t ext_byte_offset = ext_src_pos % block_size;

                    fsd_block_tracker_set_match(tracker, ext_idx,
                                                FSD_MATCH_PARTIAL, ext_src_block_idx);
                    fsd_block_tracker_set_delta(tracker, ext_idx,
                                                ext_byte_offset, ext_delta, block_size);
                    extended_matches++;
                }
            }
        }
    }

    if (stage->verbose) {
        fprintf(stderr, "\n[Partial] Complete: %lu matches found, %lu extended\n",
                (unsigned long)matches_found, (unsigned long)extended_matches);
    }

    return FSD_SUCCESS;
}

void fsd_partial_stage_set_fft_sigma(fsd_partial_stage_t *stage, float sigma) {
    /* Not used in local search approach */
    (void)stage;
    (void)sigma;
}

void fsd_partial_stage_set_verbose(fsd_partial_stage_t *stage, int verbose) {
    if (stage) {
        stage->verbose = verbose;
    }
}

void fsd_partial_stage_destroy(fsd_partial_stage_t *stage) {
    free(stage);
}
