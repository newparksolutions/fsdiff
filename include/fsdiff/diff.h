/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file diff.h
 * @brief Diff generation API for fsdiff library
 */

#ifndef FSDIFF_DIFF_H
#define FSDIFF_DIFF_H

#include "types.h"
#include "error.h"
#include "options.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a diff context for generating patches.
 *
 * @param ctx_out  Output pointer for the created context
 * @param opts     Options (NULL for defaults)
 * @return         FSD_SUCCESS or error code
 */
fsd_error_t fsd_diff_create(fsd_diff_ctx_t **ctx_out,
                            const fsd_diff_options_t *opts);

/**
 * Generate a diff (patch) between two files.
 *
 * @param ctx          Diff context
 * @param src_path     Path to reference (old) image
 * @param dest_path    Path to destination (new) image
 * @param output_path  Path for output patch file
 * @return             FSD_SUCCESS or error code
 */
fsd_error_t fsd_diff_files(fsd_diff_ctx_t *ctx,
                           const char *src_path,
                           const char *dest_path,
                           const char *output_path);

/**
 * Set progress callback for diff operations.
 *
 * @param ctx        Diff context
 * @param callback   Progress callback function (NULL to disable)
 * @param user_data  User data passed to callback
 */
void fsd_diff_set_progress(fsd_diff_ctx_t *ctx,
                           fsd_progress_fn callback,
                           void *user_data);

/**
 * Cancel an in-progress diff operation.
 *
 * Thread-safe; can be called from another thread or signal handler.
 *
 * @param ctx  Diff context
 */
void fsd_diff_cancel(fsd_diff_ctx_t *ctx);

/**
 * Get statistics from the last diff operation.
 *
 * @param ctx        Diff context
 * @param stats_out  Output structure for statistics
 * @return           FSD_SUCCESS or error code
 */
fsd_error_t fsd_diff_get_stats(const fsd_diff_ctx_t *ctx,
                               fsd_diff_stats_t *stats_out);

/**
 * Destroy a diff context and release resources.
 *
 * @param ctx  Diff context (NULL is safe)
 */
void fsd_diff_destroy(fsd_diff_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_DIFF_H */
