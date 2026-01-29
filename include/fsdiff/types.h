/**
 * @file types.h
 * @brief Core type definitions for fsdiff library
 */

#ifndef FSDIFF_TYPES_H
#define FSDIFF_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * Constants
 *===========================================================================*/

/** File format magic: "BKDF" in little-endian */
#define FSD_MAGIC               0x46444B42

/** File format version */
#define FSD_VERSION             1

/** Default block size (4 KiB) */
#define FSD_DEFAULT_BLOCK_SIZE  4096

/** Minimum block size (512 bytes) */
#define FSD_MIN_BLOCK_SIZE      512

/** Maximum block size (1 MiB) */
#define FSD_MAX_BLOCK_SIZE      (1 << 20)

/** FFT coprime moduli */
#define FSD_MODULUS_M1          1048576    /**< 2^20 */
#define FSD_MODULUS_M2          531441     /**< 3^12 */
#define FSD_MODULUS_M3          390625     /**< 5^8  */

/** Product of moduli (~217 TB coverage) */
#define FSD_MODULUS_PRODUCT     217629003912500000ULL

/*===========================================================================
 * Enumerations
 *===========================================================================*/

/**
 * Operation types in the BKDF patch format.
 */
typedef enum {
    FSD_OP_COPY_IDENTITY   = 0,  /**< Block unchanged at same position */
    FSD_OP_COPY_RELOCATE   = 1,  /**< Block moved from different position */
    FSD_OP_ZERO            = 2,  /**< Block is all zeros */
    FSD_OP_ONE             = 3,  /**< Block is all 0xFF */
    FSD_OP_COPY_ADD        = 4,  /**< Block = source + delta */
    FSD_OP_LITERAL         = 5,  /**< Raw block data in literal stream */
    FSD_OP_RESERVED_6      = 6,  /**< Reserved */
    FSD_OP_RESERVED_7      = 7,  /**< Reserved */
} fsd_op_type_t;

/**
 * Diff stream format for OP_COPY_ADD operations.
 */
typedef enum {
    FSD_DIFF_DENSE  = 0,  /**< One diff byte per output byte */
    FSD_DIFF_SPARSE = 1,  /**< Alternating copy/copy-add runs */
} fsd_diff_format_t;

/**
 * Block match type determined during analysis.
 */
typedef enum {
    FSD_MATCH_NONE       = 0,  /**< No match found */
    FSD_MATCH_IDENTITY   = 1,  /**< Same position, identical content */
    FSD_MATCH_RELOCATE   = 2,  /**< Different position, identical content */
    FSD_MATCH_PARTIAL    = 3,  /**< Approximate match via FFT correlation */
    FSD_MATCH_ZERO       = 4,  /**< Block is all zeros */
    FSD_MATCH_ONE        = 5,  /**< Block is all 0xFF */
} fsd_match_type_t;

/*===========================================================================
 * File Format Structures
 *===========================================================================*/

/**
 * BKDF file header (32 bytes).
 *
 * File structure:
 *   [Header: 32 bytes]
 *   [Operation Stream: op_stream_len bytes]
 *   [Diff Stream: diff_stream_len bytes]
 *   [Literal Stream: to end of file]
 */
typedef struct {
    uint8_t  magic[4];          /**< "BKDF" (0x42 0x4B 0x44 0x46) */
    uint8_t  version;           /**< Format version (1) */
    uint8_t  block_size_log2;   /**< log2(block_size), e.g. 12 for 4096 */
    uint16_t reserved;          /**< Must be zero */
    uint64_t dest_blocks;       /**< Total blocks in destination image */
    uint64_t op_stream_len;     /**< Bytes in operation stream */
    uint64_t diff_stream_len;   /**< Bytes in diff stream */
} fsd_header_t;

/*===========================================================================
 * Context Handles (Opaque)
 *===========================================================================*/

/** Diff generation context */
typedef struct fsd_diff_ctx fsd_diff_ctx_t;

/** Patch application context */
typedef struct fsd_patch_ctx fsd_patch_ctx_t;

/*===========================================================================
 * Callback Types
 *===========================================================================*/

/**
 * Progress callback function type.
 *
 * @param user_data   User-provided context pointer
 * @param processed   Number of blocks processed so far
 * @param total       Total number of blocks to process
 */
typedef void (*fsd_progress_fn)(void *user_data, uint64_t processed, uint64_t total);

/**
 * Custom memory allocator interface.
 */
typedef struct {
    void *(*malloc_fn)(size_t size, void *user_data);
    void  (*free_fn)(void *ptr, void *user_data);
    void *(*realloc_fn)(void *ptr, size_t size, void *user_data);
    void *user_data;
} fsd_allocator_t;

/*===========================================================================
 * Statistics
 *===========================================================================*/

/**
 * Statistics from a diff operation.
 */
typedef struct {
    uint64_t total_blocks;       /**< Total destination blocks */
    uint64_t identity_matches;   /**< Blocks matched by identity (Stage 1) */
    uint64_t relocate_matches;   /**< Blocks matched by relocation (Stage 2) */
    uint64_t partial_matches;    /**< Blocks matched by FFT (Stage 3) */
    uint64_t zero_blocks;        /**< Blocks that are all zeros */
    uint64_t one_blocks;         /**< Blocks that are all 0xFF */
    uint64_t literal_blocks;     /**< Blocks stored as literals */
    size_t   patch_size;         /**< Size of generated patch in bytes */
    double   elapsed_seconds;    /**< Time taken for diff operation */
} fsd_diff_stats_t;

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_TYPES_H */
