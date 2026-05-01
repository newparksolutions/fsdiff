/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file options.h
 * @brief Configuration options for fsdiff operations
 */

#ifndef FSDIFF_OPTIONS_H
#define FSDIFF_OPTIONS_H

#include "types.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Source reader backend selection.
 *
 * Block-device sources (e.g. /dev/mmcblk0p5) underlying a mounted FS share
 * the bdev page cache with the FS driver, which dirties metadata buffers
 * in memory without writing them back on a read-only mount. Reading via
 * mmap or buffered I/O therefore returns the modified-in-memory bytes,
 * not what is on disk. O_DIRECT bypasses that cache entirely.
 *
 * AUTO is the safe default: regular files use mmap; block devices are
 * forced through O_DIRECT and refuse to fall back if it fails.
 */
typedef enum {
    FSD_SOURCE_AUTO   = 0,  /**< stat() picks: S_ISBLK -> direct, else mmap */
    FSD_SOURCE_MMAP   = 1,  /**< force mmap (page-cache-backed) */
    FSD_SOURCE_DIRECT = 2,  /**< force O_DIRECT pread; fail if unsupported */
} fsd_source_mode_t;

/**
 * Options for diff generation.
 */
typedef struct {
    /** Block size as log2 (default: 12 = 4096 bytes) */
    uint8_t block_size_log2;

    /** Enable Stage 1: Identity matching (default: true) */
    bool enable_identity;

    /** Enable Stage 2: Relocation matching via CRC32 (default: true) */
    bool enable_relocation;

    /** Enable Stage 3: Local search partial matching (default: true) */
    bool enable_partial;

    /** Match threshold for partial matching (0.0-1.0, default: 0.5) */
    float partial_threshold;

    /** Search radius in blocks for partial matching (default: 8) */
    int search_radius;

    /** Maximum memory usage in MB (0 = unlimited, default: 0) */
    size_t max_memory_mb;

    /** Force scalar SIMD implementation (default: false) */
    bool force_scalar;

    /** Verbose output for all stages (default: false) */
    bool verbose;

    /** Source reader backend (default: FSD_SOURCE_AUTO) */
    fsd_source_mode_t source_mode;

    /** Custom allocator (NULL for default) */
    fsd_allocator_t *allocator;

} fsd_diff_options_t;

/**
 * Options for patch application.
 */
typedef struct {
    /** Verify output checksum after patching (default: false) */
    bool verify_output;

    /** Source reader backend (default: FSD_SOURCE_AUTO) */
    fsd_source_mode_t source_mode;

    /** Custom allocator (NULL for default) */
    fsd_allocator_t *allocator;

} fsd_patch_options_t;

/**
 * Initialize diff options with default values.
 *
 * @param opts  Options structure to initialize
 */
void fsd_diff_options_init(fsd_diff_options_t *opts);

/**
 * Initialize patch options with default values.
 *
 * @param opts  Options structure to initialize
 */
void fsd_patch_options_init(fsd_patch_options_t *opts);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_OPTIONS_H */
