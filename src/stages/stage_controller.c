/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file stage_controller.c
 * @brief Stage controller implementation
 */

#include "stage_controller.h"
#include <stdlib.h>

/* True if the operation has been cancelled, via either the controller's own
 * flag or an external flag supplied by fsd_stage_controller_set_cancel_flag. */
static int controller_cancelled(const fsd_stage_controller_t *ctrl) {
    if (fsd_atomic_load(ctrl->cancelled)) {
        return 1;
    }
    if (ctrl->ext_cancel && fsd_atomic_load(*ctrl->ext_cancel)) {
        return 1;
    }
    return 0;
}

fsd_error_t fsd_stage_controller_create(fsd_stage_controller_t **ctrl_out,
                                        const fsd_diff_options_t *opts,
                                        uint64_t src_blocks,
                                        uint64_t dest_blocks) {
    if (!ctrl_out) {
        return FSD_ERR_INVALID_ARG;
    }

    /* Relocation matches against the per-block CRC32 that only the identity
     * stage computes (see options.h). Without identity every CRC32 is zero
     * and relocation silently finds nothing, so reject the combination
     * rather than degrade without a diagnostic. */
    if (opts && opts->enable_relocation && !opts->enable_identity) {
        return FSD_ERR_INVALID_ARG;
    }

    fsd_stage_controller_t *ctrl = calloc(1, sizeof(fsd_stage_controller_t));
    if (!ctrl) {
        return FSD_ERR_OUT_OF_MEMORY;
    }

    /* Apply options or defaults */
    if (opts) {
        ctrl->block_size = 1ULL << opts->block_size_log2;
        ctrl->enable_identity = opts->enable_identity;
        ctrl->enable_relocation = opts->enable_relocation;
        ctrl->enable_fsmap = opts->enable_fsmap;
        ctrl->enable_partial = opts->enable_partial;
    } else {
        ctrl->block_size = FSD_DEFAULT_BLOCK_SIZE;
        ctrl->enable_identity = true;
        ctrl->enable_relocation = true;
        ctrl->enable_fsmap = true;
        ctrl->enable_partial = true;
    }

    fsd_error_t err;

    /* Create block tracker */
    err = fsd_block_tracker_create(&ctrl->tracker, dest_blocks, ctrl->block_size);
    if (err != FSD_SUCCESS) {
        free(ctrl);
        return err;
    }

    /* Create memory pool for shared allocations */
    err = fsd_pool_create(&ctrl->pool, 4 * 1024 * 1024, 0, NULL);
    if (err != FSD_SUCCESS) {
        fsd_block_tracker_destroy(ctrl->tracker);
        free(ctrl);
        return err;
    }

    /* Create stages */
    if (ctrl->enable_identity) {
        err = fsd_identity_stage_create(&ctrl->identity, ctrl->block_size);
        if (err != FSD_SUCCESS) {
            fsd_pool_destroy(ctrl->pool);
            fsd_block_tracker_destroy(ctrl->tracker);
            free(ctrl);
            return err;
        }
    }

    if (ctrl->enable_relocation) {
        err = fsd_relocation_stage_create(&ctrl->relocation, ctrl->block_size, src_blocks);
        if (err != FSD_SUCCESS) {
            fsd_identity_stage_destroy(ctrl->identity);
            fsd_pool_destroy(ctrl->pool);
            fsd_block_tracker_destroy(ctrl->tracker);
            free(ctrl);
            return err;
        }
    }

    if (ctrl->enable_fsmap) {
        err = fsd_fsmap_stage_create(&ctrl->fsmap, ctrl->block_size,
                                     opts ? opts->partial_threshold : 0.5f,
                                     opts ? opts->search_radius : 0);
        if (err != FSD_SUCCESS) {
            fsd_relocation_stage_destroy(ctrl->relocation);
            fsd_identity_stage_destroy(ctrl->identity);
            fsd_pool_destroy(ctrl->pool);
            fsd_block_tracker_destroy(ctrl->tracker);
            free(ctrl);
            return err;
        }
    }

    if (ctrl->enable_partial) {
        err = fsd_partial_stage_create(&ctrl->partial, ctrl->block_size,
                                       opts ? opts->partial_threshold : 0.5f,
                                       opts ? opts->search_radius : 0);
        if (err != FSD_SUCCESS) {
            fsd_fsmap_stage_destroy(ctrl->fsmap);
            fsd_relocation_stage_destroy(ctrl->relocation);
            fsd_identity_stage_destroy(ctrl->identity);
            fsd_pool_destroy(ctrl->pool);
            fsd_block_tracker_destroy(ctrl->tracker);
            free(ctrl);
            return err;
        }
    }

    ctrl->cancelled = 0;
    ctrl->ext_cancel = NULL;
    ctrl->progress_cb = NULL;
    ctrl->progress_user_data = NULL;

    *ctrl_out = ctrl;
    return FSD_SUCCESS;
}

fsd_error_t fsd_stage_controller_run(fsd_stage_controller_t *ctrl,
                                     const uint8_t *src_data,
                                     size_t src_size,
                                     const uint8_t *dest_data,
                                     size_t dest_size) {
    if (!ctrl || !src_data || !dest_data) {
        return FSD_ERR_INVALID_ARG;
    }

    uint64_t src_blocks = src_size / ctrl->block_size;
    uint64_t dest_blocks = dest_size / ctrl->block_size;

    fsd_error_t err;

    /*
     * Stage 1: Identity matching
     * Compare blocks at same positions, also detect zero/one blocks
     */
    if (ctrl->enable_identity && ctrl->identity) {
        if (controller_cancelled(ctrl)) return FSD_ERR_CANCELLED;

        err = fsd_identity_stage_run(ctrl->identity,
                                     ctrl->tracker,
                                     src_data,
                                     src_blocks,
                                     dest_data,
                                     dest_blocks);
        if (err != FSD_SUCCESS) return err;

        /* Report progress */
        if (ctrl->progress_cb) {
            ctrl->progress_cb(ctrl->progress_user_data,
                              dest_blocks - fsd_block_tracker_unmatched_count(ctrl->tracker),
                              dest_blocks);
        }
    }

    /*
     * Stage 2: Relocation matching
     * Build hash table of source blocks, look up unmatched dest blocks
     */
    if (ctrl->enable_relocation && ctrl->relocation) {
        if (controller_cancelled(ctrl)) return FSD_ERR_CANCELLED;

        /* Build source block index */
        err = fsd_relocation_stage_build_index(ctrl->relocation, src_data);
        if (err != FSD_SUCCESS) return err;

        /* Find relocated blocks */
        err = fsd_relocation_stage_run(ctrl->relocation,
                                       ctrl->tracker,
                                       src_data,
                                       dest_data);
        if (err != FSD_SUCCESS) return err;

        /* Report progress */
        if (ctrl->progress_cb) {
            ctrl->progress_cb(ctrl->progress_user_data,
                              dest_blocks - fsd_block_tracker_unmatched_count(ctrl->tracker),
                              dest_blocks);
        }
    }

    /*
     * Stage 2b: Filesystem-aware matching
     * If both images are supported ext filesystems, map unmatched dest
     * blocks to (path, file offset) and verify the same-path/same-offset
     * source position. Self-deactivates on unsupported images.
     */
    if (ctrl->enable_fsmap && ctrl->fsmap) {
        if (controller_cancelled(ctrl)) return FSD_ERR_CANCELLED;

        fsd_fsmap_stage_set_cancel(ctrl->fsmap, &ctrl->cancelled,
                                   ctrl->ext_cancel);

        err = fsd_fsmap_stage_build_index(ctrl->fsmap,
                                          src_data, src_size,
                                          dest_data, dest_size,
                                          ctrl->tracker);
        if (err != FSD_SUCCESS) return err;

        err = fsd_fsmap_stage_run(ctrl->fsmap,
                                  ctrl->tracker,
                                  src_data,
                                  dest_data,
                                  ctrl->pool);
        if (err != FSD_SUCCESS) return err;

        /* Report progress */
        if (ctrl->progress_cb) {
            ctrl->progress_cb(ctrl->progress_user_data,
                              dest_blocks - fsd_block_tracker_unmatched_count(ctrl->tracker),
                              dest_blocks);
        }
    }

    /*
     * Stage 3: Partial matching (local search)
     * Find approximate matches for remaining unmatched blocks
     */
    if (ctrl->enable_partial && ctrl->partial) {
        if (controller_cancelled(ctrl)) return FSD_ERR_CANCELLED;

        /* Build source index */
        err = fsd_partial_stage_build_index(ctrl->partial,
                                            src_data,
                                            src_blocks);
        if (err != FSD_SUCCESS) return err;

        /* Find partial matches */
        err = fsd_partial_stage_run(ctrl->partial,
                                    ctrl->tracker,
                                    src_data,
                                    dest_data,
                                    ctrl->pool);
        if (err != FSD_SUCCESS) return err;

        /* Report progress */
        if (ctrl->progress_cb) {
            ctrl->progress_cb(ctrl->progress_user_data,
                              dest_blocks - fsd_block_tracker_unmatched_count(ctrl->tracker),
                              dest_blocks);
        }
    }

    /* Mark remaining unmatched blocks for literal storage */
    fsd_block_tracker_finalize(ctrl->tracker);

    /* Final progress */
    if (ctrl->progress_cb) {
        ctrl->progress_cb(ctrl->progress_user_data, dest_blocks, dest_blocks);
    }

    return FSD_SUCCESS;
}

fsd_block_tracker_t *fsd_stage_controller_get_tracker(fsd_stage_controller_t *ctrl) {
    return ctrl ? ctrl->tracker : NULL;
}

void fsd_stage_controller_set_progress(fsd_stage_controller_t *ctrl,
                                       fsd_progress_fn callback,
                                       void *user_data) {
    if (!ctrl) return;
    ctrl->progress_cb = callback;
    ctrl->progress_user_data = user_data;
}

void fsd_stage_controller_cancel(fsd_stage_controller_t *ctrl) {
    if (ctrl) {
        fsd_atomic_store(ctrl->cancelled, 1);
    }
}

void fsd_stage_controller_set_cancel_flag(fsd_stage_controller_t *ctrl,
                                          const FSD_ATOMIC int *flag) {
    if (ctrl) {
        ctrl->ext_cancel = flag;
    }
}

void fsd_stage_controller_set_verbose_identity(fsd_stage_controller_t *ctrl, int verbose) {
    if (ctrl) {
        ctrl->verbose_identity = verbose;
        if (ctrl->identity) {
            fsd_identity_stage_set_verbose(ctrl->identity, verbose);
        }
    }
}

void fsd_stage_controller_set_verbose_relocation(fsd_stage_controller_t *ctrl, int verbose) {
    if (ctrl) {
        ctrl->verbose_relocation = verbose;
        if (ctrl->relocation) {
            fsd_relocation_stage_set_verbose(ctrl->relocation, verbose);
        }
    }
}

void fsd_stage_controller_set_verbose_fsmap(fsd_stage_controller_t *ctrl, int verbose) {
    if (ctrl) {
        ctrl->verbose_fsmap = verbose;
        if (ctrl->fsmap) {
            fsd_fsmap_stage_set_verbose(ctrl->fsmap, verbose);
        }
    }
}

void fsd_stage_controller_set_verbose_partial(fsd_stage_controller_t *ctrl, int verbose) {
    if (ctrl) {
        ctrl->verbose_partial = verbose;
        if (ctrl->partial) {
            fsd_partial_stage_set_verbose(ctrl->partial, verbose);
        }
    }
}

void fsd_stage_controller_set_verbose(fsd_stage_controller_t *ctrl, int verbose) {
    fsd_stage_controller_set_verbose_identity(ctrl, verbose);
    fsd_stage_controller_set_verbose_relocation(ctrl, verbose);
    fsd_stage_controller_set_verbose_fsmap(ctrl, verbose);
    fsd_stage_controller_set_verbose_partial(ctrl, verbose);
}

void fsd_stage_controller_destroy(fsd_stage_controller_t *ctrl) {
    if (!ctrl) return;

    fsd_partial_stage_destroy(ctrl->partial);
    fsd_fsmap_stage_destroy(ctrl->fsmap);
    fsd_relocation_stage_destroy(ctrl->relocation);
    fsd_identity_stage_destroy(ctrl->identity);
    fsd_block_tracker_destroy(ctrl->tracker);
    fsd_pool_destroy(ctrl->pool);

    free(ctrl);
}
