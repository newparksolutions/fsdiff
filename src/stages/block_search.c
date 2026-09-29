/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file block_search.c
 * @brief Shared byte-granular block search (moved from stage_partial.c)
 */

#include "block_search.h"

fsd_count_matches_fn fsd_block_search_count_fn(void) {
    return g_fsd_simd.count_matches ? g_fsd_simd.count_matches
                                    : fsd_scalar_count_matches;
}

int64_t fsd_block_search_best(const uint8_t *src, size_t src_size,
                              const uint8_t *dest_block, size_t block_size,
                              int64_t center_pos,
                              int64_t min_offset, int64_t max_offset,
                              size_t threshold_count,
                              fsd_count_matches_fn count_fn,
                              size_t *best_count_out) {
    int64_t best_offset = -1;
    /* Initialize to threshold-1 so matches at exactly threshold are accepted */
    size_t best_count = (threshold_count > 0) ? threshold_count - 1 : 0;

    /* Prescreen: almost all candidate offsets are nowhere near the threshold,
     * so sample two 128-byte windows (start and middle) and skip the full
     * count unless the sample reaches half the threshold's match rate. At the
     * default 0.5 threshold that bar is 64/256 sampled bytes; chance-level
     * data (~1/256 per byte) never gets close, while a genuine
     * above-threshold match failing it needs both windows to sit almost
     * entirely inside changed bytes. Disabled for small blocks, where the
     * sample would be most of the block anyway. */
    size_t prescreen_len = 0;
    size_t prescreen_min = 0;
    size_t prescreen_mid = 0;
    if (block_size >= 1024) {
        prescreen_len = 128;
        prescreen_mid = block_size / 2;
        /* Half the threshold rate, scaled to the 2*prescreen_len sampled
         * bytes: threshold_count * (2*prescreen_len) / (2*block_size). */
        prescreen_min = (threshold_count * prescreen_len) / block_size;
        if (prescreen_min == 0) {
            prescreen_len = 0;  /* threshold too low for the screen to reject */
        }
    }

    for (int64_t offset = min_offset; offset <= max_offset; offset++) {
        int64_t src_pos = center_pos + offset;

        /* Bounds check */
        if (src_pos < 0 || (size_t)(src_pos + block_size) > src_size) {
            continue;
        }

        if (prescreen_len) {
            size_t sample = count_fn(src + src_pos, dest_block, prescreen_len) +
                            count_fn(src + src_pos + prescreen_mid,
                                     dest_block + prescreen_mid, prescreen_len);
            if (sample < prescreen_min) {
                continue;
            }
        }

        size_t match_count = count_fn(src + src_pos, dest_block, block_size);

        if (match_count > best_count) {
            best_count = match_count;
            best_offset = offset;
            if (best_count == block_size) {
                break;  /* Perfect match; no remaining offset can beat it */
            }
        }
    }

    if (best_count_out) {
        *best_count_out = best_count;
    }
    return best_offset;
}
