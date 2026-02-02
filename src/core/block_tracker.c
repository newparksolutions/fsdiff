/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file block_tracker.c
 * @brief Block tracker implementation
 */

#include "block_tracker.h"
#include <stdlib.h>
#include <string.h>

/* Bitmap operations */
static inline void bitmap_set(uint64_t *bitmap, uint64_t index) {
    bitmap[index / 64] |= (1ULL << (index % 64));
}

static inline void bitmap_clear(uint64_t *bitmap, uint64_t index) {
    bitmap[index / 64] &= ~(1ULL << (index % 64));
}

static inline bool bitmap_test(const uint64_t *bitmap, uint64_t index) {
    return (bitmap[index / 64] & (1ULL << (index % 64))) != 0;
}

fsd_error_t fsd_block_tracker_create(fsd_block_tracker_t **tracker_out,
                                     uint64_t dest_blocks,
                                     size_t block_size) {
    if (!tracker_out || dest_blocks == 0 || block_size == 0) {
        return FSD_ERR_INVALID_ARG;
    }

    fsd_block_tracker_t *tracker = calloc(1, sizeof(fsd_block_tracker_t));
    if (!tracker) {
        return FSD_ERR_OUT_OF_MEMORY;
    }

    tracker->blocks = calloc(dest_blocks, sizeof(fsd_block_state_t));
    if (!tracker->blocks) {
        free(tracker);
        return FSD_ERR_OUT_OF_MEMORY;
    }

    /* Allocate bitmap for unmatched tracking */
    tracker->bitmap_words = (dest_blocks + 63) / 64;
    tracker->unmatched_bitmap = calloc(tracker->bitmap_words, sizeof(uint64_t));
    if (!tracker->unmatched_bitmap) {
        free(tracker->blocks);
        free(tracker);
        return FSD_ERR_OUT_OF_MEMORY;
    }

    /* Initialize all blocks as unmatched */
    tracker->count = dest_blocks;
    tracker->block_size = block_size;

    for (uint64_t i = 0; i < dest_blocks; i++) {
        tracker->blocks[i].dest_index = i;
        tracker->blocks[i].match_type = FSD_MATCH_NONE;
        bitmap_set(tracker->unmatched_bitmap, i);
    }

    tracker->identity_count = 0;
    tracker->relocate_count = 0;
    tracker->partial_count = 0;
    tracker->zero_count = 0;
    tracker->one_count = 0;
    tracker->literal_count = 0;

    *tracker_out = tracker;
    return FSD_SUCCESS;
}

void fsd_block_tracker_set_match(fsd_block_tracker_t *tracker,
                                 uint64_t dest_index,
                                 fsd_match_type_t match_type,
                                 uint64_t src_index) {
    if (!tracker || dest_index >= tracker->count) return;

    fsd_block_state_t *block = &tracker->blocks[dest_index];

    /* Don't re-match already matched blocks */
    if (block->match_type != FSD_MATCH_NONE) return;

    block->match_type = match_type;
    block->src_index = src_index;

    /* Clear unmatched bit */
    bitmap_clear(tracker->unmatched_bitmap, dest_index);

    /* Update statistics */
    switch (match_type) {
        case FSD_MATCH_IDENTITY:  tracker->identity_count++;  break;
        case FSD_MATCH_RELOCATE:  tracker->relocate_count++;  break;
        case FSD_MATCH_PARTIAL:   tracker->partial_count++;   break;
        case FSD_MATCH_ZERO:      tracker->zero_count++;      break;
        case FSD_MATCH_ONE:       tracker->one_count++;       break;
        default: break;
    }
}

void fsd_block_tracker_set_crc32(fsd_block_tracker_t *tracker,
                                 uint64_t dest_index,
                                 uint32_t crc32) {
    if (!tracker || dest_index >= tracker->count) return;
    tracker->blocks[dest_index].crc32 = crc32;
}

void fsd_block_tracker_set_delta(fsd_block_tracker_t *tracker,
                                 uint64_t dest_index,
                                 int64_t byte_offset,
                                 uint8_t *delta,
                                 uint32_t delta_size) {
    if (!tracker || dest_index >= tracker->count) return;

    fsd_block_state_t *block = &tracker->blocks[dest_index];
    block->byte_offset = byte_offset;
    block->delta = delta;
    block->delta_size = delta_size;
}

bool fsd_block_tracker_is_unmatched(const fsd_block_tracker_t *tracker,
                                    uint64_t dest_index) {
    if (!tracker || dest_index >= tracker->count) return false;
    return bitmap_test(tracker->unmatched_bitmap, dest_index);
}

const fsd_block_state_t *fsd_block_tracker_get(const fsd_block_tracker_t *tracker,
                                                uint64_t dest_index) {
    if (!tracker || dest_index >= tracker->count) return NULL;
    return &tracker->blocks[dest_index];
}

uint64_t fsd_block_tracker_unmatched_count(const fsd_block_tracker_t *tracker) {
    if (!tracker) return 0;

    uint64_t count = 0;
    for (size_t i = 0; i < tracker->bitmap_words; i++) {
        /* Count set bits using popcount */
        uint64_t word = tracker->unmatched_bitmap[i];
        /* Brian Kernighan's algorithm */
        while (word) {
            word &= (word - 1);
            count++;
        }
    }
    return count;
}

void fsd_block_tracker_foreach_unmatched(const fsd_block_tracker_t *tracker,
                                         fsd_unmatched_fn callback,
                                         void *user_data) {
    if (!tracker || !callback) return;

    for (uint64_t i = 0; i < tracker->count; i++) {
        if (bitmap_test(tracker->unmatched_bitmap, i)) {
            callback(i, user_data);
        }
    }
}

void fsd_block_tracker_finalize(fsd_block_tracker_t *tracker) {
    if (!tracker) return;

    /* Mark all remaining unmatched blocks as literals and clear bitmap */
    for (uint64_t i = 0; i < tracker->count; i++) {
        if (bitmap_test(tracker->unmatched_bitmap, i)) {
            /* Block is unmatched - becomes a literal */
            tracker->literal_count++;
            bitmap_clear(tracker->unmatched_bitmap, i);
        }
    }
}

void fsd_block_tracker_destroy(fsd_block_tracker_t *tracker) {
    if (!tracker) return;

    /* Note: delta buffers are allocated from the memory pool and are freed
     * when the pool is destroyed - do not free them individually here */

    free(tracker->unmatched_bitmap);
    free(tracker->blocks);
    free(tracker);
}
