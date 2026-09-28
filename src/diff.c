/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file diff.c
 * @brief Public diff API implementation
 */

/* Enable POSIX.1-2008 features (fdopen) before any includes */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <fsdiff/fsdiff.h>
#include "platform.h"
#include "stages/stage_controller.h"
#include "encoding/bkdf_header.h"
#include "encoding/operation_encoder.h"
#include "io/mmap_reader.h"
#include "io/source_reader.h"
#include "io/buffered_writer.h"
#include "simd/simd_dispatch.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct fsd_diff_ctx {
    fsd_diff_options_t opts;
    fsd_diff_stats_t stats;
    fsd_progress_fn progress_cb;
    void *progress_user_data;
    FSD_ATOMIC int cancelled;
};

/**
 * Diagnostic match log, enabled by setting FSDIFF_MATCH_LOG=<path> in the
 * environment. Writes one CSV row per destination block describing what the
 * matching stages decided, for offline analysis (ground truth for evaluating
 * alternative matchers, distance/quality scatter of partial matches). Not part
 * of the public API; zero cost when the variable is unset.
 *
 * Columns: dest_index,match_type,src_index,byte_offset,rel_offset,match_bytes
 *   match_type   0=literal 1=identity 2=relocate 3=partial 4=zero 5=one
 *   rel_offset   signed source-minus-dest byte distance (empty for
 *                literal/zero/one)
 *   match_bytes  block_size for identity/relocate; recounted exactly for
 *                partial; empty otherwise
 */
static void fsd_write_match_log(const char *path,
                                const fsd_block_tracker_t *tracker,
                                const uint8_t *src_data, size_t src_size,
                                const uint8_t *dest_data, size_t block_size) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "fsdiff: cannot open FSDIFF_MATCH_LOG path '%s'\n", path);
        return;
    }

    fsd_count_matches_fn count_fn = g_fsd_simd.count_matches
                                        ? g_fsd_simd.count_matches
                                        : fsd_scalar_count_matches;

    fprintf(f, "dest_index,match_type,src_index,byte_offset,rel_offset,match_bytes\n");
    for (uint64_t i = 0; i < tracker->count; i++) {
        const fsd_block_state_t *st = fsd_block_tracker_get(tracker, i);
        int64_t dest_pos = (int64_t)(i * block_size);

        switch (st->match_type) {
        case FSD_MATCH_IDENTITY:
        case FSD_MATCH_RELOCATE: {
            int64_t src_pos = (int64_t)(st->src_index * block_size);
            fprintf(f, "%llu,%d,%llu,0,%lld,%zu\n",
                    (unsigned long long)i, (int)st->match_type,
                    (unsigned long long)st->src_index,
                    (long long)(src_pos - dest_pos), block_size);
            break;
        }
        case FSD_MATCH_PARTIAL: {
            int64_t src_pos = (int64_t)(st->src_index * block_size) + st->byte_offset;
            size_t match_bytes = 0;
            if (src_pos >= 0 && (size_t)src_pos + block_size <= src_size) {
                match_bytes = count_fn(src_data + src_pos,
                                       dest_data + dest_pos, block_size);
            }
            fprintf(f, "%llu,%d,%llu,%lld,%lld,%zu\n",
                    (unsigned long long)i, (int)st->match_type,
                    (unsigned long long)st->src_index,
                    (long long)st->byte_offset,
                    (long long)(src_pos - dest_pos), match_bytes);
            break;
        }
        default:
            /* Literal / zero / one: no source reference */
            fprintf(f, "%llu,%d,,,,\n", (unsigned long long)i, (int)st->match_type);
            break;
        }
    }
    fclose(f);
}

/**
 * Copy a temp stream into the patch file, verifying that exactly `expected`
 * bytes come back. The length check catches a temp file that ended up shorter
 * than the size recorded in the header (e.g. a write lost to ENOSPC); ferror
 * distinguishes a read error from a clean EOF.
 */
static fsd_error_t fsd_copy_stream(FILE *src, FILE *dst, size_t expected) {
    uint8_t copy_buf[65536];
    size_t copied = 0;
    size_t n;

    rewind(src);
    while ((n = fread(copy_buf, 1, sizeof(copy_buf), src)) > 0) {
        if (fwrite(copy_buf, 1, n, dst) != n) {
            return FSD_ERR_IO;
        }
        copied += n;
    }
    if (ferror(src) || copied != expected) {
        return FSD_ERR_IO;
    }
    return FSD_SUCCESS;
}

void fsd_diff_options_init(fsd_diff_options_t *opts) {
    if (!opts) return;

    memset(opts, 0, sizeof(*opts));
    opts->block_size_log2 = 12;  /* 4096 */
    opts->enable_identity = true;
    opts->enable_relocation = true;
    opts->enable_partial = true;
    opts->enable_fsmap = true;
    opts->partial_threshold = 0.5f;
    opts->search_radius = 8;
    opts->max_memory_mb = 0;
    opts->force_scalar = false;
    opts->source_mode = FSD_SOURCE_AUTO;
    opts->allocator = NULL;
}

fsd_error_t fsd_diff_create(fsd_diff_ctx_t **ctx_out,
                            const fsd_diff_options_t *opts) {
    if (!ctx_out) {
        return FSD_ERR_INVALID_ARG;
    }

    fsd_diff_ctx_t *ctx = calloc(1, sizeof(fsd_diff_ctx_t));
    if (!ctx) {
        return FSD_ERR_OUT_OF_MEMORY;
    }

    if (opts) {
        ctx->opts = *opts;
    } else {
        fsd_diff_options_init(&ctx->opts);
    }

    /* Validate block size range */
    if (ctx->opts.block_size_log2 < 5 || ctx->opts.block_size_log2 > 24) {
        free(ctx);
        return FSD_ERR_BAD_BLOCK_SIZE;
    }

    ctx->progress_cb = NULL;
    ctx->progress_user_data = NULL;
    ctx->cancelled = 0;

    memset(&ctx->stats, 0, sizeof(ctx->stats));

    *ctx_out = ctx;
    return FSD_SUCCESS;
}

fsd_error_t fsd_diff_files(fsd_diff_ctx_t *ctx,
                           const char *src_path,
                           const char *dest_path,
                           const char *output_path) {
    if (!ctx || !src_path || !dest_path || !output_path) {
        return FSD_ERR_INVALID_ARG;
    }

    clock_t start_time = clock();
    fsd_error_t err;

    /* Note: ctx->cancelled is deliberately not cleared here. It is zeroed in
     * fsd_diff_create and again on the cancelled return path below, so a
     * cancel that races with the start of a run is honoured rather than
     * discarded. */

    /* Honor the force-scalar option. The SIMD dispatch table is process-
     * global (set up by fsd_init), so this affects subsequent operations
     * too — callers that want SIMD back must call fsd_init() again. */
    if (ctx->opts.force_scalar) {
        fsd_simd_force_scalar();
    }

    /* Open source via the source_reader abstraction. Auto-detect routes
     * block-device sources through O_DIRECT to bypass the bdev page cache
     * (which would otherwise be polluted by a mounted FS driver's dirty
     * metadata buffers). The destination is a normal file in the typical
     * build-host workflow, so it stays on mmap. */
    fsd_source_reader_t *src_reader = NULL;
    fsd_mmap_reader_t *dest_reader = NULL;

    err = fsd_source_reader_open(&src_reader, src_path, ctx->opts.source_mode);
    if (err != FSD_SUCCESS) return err;

    err = fsd_mmap_open(&dest_reader, dest_path);
    if (err != FSD_SUCCESS) {
        fsd_source_reader_close(src_reader);
        return err;
    }

    /* The matching stages need a contiguous base pointer for random access.
     * On the direct backend this slurps the source into an aligned buffer
     * (cost = source size); on mmap this is a zero-cost pointer return. */
    const uint8_t *src_data = NULL;
    err = fsd_source_reader_data(src_reader, (const void **)&src_data);
    if (err != FSD_SUCCESS) {
        fsd_mmap_close(dest_reader);
        fsd_source_reader_close(src_reader);
        return err;
    }
    size_t src_size = fsd_source_reader_size(src_reader);
    const uint8_t *dest_data = fsd_mmap_data(dest_reader);
    size_t dest_size = fsd_mmap_size(dest_reader);

    size_t block_size = 1ULL << ctx->opts.block_size_log2;
    uint64_t src_blocks = src_size / block_size;
    uint64_t dest_blocks = dest_size / block_size;

    /* The patch format is block-oriented: the header records only dest_blocks,
     * so a reconstructed image is always a whole number of blocks. If the
     * destination size is not a block multiple, its trailing partial block
     * cannot be represented and would be silently lost. Reject rather than
     * produce a patch that reconstructs a truncated image. (The source may be
     * any size; its trailing partial block is simply unused as reference.) */
    if (dest_size % block_size != 0) {
        fsd_mmap_close(dest_reader);
        fsd_source_reader_close(src_reader);
        return FSD_ERR_SIZE_MISMATCH;
    }

    /* Create stage controller */
    fsd_stage_controller_t *controller = NULL;
    err = fsd_stage_controller_create(&controller, &ctx->opts, src_blocks, dest_blocks);
    if (err != FSD_SUCCESS) {
        fsd_mmap_close(dest_reader);
        fsd_source_reader_close(src_reader);
        return err;
    }

    /* Route the public cancel flag to the (function-local) controller so
     * fsd_diff_cancel from another thread is observed between stages. */
    fsd_stage_controller_set_cancel_flag(controller, &ctx->cancelled);

    /* Set progress callback */
    if (ctx->progress_cb) {
        fsd_stage_controller_set_progress(controller, ctx->progress_cb, ctx->progress_user_data);
    }

    /* Set verbose flag for all stages */
    if (ctx->opts.verbose) {
        fsd_stage_controller_set_verbose(controller, 1);
    }

    /* Run matching stages */
    err = fsd_stage_controller_run(controller, src_data, src_size, dest_data, dest_size);
    if (err != FSD_SUCCESS) {
        fsd_stage_controller_destroy(controller);
        fsd_mmap_close(dest_reader);
        fsd_source_reader_close(src_reader);
        if (err == FSD_ERR_CANCELLED) {
            /* The request has been honoured; reset so the context can be
             * reused for another run. */
            fsd_atomic_store(ctx->cancelled, 0);
        }
        return err;
    }

    /* Get block tracker with results */
    fsd_block_tracker_t *tracker = fsd_stage_controller_get_tracker(controller);

    /* Optional diagnostic match log (see fsd_write_match_log above) */
    {
        const char *match_log_path = getenv("FSDIFF_MATCH_LOG");
        if (match_log_path && *match_log_path) {
            fsd_write_match_log(match_log_path, tracker,
                                src_data, src_size, dest_data, block_size);
        }
    }

    /* Create temporary files for streams */
    char op_tmp[256];
    char diff_tmp[256];
    char lit_tmp[256];

    int op_fd = fsd_create_temp_file("fsdiff_op", op_tmp);
    int diff_fd = fsd_create_temp_file("fsdiff_diff", diff_tmp);
    int lit_fd = fsd_create_temp_file("fsdiff_lit", lit_tmp);

    if (op_fd < 0 || diff_fd < 0 || lit_fd < 0) {
        if (op_fd >= 0) { fsd_close(op_fd); fsd_unlink(op_tmp); }
        if (diff_fd >= 0) { fsd_close(diff_fd); fsd_unlink(diff_tmp); }
        if (lit_fd >= 0) { fsd_close(lit_fd); fsd_unlink(lit_tmp); }
        fsd_stage_controller_destroy(controller);
        fsd_mmap_close(dest_reader);
        fsd_source_reader_close(src_reader);
        return FSD_ERR_IO;
    }

    FILE *op_file = fdopen(op_fd, "w+b");
    FILE *diff_file = fdopen(diff_fd, "w+b");
    FILE *lit_file = fdopen(lit_fd, "w+b");

    if (!op_file || !diff_file || !lit_file) {
        if (op_file) fclose(op_file); else if (op_fd >= 0) fsd_close(op_fd);
        if (diff_file) fclose(diff_file); else if (diff_fd >= 0) fsd_close(diff_fd);
        if (lit_file) fclose(lit_file); else if (lit_fd >= 0) fsd_close(lit_fd);
        fsd_unlink(op_tmp);
        fsd_unlink(diff_tmp);
        fsd_unlink(lit_tmp);
        fsd_stage_controller_destroy(controller);
        fsd_mmap_close(dest_reader);
        fsd_source_reader_close(src_reader);
        return FSD_ERR_IO;
    }

    /* Create writers */
    fsd_buffered_writer_t *op_writer = NULL;
    fsd_buffered_writer_t *diff_writer = NULL;
    fsd_buffered_writer_t *lit_writer = NULL;

    /* Set once the output file has been created, so cleanup can remove a
     * partial patch on error (never for special files; see platform.h). */
    int unlink_output_on_error = 0;

    fsd_writer_create_from_file(&op_writer, op_file, 0);
    fsd_writer_create_from_file(&diff_writer, diff_file, 0);
    fsd_writer_create_from_file(&lit_writer, lit_file, 0);

    /* Create encoder and encode operations */
    fsd_op_encoder_t *encoder = NULL;
    err = fsd_op_encoder_create(&encoder, block_size);
    if (err != FSD_SUCCESS) goto cleanup;

    err = fsd_op_encoder_encode(encoder, tracker, op_writer, diff_writer, lit_writer, dest_data);
    if (err != FSD_SUCCESS) goto cleanup;

    /* Flush writers, then the stdio buffers beneath them. A flush failure
     * (e.g. ENOSPC on the temp stream) means the temp files are incomplete, so
     * the stream lengths we are about to bake into the header would not match
     * their contents — treat it as a hard error rather than emitting a
     * structurally corrupt patch. The fflush is needed because
     * fsd_writer_flush only hands data to stdio; without it the final buffer
     * would be written by rewind(), which cannot report failure. */
    err = fsd_writer_flush(op_writer);
    if (err != FSD_SUCCESS) goto cleanup;
    err = fsd_writer_flush(diff_writer);
    if (err != FSD_SUCCESS) goto cleanup;
    err = fsd_writer_flush(lit_writer);
    if (err != FSD_SUCCESS) goto cleanup;
    if (fflush(op_file) != 0 || fflush(diff_file) != 0 || fflush(lit_file) != 0) {
        err = FSD_ERR_IO;
        goto cleanup;
    }

    size_t op_size = fsd_writer_bytes_written(op_writer);
    size_t diff_size = fsd_writer_bytes_written(diff_writer);
    size_t lit_size = fsd_writer_bytes_written(lit_writer);

    /* Write final output file */
    int may_unlink_output = fsd_path_is_regular_or_missing(output_path);
    FILE *output = fopen(output_path, "wb");
    if (!output) {
        err = FSD_ERR_IO;
        goto cleanup;
    }
    unlink_output_on_error = may_unlink_output;

    /* Write header, then the three streams */
    err = fsd_header_write(output, dest_blocks, ctx->opts.block_size_log2, op_size, diff_size);
    if (err == FSD_SUCCESS) err = fsd_copy_stream(op_file, output, op_size);
    if (err == FSD_SUCCESS) err = fsd_copy_stream(diff_file, output, diff_size);
    if (err == FSD_SUCCESS) err = fsd_copy_stream(lit_file, output, lit_size);

    /* Check fclose: buffered writes to the patch file are flushed here, so a
     * full disk surfaces as an fclose failure rather than a prior fwrite one. */
    if (fclose(output) != 0 && err == FSD_SUCCESS) {
        err = FSD_ERR_IO;
    }
    if (err != FSD_SUCCESS) goto cleanup;

    /* Update statistics */
    ctx->stats.total_blocks = dest_blocks;
    ctx->stats.identity_matches = tracker->identity_count;
    ctx->stats.relocate_matches = tracker->relocate_count;
    ctx->stats.partial_matches = tracker->partial_count;
    ctx->stats.zero_blocks = tracker->zero_count;
    ctx->stats.one_blocks = tracker->one_count;
    ctx->stats.literal_blocks = tracker->literal_count;
    ctx->stats.patch_size = FSD_HEADER_SIZE + op_size + diff_size + lit_size;
    ctx->stats.elapsed_seconds = (double)(clock() - start_time) / CLOCKS_PER_SEC;

cleanup:
    fsd_op_encoder_destroy(encoder);
    /* Transfer FILE ownership to writers so fsd_writer_close handles fclose */
    if (op_writer) op_writer->owns_file = true;
    if (diff_writer) diff_writer->owns_file = true;
    if (lit_writer) lit_writer->owns_file = true;
    fsd_writer_close(op_writer);
    fsd_writer_close(diff_writer);
    fsd_writer_close(lit_writer);
    fsd_unlink(op_tmp);
    fsd_unlink(diff_tmp);
    fsd_unlink(lit_tmp);
    if (err != FSD_SUCCESS && unlink_output_on_error) {
        fsd_unlink(output_path);
    }
    fsd_stage_controller_destroy(controller);
    fsd_mmap_close(dest_reader);
    fsd_source_reader_close(src_reader);

    return err;
}

void fsd_diff_set_progress(fsd_diff_ctx_t *ctx,
                           fsd_progress_fn callback,
                           void *user_data) {
    if (!ctx) return;
    ctx->progress_cb = callback;
    ctx->progress_user_data = user_data;
}

void fsd_diff_cancel(fsd_diff_ctx_t *ctx) {
    if (ctx) {
        fsd_atomic_store(ctx->cancelled, 1);
    }
}

fsd_error_t fsd_diff_get_stats(const fsd_diff_ctx_t *ctx,
                               fsd_diff_stats_t *stats_out) {
    if (!ctx || !stats_out) {
        return FSD_ERR_INVALID_ARG;
    }
    *stats_out = ctx->stats;
    return FSD_SUCCESS;
}

void fsd_diff_destroy(fsd_diff_ctx_t *ctx) {
    free(ctx);
}
