/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file patch.h
 * @brief Patch application API for fsdiff library
 */

#ifndef FSDIFF_PATCH_H
#define FSDIFF_PATCH_H

#include "types.h"
#include "error.h"
#include "options.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a patch context for applying patches.
 *
 * @param ctx_out  Output pointer for the created context
 * @param opts     Options (NULL for defaults)
 * @return         FSD_SUCCESS or error code
 */
fsd_error_t fsd_patch_create(fsd_patch_ctx_t **ctx_out,
                             const fsd_patch_options_t *opts);

/**
 * Apply a patch to create a new file.
 *
 * @param ctx          Patch context
 * @param src_path     Path to reference (old) image
 * @param patch_path   Path to patch file
 * @param output_path  Path for output (new) image
 * @return             FSD_SUCCESS or error code
 */
fsd_error_t fsd_patch_apply(fsd_patch_ctx_t *ctx,
                            const char *src_path,
                            const char *patch_path,
                            const char *output_path);

/**
 * Read and validate a patch header without applying.
 *
 * @param patch_path  Path to patch file
 * @param header_out  Output structure for header data
 * @return            FSD_SUCCESS or error code
 */
fsd_error_t fsd_patch_read_header(const char *patch_path,
                                  fsd_header_t *header_out);

/**
 * Calculate the output size required for a patch.
 *
 * @param patch_path  Path to patch file
 * @param size_out    Output for required size in bytes
 * @return            FSD_SUCCESS or error code
 */
fsd_error_t fsd_patch_output_size(const char *patch_path,
                                  size_t *size_out);

/**
 * Set progress callback for patch operations.
 *
 * @param ctx        Patch context
 * @param callback   Progress callback function (NULL to disable)
 * @param user_data  User data passed to callback
 */
void fsd_patch_set_progress(fsd_patch_ctx_t *ctx,
                            fsd_progress_fn callback,
                            void *user_data);

/**
 * Destroy a patch context and release resources.
 *
 * @param ctx  Patch context (NULL is safe)
 */
void fsd_patch_destroy(fsd_patch_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_PATCH_H */
