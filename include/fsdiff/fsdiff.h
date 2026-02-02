/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file fsdiff.h
 * @brief Main header for fsdiff library - filesystem binary diff/patch
 *
 * fsdiff is a library for creating and applying binary patches between
 * filesystem images. It uses a three-stage block matching algorithm:
 *
 *   Stage 1: Identity matching (same position, identical content)
 *   Stage 2: Relocation matching (different position, identical content)
 *   Stage 3: FFT-based partial matching (approximate matches)
 *
 * The output format (BKDF) is designed for efficient storage and
 * external compression.
 *
 * @code
 * #include <fsdiff/fsdiff.h>
 *
 * // Initialize library (optional, enables SIMD detection)
 * fsd_init();
 *
 * // Generate a patch
 * fsd_diff_ctx_t *ctx;
 * fsd_diff_create(&ctx, NULL);
 * fsd_diff_files(ctx, "old.img", "new.img", "update.bkdf");
 * fsd_diff_destroy(ctx);
 *
 * // Apply a patch
 * fsd_patch_ctx_t *pctx;
 * fsd_patch_create(&pctx, NULL);
 * fsd_patch_apply(pctx, "old.img", "update.bkdf", "new.img");
 * fsd_patch_destroy(pctx);
 *
 * fsd_cleanup();
 * @endcode
 */

#ifndef FSDIFF_H
#define FSDIFF_H

/* Include all public headers */
#include "types.h"
#include "error.h"
#include "options.h"
#include "diff.h"
#include "patch.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the fsdiff library.
 *
 * This performs runtime SIMD detection and initializes any global state.
 * Calling this is optional but recommended before using other functions.
 * Thread-safe; only the first call performs initialization.
 *
 * @return  FSD_SUCCESS or error code
 */
fsd_error_t fsd_init(void);

/**
 * Clean up library resources.
 *
 * Call this when done using the library to release any global resources.
 * After calling, you must call fsd_init() again before using other functions.
 */
void fsd_cleanup(void);

/**
 * Get the library version string.
 *
 * @return  Static string like "1.0.0"
 */
const char *fsd_version(void);

/**
 * Get library build information.
 *
 * @return  Static string with compiler and feature info
 */
const char *fsd_build_info(void);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_H */
