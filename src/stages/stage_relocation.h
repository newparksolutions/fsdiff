/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file stage_relocation.h
 * @brief Stage 2: Relocation matching - find blocks moved to different positions
 */

#ifndef FSDIFF_STAGE_RELOCATION_H
#define FSDIFF_STAGE_RELOCATION_H

#include <fsdiff/types.h>
#include <fsdiff/error.h>
#include "../core/block_tracker.h"
#include "../core/hash_table.h"
#include "../core/memory_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Relocation stage handle */
typedef struct fsd_relocation_stage {
    size_t block_size;
    uint64_t src_blocks;
    fsd_hash_table_t *src_hash;
    fsd_memory_pool_t *pool;
    int verbose;
} fsd_relocation_stage_t;

/**
 * Create relocation matching stage.
 *
 * @param stage_out   Output pointer for stage
 * @param block_size  Block size in bytes
 * @param src_blocks  Number of blocks in source image
 * @return            FSD_SUCCESS or error code
 */
fsd_error_t fsd_relocation_stage_create(fsd_relocation_stage_t **stage_out,
                                        size_t block_size,
                                        uint64_t src_blocks);

/**
 * Build CRC32 hash table from source blocks.
 *
 * Must be called before run(). Creates a hash table mapping CRC32 values
 * to block indices in the source image. Zero blocks are excluded.
 *
 * @param stage  Stage handle
 * @param src    Source image data
 * @return       FSD_SUCCESS or error code
 */
fsd_error_t fsd_relocation_stage_build_index(fsd_relocation_stage_t *stage,
                                             const uint8_t *src);

/**
 * Run relocation matching on unmatched blocks.
 *
 * For each unmatched destination block, looks up its CRC32 in the source
 * hash table. On match, verifies with full memcmp to handle collisions.
 *
 * @param stage    Stage handle
 * @param tracker  Block tracker with CRC32s from identity stage
 * @param src      Source image data
 * @param dest     Destination image data
 * @return         FSD_SUCCESS or error code
 */
fsd_error_t fsd_relocation_stage_run(fsd_relocation_stage_t *stage,
                                     fsd_block_tracker_t *tracker,
                                     const uint8_t *src,
                                     const uint8_t *dest);

/**
 * Set verbose output mode.
 *
 * @param stage    Stage handle
 * @param verbose  1 to enable, 0 to disable
 */
void fsd_relocation_stage_set_verbose(fsd_relocation_stage_t *stage, int verbose);

/**
 * Destroy relocation stage.
 *
 * @param stage  Stage handle (NULL is safe)
 */
void fsd_relocation_stage_destroy(fsd_relocation_stage_t *stage);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_STAGE_RELOCATION_H */
