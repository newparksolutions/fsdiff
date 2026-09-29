/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file stage_fsmap.h
 * @brief Filesystem-aware matching stage.
 *
 * When both images are clean, feature-whitelisted ext2/3/4 filesystems, this
 * stage maps each still-unmatched destination block to (file path, file
 * offset) via the destination filesystem, looks the same path up in the
 * source filesystem, and verifies the resulting same-offset source position
 * hypothesis with an exact SIMD byte count plus a small directed sweep.
 * Runs between the relocation and partial stages; blocks it matches are
 * recorded as partial matches (first-writer-wins in the block tracker).
 *
 * If either image is not a supported filesystem the stage deactivates
 * itself and the pipeline behaves exactly as if it were disabled.
 */

#ifndef FSDIFF_STAGE_FSMAP_H
#define FSDIFF_STAGE_FSMAP_H

#include <fsdiff/types.h>
#include <fsdiff/error.h>
#include "../core/block_tracker.h"
#include "../core/memory_pool.h"
#include "../platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Filesystem-aware matching stage handle */
typedef struct fsd_fsmap_stage fsd_fsmap_stage_t;

/**
 * Create the stage.
 *
 * @param stage_out             Output pointer for stage
 * @param block_size            fsdiff block size in bytes
 * @param threshold             Match threshold (0.0-1.0; same semantics as
 *                              the partial stage)
 * @param search_radius_blocks  Directed-sweep radius around each hypothesis,
 *                              in blocks (<= 0 selects the default of 8)
 * @return                      FSD_SUCCESS or error code
 */
fsd_error_t fsd_fsmap_stage_create(fsd_fsmap_stage_t **stage_out,
                                   size_t block_size,
                                   float threshold,
                                   int search_radius_blocks);

/**
 * Probe both images and build the hypothesis list.
 *
 * Walks the source filesystem into a path-indexed extent table, then walks
 * the destination filesystem and emits one source-position hypothesis for
 * every unmatched tracker block that maps to the same path and file offset
 * in both filesystems. If either image fails the filesystem probe the stage
 * marks itself inactive and returns FSD_SUCCESS (run() becomes a no-op).
 *
 * @return FSD_SUCCESS or FSD_ERR_OUT_OF_MEMORY
 */
fsd_error_t fsd_fsmap_stage_build_index(fsd_fsmap_stage_t *stage,
                                        const uint8_t *src, size_t src_size,
                                        const uint8_t *dest, size_t dest_size,
                                        const fsd_block_tracker_t *tracker);

/**
 * Verify hypotheses and record matches.
 *
 * @param stage       Stage handle
 * @param tracker     Block tracker (matches recorded as FSD_MATCH_PARTIAL)
 * @param src         Source image data
 * @param dest        Destination image data
 * @param delta_pool  Memory pool for delta allocations
 * @return            FSD_SUCCESS, FSD_ERR_CANCELLED, or error code
 */
fsd_error_t fsd_fsmap_stage_run(fsd_fsmap_stage_t *stage,
                                fsd_block_tracker_t *tracker,
                                const uint8_t *src,
                                const uint8_t *dest,
                                fsd_memory_pool_t *delta_pool);

/**
 * Point the stage at up to two external cancel flags, checked periodically
 * inside run(); the run is cancelled when either is set. The controller
 * passes its own flag and the caller-supplied one so an in-stage check sees
 * the same requests as the between-stage check. Either may be NULL. The
 * flags must outlive the run.
 */
void fsd_fsmap_stage_set_cancel(fsd_fsmap_stage_t *stage,
                                const FSD_ATOMIC int *flag,
                                const FSD_ATOMIC int *flag2);

/** Enable verbose output (1 to enable, 0 to disable). */
void fsd_fsmap_stage_set_verbose(fsd_fsmap_stage_t *stage, int verbose);

/** Destroy the stage (NULL is safe). */
void fsd_fsmap_stage_destroy(fsd_fsmap_stage_t *stage);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_STAGE_FSMAP_H */
