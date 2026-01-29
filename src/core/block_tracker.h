/**
 * @file block_tracker.h
 * @brief Tracks state of all destination blocks through matching stages
 */

#ifndef FSDIFF_BLOCK_TRACKER_H
#define FSDIFF_BLOCK_TRACKER_H

#include <fsdiff/types.h>
#include <fsdiff/error.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** State of a single destination block */
typedef struct {
    uint64_t dest_index;         /**< Destination block index */
    uint64_t src_index;          /**< Source block index (for relocate/partial) */
    int64_t byte_offset;         /**< Byte offset within block (for partial) */
    uint32_t crc32;              /**< CRC32 of destination block */
    fsd_match_type_t match_type; /**< Match type determined by stages */
    uint8_t *delta;              /**< Delta data for partial match (NULL if not needed) */
    uint32_t delta_size;         /**< Size of delta data */
} fsd_block_state_t;

/** Block tracker handle */
typedef struct fsd_block_tracker {
    fsd_block_state_t *blocks;   /**< Array of block states */
    uint64_t count;              /**< Number of destination blocks */
    size_t block_size;           /**< Size of each block in bytes */

    /** Bitmap of unmatched blocks (1 = unmatched) */
    uint64_t *unmatched_bitmap;
    size_t bitmap_words;

    /** Statistics */
    uint64_t identity_count;
    uint64_t relocate_count;
    uint64_t partial_count;
    uint64_t zero_count;
    uint64_t one_count;
    uint64_t literal_count;
} fsd_block_tracker_t;

/**
 * Create a block tracker for destination blocks.
 *
 * @param tracker_out  Output pointer for tracker
 * @param dest_blocks  Number of destination blocks
 * @param block_size   Size of each block in bytes
 * @return             FSD_SUCCESS or error code
 */
fsd_error_t fsd_block_tracker_create(fsd_block_tracker_t **tracker_out,
                                     uint64_t dest_blocks,
                                     size_t block_size);

/**
 * Mark a block as matched.
 *
 * Updates statistics and clears the unmatched bit.
 *
 * @param tracker     Block tracker
 * @param dest_index  Destination block index
 * @param match_type  Type of match
 * @param src_index   Source block index (for relocate/partial)
 */
void fsd_block_tracker_set_match(fsd_block_tracker_t *tracker,
                                 uint64_t dest_index,
                                 fsd_match_type_t match_type,
                                 uint64_t src_index);

/**
 * Set CRC32 for a block.
 *
 * @param tracker     Block tracker
 * @param dest_index  Destination block index
 * @param crc32       CRC32 value
 */
void fsd_block_tracker_set_crc32(fsd_block_tracker_t *tracker,
                                 uint64_t dest_index,
                                 uint32_t crc32);

/**
 * Set delta data for a partial match.
 *
 * @param tracker      Block tracker
 * @param dest_index   Destination block index
 * @param byte_offset  Byte offset from aligned position
 * @param delta        Delta data (ownership transferred)
 * @param delta_size   Size of delta data
 */
void fsd_block_tracker_set_delta(fsd_block_tracker_t *tracker,
                                 uint64_t dest_index,
                                 int64_t byte_offset,
                                 uint8_t *delta,
                                 uint32_t delta_size);

/**
 * Check if a block is still unmatched.
 *
 * @param tracker     Block tracker
 * @param dest_index  Destination block index
 * @return            true if unmatched
 */
bool fsd_block_tracker_is_unmatched(const fsd_block_tracker_t *tracker,
                                    uint64_t dest_index);

/**
 * Get block state by destination index.
 *
 * @param tracker     Block tracker
 * @param dest_index  Destination block index
 * @return            Pointer to block state (do not free)
 */
const fsd_block_state_t *fsd_block_tracker_get(const fsd_block_tracker_t *tracker,
                                                uint64_t dest_index);

/**
 * Count unmatched blocks.
 *
 * @param tracker  Block tracker
 * @return         Number of unmatched blocks
 */
uint64_t fsd_block_tracker_unmatched_count(const fsd_block_tracker_t *tracker);

/**
 * Callback type for iterating unmatched blocks.
 */
typedef void (*fsd_unmatched_fn)(uint64_t dest_index, void *user_data);

/**
 * Iterate over all unmatched blocks.
 *
 * @param tracker    Block tracker
 * @param callback   Function to call for each unmatched block
 * @param user_data  User data passed to callback
 */
void fsd_block_tracker_foreach_unmatched(const fsd_block_tracker_t *tracker,
                                         fsd_unmatched_fn callback,
                                         void *user_data);

/**
 * Mark all remaining unmatched blocks as literals.
 *
 * @param tracker  Block tracker
 */
void fsd_block_tracker_finalize(fsd_block_tracker_t *tracker);

/**
 * Destroy the block tracker and free resources.
 *
 * @param tracker  Block tracker (NULL is safe)
 */
void fsd_block_tracker_destroy(fsd_block_tracker_t *tracker);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_BLOCK_TRACKER_H */
