/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file stage_fsmap.c
 * @brief Filesystem-aware matching stage implementation
 */

#include "stage_fsmap.h"
#include "block_search.h"
#include "../fs/ext_reader.h"
#include "../core/hash_table.h"
#include "../core/crc32.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define DEFAULT_SEARCH_RADIUS_BLOCKS 8
#define CANCEL_CHECK_INTERVAL 256

/* Source file table entry: path plus its extent list (in source fs blocks) */
typedef struct {
    const char *path;               /* pool-allocated, NUL-terminated */
    uint64_t size;
    const fsd_ext_extent_t *ext;    /* pool-allocated copy */
    size_t n_ext;
} src_file_t;

/* One verification job: dest fsdiff-block index -> hypothesized source byte */
typedef struct {
    uint64_t dest_block;
    uint64_t src_pos;
} hypothesis_t;

struct fsd_fsmap_stage {
    size_t block_size;              /* fsdiff block size */
    float threshold;
    size_t search_range;            /* bytes */
    int verbose;
    int inactive;                   /* probe failed: run() no-ops */
    const FSD_ATOMIC int *cancel;

    fsd_memory_pool_t *pool;        /* owns paths, extent copies, file table */
    fsd_hash_table_t *path_hash;    /* crc32(path) -> src_files index */
    src_file_t *src_files;          /* pool-allocated array (grown via chunks) */
    size_t n_src_files;
    size_t cap_src_files;

    hypothesis_t *hyps;             /* malloc'd, sorted by dest_block */
    size_t n_hyps;
    size_t cap_hyps;

    size_t src_size;                /* image sizes, for bounds checks */
    size_t dest_size;
};

/*===========================================================================
 * Create / destroy / setters
 *===========================================================================*/

fsd_error_t fsd_fsmap_stage_create(fsd_fsmap_stage_t **stage_out,
                                   size_t block_size,
                                   float threshold,
                                   int search_radius_blocks) {
    if (!stage_out || block_size == 0) {
        return FSD_ERR_INVALID_ARG;
    }
    fsd_fsmap_stage_t *stage = calloc(1, sizeof(*stage));
    if (!stage) {
        return FSD_ERR_OUT_OF_MEMORY;
    }
    stage->block_size = block_size;
    stage->threshold = (threshold > 0.0f && threshold < 1.0f) ? threshold : 0.5f;
    if (search_radius_blocks <= 0) {
        search_radius_blocks = DEFAULT_SEARCH_RADIUS_BLOCKS;
    }
    stage->search_range = (size_t)search_radius_blocks * block_size;
    stage->inactive = 1;            /* until build_index succeeds */
    *stage_out = stage;
    return FSD_SUCCESS;
}

void fsd_fsmap_stage_destroy(fsd_fsmap_stage_t *stage) {
    if (!stage) return;
    fsd_hash_table_destroy(stage->path_hash);
    fsd_pool_destroy(stage->pool);
    free(stage->hyps);
    free(stage);
}

void fsd_fsmap_stage_set_cancel(fsd_fsmap_stage_t *stage,
                                const FSD_ATOMIC int *flag) {
    if (stage) stage->cancel = flag;
}

void fsd_fsmap_stage_set_verbose(fsd_fsmap_stage_t *stage, int verbose) {
    if (stage) stage->verbose = verbose;
}

/*===========================================================================
 * Source walk: build the path-indexed file table
 *===========================================================================*/

typedef struct {
    fsd_fsmap_stage_t *stage;
    int oom;
} src_walk_ctx_t;

static int src_walk_cb(void *user, const char *path, uint64_t inode_no,
                       uint64_t file_size, const fsd_ext_extent_t *extents,
                       size_t n_extents) {
    (void)inode_no;
    src_walk_ctx_t *ctx = user;
    fsd_fsmap_stage_t *stage = ctx->stage;

    if (n_extents == 0) {
        return 0;                   /* inline/empty/fully-sparse: no blocks */
    }

    /* Grow the file table (pool allocations don't free, so grow by copy in
     * doubling chunks; total waste is bounded by one array's size). */
    if (stage->n_src_files == stage->cap_src_files) {
        size_t cap = stage->cap_src_files ? stage->cap_src_files * 2 : 1024;
        src_file_t *v = fsd_pool_alloc(stage->pool, cap * sizeof(*v));
        if (!v) { ctx->oom = 1; return 1; }
        if (stage->n_src_files) {
            memcpy(v, stage->src_files,
                   stage->n_src_files * sizeof(*v));
        }
        stage->src_files = v;
        stage->cap_src_files = cap;
    }

    size_t path_len = strlen(path);
    char *path_copy = fsd_pool_alloc(stage->pool, path_len + 1);
    fsd_ext_extent_t *ext_copy =
        fsd_pool_alloc(stage->pool, n_extents * sizeof(*ext_copy));
    if (!path_copy || !ext_copy) { ctx->oom = 1; return 1; }
    memcpy(path_copy, path, path_len + 1);
    memcpy(ext_copy, extents, n_extents * sizeof(*ext_copy));

    src_file_t *f = &stage->src_files[stage->n_src_files];
    f->path = path_copy;
    f->size = file_size;
    f->ext = ext_copy;
    f->n_ext = n_extents;

    fsd_hash_table_insert(stage->path_hash,
                          fsd_crc32(path, path_len),
                          (uint64_t)stage->n_src_files);
    stage->n_src_files++;
    return 0;
}

/* Look up a source file by path; NULL if absent. */
static const src_file_t *src_file_find(const fsd_fsmap_stage_t *stage,
                                       const char *path) {
    uint32_t crc = fsd_crc32(path, strlen(path));
    const fsd_hash_entry_t *e = fsd_hash_table_lookup(stage->path_hash, crc);
    for (; e; e = e->next) {
        if (e->crc32 != crc || e->block_index >= stage->n_src_files) {
            continue;
        }
        const src_file_t *f = &stage->src_files[e->block_index];
        if (strcmp(f->path, path) == 0) {
            return f;
        }
    }
    return NULL;
}

/* Map a file-logical byte range of `len` bytes to a source image byte
 * position, requiring the whole range to be contiguous within one extent.
 * Returns nonzero on success. */
static int src_map_range(const src_file_t *f, uint32_t fs_block_size,
                         uint64_t logical_byte, uint64_t len,
                         uint64_t *src_pos_out) {
    for (size_t i = 0; i < f->n_ext; i++) {
        uint64_t ext_lo = f->ext[i].logical * fs_block_size;
        uint64_t ext_bytes = f->ext[i].count * fs_block_size;
        if (logical_byte >= ext_lo && logical_byte + len <= ext_lo + ext_bytes) {
            *src_pos_out = f->ext[i].physical * fs_block_size +
                           (logical_byte - ext_lo);
            return 1;
        }
    }
    return 0;
}

/*===========================================================================
 * Dest walk: emit hypotheses for unmatched blocks
 *===========================================================================*/

typedef struct {
    fsd_fsmap_stage_t *stage;
    const fsd_block_tracker_t *tracker;
    uint32_t dest_fs_bs;
    uint32_t src_fs_bs;
    int oom;
    uint64_t files_no_src_path;
} dest_walk_ctx_t;

static int hyp_append(fsd_fsmap_stage_t *stage, uint64_t dest_block,
                      uint64_t src_pos) {
    if (stage->n_hyps == stage->cap_hyps) {
        size_t cap = stage->cap_hyps ? stage->cap_hyps * 2 : 4096;
        hypothesis_t *v = realloc(stage->hyps, cap * sizeof(*v));
        if (!v) return -1;
        stage->hyps = v;
        stage->cap_hyps = cap;
    }
    stage->hyps[stage->n_hyps].dest_block = dest_block;
    stage->hyps[stage->n_hyps].src_pos = src_pos;
    stage->n_hyps++;
    return 0;
}

static int dest_walk_cb(void *user, const char *path, uint64_t inode_no,
                        uint64_t file_size, const fsd_ext_extent_t *extents,
                        size_t n_extents) {
    (void)inode_no;
    (void)file_size;
    dest_walk_ctx_t *ctx = user;
    fsd_fsmap_stage_t *stage = ctx->stage;
    size_t bs = stage->block_size;
    const src_file_t *sf = NULL;
    int looked_up = 0;

    for (size_t e = 0; e < n_extents; e++) {
        uint64_t phys_lo = extents[e].physical * ctx->dest_fs_bs;
        uint64_t phys_hi = phys_lo + extents[e].count * ctx->dest_fs_bs;

        /* fsdiff blocks fully contained in this extent's physical range */
        uint64_t first = (phys_lo + bs - 1) / bs;
        uint64_t last = phys_hi / bs;       /* exclusive */
        for (uint64_t b = first; b < last; b++) {
            if (b >= ctx->tracker->count ||
                !fsd_block_tracker_is_unmatched(ctx->tracker, b)) {
                continue;
            }
            if (!looked_up) {               /* lazy: most files have no
                                             * unmatched blocks at all */
                sf = src_file_find(stage, path);
                looked_up = 1;
                if (!sf) ctx->files_no_src_path++;
            }
            if (!sf) {
                return 0;
            }
            uint64_t logical_byte = extents[e].logical * ctx->dest_fs_bs +
                                    (b * bs - phys_lo);
            uint64_t src_pos;
            if (!src_map_range(sf, ctx->src_fs_bs, logical_byte, bs, &src_pos)) {
                continue;                   /* hole/fragment boundary in src */
            }
            if (src_pos + bs > stage->src_size) {
                continue;
            }
            if (hyp_append(stage, b, src_pos) != 0) {
                ctx->oom = 1;
                return 1;
            }
        }
    }
    return 0;
}

/*===========================================================================
 * build_index
 *===========================================================================*/

static int hyp_cmp(const void *a, const void *b) {
    const hypothesis_t *ha = a, *hb = b;
    if (ha->dest_block != hb->dest_block) {
        return ha->dest_block < hb->dest_block ? -1 : 1;
    }
    return ha->src_pos < hb->src_pos ? -1 : (ha->src_pos > hb->src_pos ? 1 : 0);
}

fsd_error_t fsd_fsmap_stage_build_index(fsd_fsmap_stage_t *stage,
                                        const uint8_t *src, size_t src_size,
                                        const uint8_t *dest, size_t dest_size,
                                        const fsd_block_tracker_t *tracker) {
    if (!stage || !src || !dest || !tracker) {
        return FSD_ERR_INVALID_ARG;
    }
    stage->inactive = 1;
    stage->src_size = src_size;
    stage->dest_size = dest_size;

    const char *reason = "";
    fsd_extfs_t *src_fs = NULL;
    fsd_extfs_t *dest_fs = NULL;
    fsd_error_t err;

    err = fsd_extfs_open(&src_fs, src, src_size, &reason);
    if (err != FSD_SUCCESS) {
        if (err == FSD_ERR_OUT_OF_MEMORY) return err;
        if (stage->verbose) {
            fprintf(stderr, "[FSMap] Source: %s; stage disabled\n", reason);
        }
        return FSD_SUCCESS;
    }
    err = fsd_extfs_open(&dest_fs, dest, dest_size, &reason);
    if (err != FSD_SUCCESS) {
        fsd_extfs_close(src_fs);
        if (err == FSD_ERR_OUT_OF_MEMORY) return err;
        if (stage->verbose) {
            fprintf(stderr, "[FSMap] Destination: %s; stage disabled\n", reason);
        }
        return FSD_SUCCESS;
    }

    err = FSD_ERR_OUT_OF_MEMORY;
    if (fsd_pool_create(&stage->pool, 1024 * 1024, 0, NULL) != FSD_SUCCESS) {
        goto out;
    }
    if (fsd_hash_table_create(&stage->path_hash, 16384, stage->pool)
            != FSD_SUCCESS) {
        goto out;
    }

    src_walk_ctx_t sctx = { stage, 0 };
    if (fsd_extfs_walk(src_fs, src_walk_cb, &sctx) != FSD_SUCCESS ||
        sctx.oom) {
        goto out;
    }

    dest_walk_ctx_t dctx = {
        stage, tracker,
        fsd_extfs_block_size(dest_fs),
        fsd_extfs_block_size(src_fs),
        0, 0,
    };
    if (fsd_extfs_walk(dest_fs, dest_walk_cb, &dctx) != FSD_SUCCESS ||
        dctx.oom) {
        goto out;
    }

    /* Sort by dest block (walk order is directory order) and drop duplicate
     * hypotheses for the same block (hard links to the same inode). */
    if (stage->n_hyps > 1) {
        qsort(stage->hyps, stage->n_hyps, sizeof(*stage->hyps), hyp_cmp);
        size_t w = 1;
        for (size_t i = 1; i < stage->n_hyps; i++) {
            if (stage->hyps[i].dest_block != stage->hyps[w - 1].dest_block) {
                stage->hyps[w++] = stage->hyps[i];
            }
        }
        stage->n_hyps = w;
    }

    stage->inactive = 0;
    err = FSD_SUCCESS;
    if (stage->verbose) {
        fprintf(stderr,
                "[FSMap] Index: %zu source files, %zu hypotheses "
                "(%llu dest files without source path)\n",
                stage->n_src_files, stage->n_hyps,
                (unsigned long long)dctx.files_no_src_path);
    }

out:
    fsd_extfs_close(src_fs);
    fsd_extfs_close(dest_fs);
    return err;
}

/*===========================================================================
 * run
 *===========================================================================*/

fsd_error_t fsd_fsmap_stage_run(fsd_fsmap_stage_t *stage,
                                fsd_block_tracker_t *tracker,
                                const uint8_t *src,
                                const uint8_t *dest,
                                fsd_memory_pool_t *delta_pool) {
    if (!stage || !tracker || !src || !dest || !delta_pool) {
        return FSD_ERR_INVALID_ARG;
    }
    if (stage->inactive || stage->n_hyps == 0) {
        return FSD_SUCCESS;
    }

    size_t bs = stage->block_size;
    size_t threshold_count = (size_t)(stage->threshold * bs);
    if (threshold_count == 0) {
        threshold_count = 1;
    }
    fsd_count_matches_fn count_fn = fsd_block_search_count_fn();

    uint64_t exact_hits = 0, sweep_hits = 0, misses = 0, already = 0;

    for (size_t h = 0; h < stage->n_hyps; h++) {
        if (stage->cancel && (h % CANCEL_CHECK_INTERVAL) == 0 &&
            fsd_atomic_load(*stage->cancel)) {
            return FSD_ERR_CANCELLED;
        }

        uint64_t dest_idx = stage->hyps[h].dest_block;
        if (!fsd_block_tracker_is_unmatched(tracker, dest_idx)) {
            already++;
            continue;
        }
        const uint8_t *dest_block = dest + dest_idx * bs;
        int64_t hyp_pos = (int64_t)stage->hyps[h].src_pos;

        /* Exact hypothesis first (the common case: same file offset) */
        size_t best_count = count_fn(src + hyp_pos, dest_block, bs);
        int64_t src_pos = hyp_pos;
        int is_exact = 1;

        if (best_count < threshold_count) {
            /* Directed sweep around the hypothesis (file content shifted
             * internally, e.g. an insertion earlier in the file) */
            is_exact = 0;
            size_t sweep_count = 0;
            int64_t off = fsd_block_search_best(src, stage->src_size,
                                                dest_block, bs, hyp_pos,
                                                -(int64_t)stage->search_range,
                                                (int64_t)stage->search_range,
                                                threshold_count, count_fn,
                                                &sweep_count);
            if (sweep_count < threshold_count) {
                misses++;
                continue;
            }
            src_pos = hyp_pos + off;
            best_count = sweep_count;
            if (src_pos < 0 || (size_t)src_pos + bs > stage->src_size) {
                misses++;
                continue;
            }
        }

        uint8_t *delta = fsd_pool_alloc(delta_pool, bs);
        if (!delta) {
            continue;               /* same policy as the partial stage */
        }
        const uint8_t *src_block = src + src_pos;
        for (size_t i = 0; i < bs; i++) {
            delta[i] = dest_block[i] - src_block[i];
        }
        fsd_block_tracker_set_match(tracker, dest_idx, FSD_MATCH_PARTIAL,
                                    (uint64_t)src_pos / bs);
        fsd_block_tracker_set_delta(tracker, dest_idx,
                                    (int64_t)((uint64_t)src_pos % bs),
                                    delta, (uint32_t)bs);
        if (is_exact) exact_hits++; else sweep_hits++;
    }

    if (stage->verbose) {
        fprintf(stderr,
                "[FSMap] Complete: %llu exact, %llu directed-sweep, "
                "%llu misses, %llu already matched\n",
                (unsigned long long)exact_hits,
                (unsigned long long)sweep_hits,
                (unsigned long long)misses,
                (unsigned long long)already);
    }
    return FSD_SUCCESS;
}
