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
 * Generate a diff between memory buffers.
 *
 * @param ctx          Diff context
 * @param src          Reference (old) data
 * @param src_size     Size of reference data in bytes
 * @param dest         Destination (new) data
 * @param dest_size    Size of destination data in bytes
 * @param output       Output buffer for patch (caller-allocated)
 * @param output_size  In: buffer capacity; Out: bytes written
 * @return             FSD_SUCCESS or error code
 */
fsd_error_t fsd_diff_memory(fsd_diff_ctx_t *ctx,
                            const void *src,
                            size_t src_size,
                            const void *dest,
                            size_t dest_size,
                            void *output,
                            size_t *output_size);

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
