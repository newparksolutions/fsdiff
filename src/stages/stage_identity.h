/**
 * @file stage_identity.h
 * @brief Stage 1: Identity matching - compare blocks at same positions
 */

#ifndef FSDIFF_STAGE_IDENTITY_H
#define FSDIFF_STAGE_IDENTITY_H

#include <fsdiff/types.h>
#include <fsdiff/error.h>
#include "../core/block_tracker.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Identity stage handle */
typedef struct fsd_identity_stage {
    size_t block_size;
    int verbose;
} fsd_identity_stage_t;

/**
 * Create identity matching stage.
 *
 * @param stage_out   Output pointer for stage
 * @param block_size  Block size in bytes
 * @return            FSD_SUCCESS or error code
 */
fsd_error_t fsd_identity_stage_create(fsd_identity_stage_t **stage_out,
                                      size_t block_size);

/**
 * Run identity matching on all blocks.
 *
 * For each destination block, compares against the source block at the same
 * position. Also detects all-zero and all-one blocks.
 *
 * @param stage       Stage handle
 * @param tracker     Block tracker to update with matches
 * @param src         Source image data
 * @param src_blocks  Number of blocks in source
 * @param dest        Destination image data
 * @param dest_blocks Number of blocks in destination
 * @return            FSD_SUCCESS or error code
 */
fsd_error_t fsd_identity_stage_run(fsd_identity_stage_t *stage,
                                   fsd_block_tracker_t *tracker,
                                   const uint8_t *src,
                                   uint64_t src_blocks,
                                   const uint8_t *dest,
                                   uint64_t dest_blocks);

/**
 * Set verbose output mode.
 *
 * @param stage    Stage handle
 * @param verbose  1 to enable, 0 to disable
 */
void fsd_identity_stage_set_verbose(fsd_identity_stage_t *stage, int verbose);

/**
 * Destroy identity stage.
 *
 * @param stage  Stage handle (NULL is safe)
 */
void fsd_identity_stage_destroy(fsd_identity_stage_t *stage);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_STAGE_IDENTITY_H */
