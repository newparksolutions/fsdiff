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
 * Options for diff generation.
 */
typedef struct {
    /** Block size as log2 (default: 12 = 4096 bytes) */
    uint8_t block_size_log2;

    /** Enable Stage 1: Identity matching (default: true) */
    bool enable_identity;

    /** Enable Stage 2: Relocation matching via CRC32 (default: true) */
    bool enable_relocation;

    /** Enable Stage 3: FFT partial matching (default: true) */
    bool enable_partial;

    /** Correlation threshold for partial match (0.0-1.0, default: 0.5) */
    float partial_threshold;

    /** Search radius in blocks for partial matching (default: 8) */
    int search_radius;

    /** Number of random projections for FFT (1-4, default: 1) */
    int num_projections;

    /** Use frequency weighting for FFT (default: false) */
    bool use_freq_weighting;

    /** Maximum memory usage in MB (0 = unlimited, default: 0) */
    size_t max_memory_mb;

    /** Force scalar SIMD implementation (default: false) */
    bool force_scalar;

    /** Verbose output for all stages (default: false) */
    bool verbose;

    /** Custom allocator (NULL for default) */
    fsd_allocator_t *allocator;

} fsd_diff_options_t;

/**
 * Options for patch application.
 */
typedef struct {
    /** Verify output checksum after patching (default: false) */
    bool verify_output;

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
