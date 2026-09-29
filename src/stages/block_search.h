/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file block_search.h
 * @brief Shared byte-granular block search used by the partial and fsmap
 *        stages: SIMD-dispatched match counting with a sampled prescreen and
 *        perfect-match early exit.
 */

#ifndef FSDIFF_BLOCK_SEARCH_H
#define FSDIFF_BLOCK_SEARCH_H

#include <stdint.h>
#include <stddef.h>
#include "../simd/simd_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Resolve the byte-match counter once per run: the dispatch table is a
 * mutable global, so reading it inside search loops would force a reload
 * (and an un-inlinable indirect call setup) on every candidate offset.
 */
fsd_count_matches_fn fsd_block_search_count_fn(void);

/**
 * Search for the best matching source position within an offset range.
 *
 * Candidate offsets are prescreened with two 128-byte samples (start and
 * middle of the block) and only counted in full when the sample reaches half
 * the threshold's match rate; the sweep stops early on a perfect match.
 *
 * @param src              Source data
 * @param src_size         Source data size
 * @param dest_block       Destination block to match
 * @param block_size       Block size
 * @param center_pos       Center byte position to search around
 * @param min_offset       Minimum offset from center (can be negative)
 * @param max_offset       Maximum offset from center
 * @param threshold_count  Minimum matching bytes required
 * @param count_fn         Byte-match counter (from fsd_block_search_count_fn)
 * @param best_count_out   Output: best match count found
 * @return                 Best offset found, or -1 if no match above threshold
 */
int64_t fsd_block_search_best(const uint8_t *src, size_t src_size,
                              const uint8_t *dest_block, size_t block_size,
                              int64_t center_pos,
                              int64_t min_offset, int64_t max_offset,
                              size_t threshold_count,
                              fsd_count_matches_fn count_fn,
                              size_t *best_count_out);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_BLOCK_SEARCH_H */
