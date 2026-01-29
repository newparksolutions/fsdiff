/**
 * @file stage_controller.h
 * @brief Orchestrates all matching stages in sequence
 */

#ifndef FSDIFF_STAGE_CONTROLLER_H
#define FSDIFF_STAGE_CONTROLLER_H

#include <fsdiff/types.h>
#include <fsdiff/error.h>
#include <fsdiff/options.h>
#include "../core/block_tracker.h"
#include "../core/memory_pool.h"
#include "stage_identity.h"
#include "stage_relocation.h"
#include "stage_partial.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Stage controller handle */
typedef struct fsd_stage_controller {
    /* Configuration */
    size_t block_size;
    bool enable_identity;
    bool enable_relocation;
    bool enable_partial;
    int verbose_identity;
    int verbose_relocation;
    int verbose_partial;

    /* Stages */
    fsd_identity_stage_t *identity;
    fsd_relocation_stage_t *relocation;
    fsd_partial_stage_t *partial;

    /* Shared resources */
    fsd_block_tracker_t *tracker;
    fsd_memory_pool_t *pool;

    /* State */
    volatile int cancelled;

    /* Progress */
    fsd_progress_fn progress_cb;
    void *progress_user_data;
} fsd_stage_controller_t;

/**
 * Create stage controller.
 *
 * @param ctrl_out     Output pointer for controller
 * @param opts         Diff options
 * @param src_blocks   Number of blocks in source image
 * @param dest_blocks  Number of blocks in destination image
 * @return             FSD_SUCCESS or error code
 */
fsd_error_t fsd_stage_controller_create(fsd_stage_controller_t **ctrl_out,
                                        const fsd_diff_options_t *opts,
                                        uint64_t src_blocks,
                                        uint64_t dest_blocks);

/**
 * Run all enabled stages in sequence.
 *
 * Stage 1 (identity) processes all blocks.
 * Stage 2 (relocation) processes remaining unmatched blocks.
 * Stage 3 (partial) processes remaining unmatched blocks.
 *
 * @param ctrl       Controller handle
 * @param src_data   Source image data
 * @param src_size   Source image size in bytes
 * @param dest_data  Destination image data
 * @param dest_size  Destination image size in bytes
 * @return           FSD_SUCCESS or error code
 */
fsd_error_t fsd_stage_controller_run(fsd_stage_controller_t *ctrl,
                                     const uint8_t *src_data,
                                     size_t src_size,
                                     const uint8_t *dest_data,
                                     size_t dest_size);

/**
 * Get the block tracker with results.
 *
 * @param ctrl  Controller handle
 * @return      Block tracker (do not free)
 */
fsd_block_tracker_t *fsd_stage_controller_get_tracker(fsd_stage_controller_t *ctrl);

/**
 * Set progress callback.
 *
 * @param ctrl       Controller handle
 * @param callback   Progress callback function
 * @param user_data  User data for callback
 */
void fsd_stage_controller_set_progress(fsd_stage_controller_t *ctrl,
                                       fsd_progress_fn callback,
                                       void *user_data);

/**
 * Cancel the current operation.
 *
 * @param ctrl  Controller handle
 */
void fsd_stage_controller_cancel(fsd_stage_controller_t *ctrl);

/**
 * Enable verbose output for identity matching stage.
 *
 * @param ctrl     Controller handle
 * @param verbose  1 to enable, 0 to disable
 */
void fsd_stage_controller_set_verbose_identity(fsd_stage_controller_t *ctrl, int verbose);

/**
 * Enable verbose output for relocation matching stage.
 *
 * @param ctrl     Controller handle
 * @param verbose  1 to enable, 0 to disable
 */
void fsd_stage_controller_set_verbose_relocation(fsd_stage_controller_t *ctrl, int verbose);

/**
 * Enable verbose output for partial matching stage.
 *
 * @param ctrl     Controller handle
 * @param verbose  1 to enable, 0 to disable
 */
void fsd_stage_controller_set_verbose_partial(fsd_stage_controller_t *ctrl, int verbose);

/**
 * Enable verbose output for all stages.
 *
 * @param ctrl     Controller handle
 * @param verbose  1 to enable, 0 to disable
 */
void fsd_stage_controller_set_verbose(fsd_stage_controller_t *ctrl, int verbose);

/**
 * Destroy stage controller.
 *
 * @param ctrl  Controller handle (NULL is safe)
 */
void fsd_stage_controller_destroy(fsd_stage_controller_t *ctrl);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_STAGE_CONTROLLER_H */
