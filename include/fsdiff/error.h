/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file error.h
 * @brief Error codes and handling for fsdiff library
 */

#ifndef FSDIFF_ERROR_H
#define FSDIFF_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error codes returned by fsdiff functions.
 */
typedef enum {
    FSD_SUCCESS             =  0,   /**< Operation completed successfully */
    FSD_ERR_INVALID_ARG     = -1,   /**< Invalid argument passed to function */
    FSD_ERR_OUT_OF_MEMORY   = -2,   /**< Memory allocation failed */
    FSD_ERR_IO              = -3,   /**< I/O error (file read/write) */
    FSD_ERR_CORRUPT_DATA    = -4,   /**< Patch file is corrupted */
    FSD_ERR_BAD_MAGIC       = -5,   /**< Invalid magic number in patch header */
    FSD_ERR_BAD_VERSION     = -6,   /**< Unsupported patch format version */
    FSD_ERR_BAD_BLOCK_SIZE  = -7,   /**< Invalid or unsupported block size */
    FSD_ERR_SIZE_MISMATCH   = -8,   /**< File size doesn't match expected */
    FSD_ERR_FFTW            = -9,   /**< FFTW library error */
    FSD_ERR_NOT_INITIALIZED = -10,  /**< Library not initialized */
    FSD_ERR_CANCELLED       = -11,  /**< Operation was cancelled */
    FSD_ERR_FILE_NOT_FOUND  = -12,  /**< File not found */
    FSD_ERR_PERMISSION      = -13,  /**< Permission denied */
    FSD_ERR_MMAP_FAILED     = -14,  /**< Memory mapping failed */
    FSD_ERR_TRUNCATED       = -15,  /**< Unexpected end of file */
    FSD_ERR_BAD_OPERATION   = -16,  /**< Unknown operation in patch */
} fsd_error_t;

/**
 * Returns a human-readable error message for the given error code.
 *
 * @param err  Error code
 * @return     Static string describing the error (never NULL)
 */
const char *fsd_strerror(fsd_error_t err);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_ERROR_H */
