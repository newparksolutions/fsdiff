/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file stage_partial.h
 * @brief Stage 3: FFT-based partial matching for approximate block matches
 */

#ifndef FSDIFF_STAGE_PARTIAL_H
#define FSDIFF_STAGE_PARTIAL_H

#include <fsdiff/types.h>
#include <fsdiff/error.h>
#include "../core/block_tracker.h"
#include "../core/memory_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Partial matching stage handle */
typedef struct fsd_partial_stage fsd_partial_stage_t;

/**
 * Create FFT-based partial matching stage.
 *
 * @param stage_out   Output pointer for stage
 * @param block_size  Block size in bytes
 * @param threshold   Correlation threshold (0.0-1.0)
 * @return            FSD_SUCCESS or error code
 */
fsd_error_t fsd_partial_stage_create(fsd_partial_stage_t **stage_out,
                                     size_t block_size,
                                     float threshold);

/**
 * Build FFT index from source blocks.
 *
 * Precomputes projections onto coprime subspaces and FFTs.
 *
 * @param stage       Stage handle
 * @param src         Source image data
 * @param src_blocks  Number of blocks in source
 * @return            FSD_SUCCESS or error code
 */
fsd_error_t fsd_partial_stage_build_index(fsd_partial_stage_t *stage,
                                          const uint8_t *src,
                                          uint64_t src_blocks);

/**
 * Run partial matching on unmatched blocks.
 *
 * Uses FFT-based correlation to find approximate matches:
 * 1. Project onto coprime moduli subspaces (8192, 6561, 3125)
 * 2. Compute cyclic correlation via FFT
 * 3. Reconstruct positions via Chinese Remainder Theorem
 * 4. Verify candidates with direct byte comparison
 *
 * @param stage       Stage handle
 * @param tracker     Block tracker with results from earlier stages
 * @param src         Source image data
 * @param dest        Destination image data
 * @param delta_pool  Memory pool for delta allocations
 * @return            FSD_SUCCESS or error code
 */
fsd_error_t fsd_partial_stage_run(fsd_partial_stage_t *stage,
                                  fsd_block_tracker_t *tracker,
                                  const uint8_t *src,
                                  const uint8_t *dest,
                                  fsd_memory_pool_t *delta_pool);

/**
 * Enable verbose output for debugging.
 *
 * When enabled, prints:
 * - Progress during projection computation
 * - Progress during block matching
 * - Candidate counts from CRT reconstruction
 *
 * @param stage    Stage handle
 * @param verbose  1 to enable, 0 to disable
 */
void fsd_partial_stage_set_verbose(fsd_partial_stage_t *stage, int verbose);

/**
 * Destroy partial matching stage.
 *
 * @param stage  Stage handle (NULL is safe)
 */
void fsd_partial_stage_destroy(fsd_partial_stage_t *stage);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_STAGE_PARTIAL_H */
