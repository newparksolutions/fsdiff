/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file stage_relocation.c
 * @brief Stage 2: Relocation matching implementation
 */

#include "stage_relocation.h"
#include "../core/crc32.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Check if block is all zeros (for excluding from hash table) */
static int is_zero_block_quick(const uint8_t *block, size_t size) {
    /* Quick check using first and last words */
    if (size >= 16) {
        const uint64_t *ptr = (const uint64_t *)block;
        if (ptr[0] != 0) return 0;
        if (ptr[size / 8 - 1] != 0) return 0;
    }

    /* Full check */
    const uint64_t *ptr = (const uint64_t *)block;
    size_t count = size / sizeof(uint64_t);
    for (size_t i = 0; i < count; i++) {
        if (ptr[i] != 0) return 0;
    }
    return 1;
}

fsd_error_t fsd_relocation_stage_create(fsd_relocation_stage_t **stage_out,
                                        size_t block_size,
                                        uint64_t src_blocks) {
    if (!stage_out || block_size == 0) {
        return FSD_ERR_INVALID_ARG;
    }

    fsd_relocation_stage_t *stage = calloc(1, sizeof(fsd_relocation_stage_t));
    if (!stage) {
        return FSD_ERR_OUT_OF_MEMORY;
    }

    /* Create memory pool for hash table */
    fsd_error_t err = fsd_pool_create(&stage->pool, 1024 * 1024, 0, NULL);
    if (err != FSD_SUCCESS) {
        free(stage);
        return err;
    }

    stage->block_size = block_size;
    stage->src_blocks = src_blocks;
    stage->src_hash = NULL;

    *stage_out = stage;
    return FSD_SUCCESS;
}

fsd_error_t fsd_relocation_stage_build_index(fsd_relocation_stage_t *stage,
                                             const uint8_t *src) {
    if (!stage || !src) {
        return FSD_ERR_INVALID_ARG;
    }

    if (stage->verbose) {
        fprintf(stderr, "[Relocation] Building hash index for %lu source blocks\n",
                (unsigned long)stage->src_blocks);
    }

    /* Create hash table */
    fsd_error_t err = fsd_hash_table_create(&stage->src_hash,
                                            stage->src_blocks,
                                            stage->pool);
    if (err != FSD_SUCCESS) {
        return err;
    }

    uint64_t indexed_count = 0;
    uint64_t zero_skipped = 0;

    /* Build index of all non-zero source blocks */
    for (uint64_t i = 0; i < stage->src_blocks; i++) {
        const uint8_t *block = src + (i * stage->block_size);

        /* Progress update */
        if (stage->verbose && (i % 100000 == 0 || i == stage->src_blocks - 1)) {
            int pct = (stage->src_blocks > 0) ? (int)((i * 100) / stage->src_blocks) : 100;
            fprintf(stderr, "\r[Relocation] Indexing block %lu/%lu (%d%%)",
                    (unsigned long)i, (unsigned long)stage->src_blocks, pct);
            fflush(stderr);
        }

        /* Skip zero blocks (they would cause many false matches) */
        if (is_zero_block_quick(block, stage->block_size)) {
            zero_skipped++;
            continue;
        }

        uint32_t crc = fsd_crc32(block, stage->block_size);
        fsd_hash_table_insert(stage->src_hash, crc, i);
        indexed_count++;
    }

    if (stage->verbose) {
        fprintf(stderr, "\n[Relocation] Index complete: %lu blocks indexed, %lu zero blocks skipped\n",
                (unsigned long)indexed_count, (unsigned long)zero_skipped);
    }

    return FSD_SUCCESS;
}

fsd_error_t fsd_relocation_stage_run(fsd_relocation_stage_t *stage,
                                     fsd_block_tracker_t *tracker,
                                     const uint8_t *src,
                                     const uint8_t *dest) {
    if (!stage || !tracker || !src || !dest) {
        return FSD_ERR_INVALID_ARG;
    }

    if (!stage->src_hash) {
        return FSD_ERR_NOT_INITIALIZED;
    }

    size_t block_size = stage->block_size;

    /* Count unmatched blocks for progress */
    uint64_t unmatched_total = fsd_block_tracker_unmatched_count(tracker);
    uint64_t unmatched_processed = 0;
    uint64_t relocate_count = 0;
    uint64_t crc_collisions = 0;

    if (stage->verbose) {
        fprintf(stderr, "[Relocation] Searching for %lu unmatched blocks\n",
                (unsigned long)unmatched_total);
    }

    /* Process each unmatched destination block */
    for (uint64_t dest_idx = 0; dest_idx < tracker->count; dest_idx++) {
        /* Skip already matched blocks */
        if (!fsd_block_tracker_is_unmatched(tracker, dest_idx)) {
            continue;
        }

        unmatched_processed++;

        /* Progress update */
        if (stage->verbose && (unmatched_processed % 10000 == 0 || unmatched_processed == unmatched_total)) {
            int pct = (unmatched_total > 0) ? (int)((unmatched_processed * 100) / unmatched_total) : 100;
            fprintf(stderr, "\r[Relocation] Block %lu/%lu (%d%%) - %lu matches found",
                    (unsigned long)unmatched_processed, (unsigned long)unmatched_total,
                    pct, (unsigned long)relocate_count);
            fflush(stderr);
        }

        const fsd_block_state_t *state = fsd_block_tracker_get(tracker, dest_idx);
        uint32_t dest_crc = state->crc32;

        /* Look up in source hash table */
        const fsd_hash_entry_t *entry = fsd_hash_table_lookup(stage->src_hash, dest_crc);

        while (entry) {
            uint64_t src_idx = entry->block_index;

            /* Skip if same position (would be identity match) */
            if (src_idx == dest_idx) {
                entry = entry->next;
                continue;
            }

            /* Verify match with full comparison (handle CRC collisions) */
            const uint8_t *src_block = src + (src_idx * block_size);
            const uint8_t *dest_block = dest + (dest_idx * block_size);

            if (memcmp(src_block, dest_block, block_size) == 0) {
                /* Found a match */
                fsd_block_tracker_set_match(tracker, dest_idx, FSD_MATCH_RELOCATE, src_idx);
                relocate_count++;
                break;
            }

            /* CRC collision - try next entry with same CRC */
            crc_collisions++;
            entry = entry->next;
        }
    }

    if (stage->verbose) {
        fprintf(stderr, "\n[Relocation] Complete: %lu relocations found, %lu CRC collisions\n",
                (unsigned long)relocate_count, (unsigned long)crc_collisions);
    }

    return FSD_SUCCESS;
}

void fsd_relocation_stage_set_verbose(fsd_relocation_stage_t *stage, int verbose) {
    if (stage) {
        stage->verbose = verbose;
    }
}

void fsd_relocation_stage_destroy(fsd_relocation_stage_t *stage) {
    if (!stage) return;

    fsd_hash_table_destroy(stage->src_hash);
    fsd_pool_destroy(stage->pool);
    free(stage);
}
