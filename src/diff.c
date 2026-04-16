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
#include "io/buffered_writer.h"
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

void fsd_diff_options_init(fsd_diff_options_t *opts) {
    if (!opts) return;

    memset(opts, 0, sizeof(*opts));
    opts->block_size_log2 = 12;  /* 4096 */
    opts->enable_identity = true;
    opts->enable_relocation = true;
    opts->enable_partial = true;
    opts->partial_threshold = 0.5f;
    opts->search_radius = 8;
    opts->num_projections = 1;
    opts->use_freq_weighting = false;
    opts->max_memory_mb = 0;
    opts->force_scalar = false;
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

    /* Memory-map source and destination */
    fsd_mmap_reader_t *src_reader = NULL;
    fsd_mmap_reader_t *dest_reader = NULL;

    err = fsd_mmap_open(&src_reader, src_path);
    if (err != FSD_SUCCESS) return err;

    err = fsd_mmap_open(&dest_reader, dest_path);
    if (err != FSD_SUCCESS) {
        fsd_mmap_close(src_reader);
        return err;
    }

    const uint8_t *src_data = fsd_mmap_data(src_reader);
    size_t src_size = fsd_mmap_size(src_reader);
    const uint8_t *dest_data = fsd_mmap_data(dest_reader);
    size_t dest_size = fsd_mmap_size(dest_reader);

    size_t block_size = 1ULL << ctx->opts.block_size_log2;
    uint64_t src_blocks = src_size / block_size;
    uint64_t dest_blocks = dest_size / block_size;

    /* Create stage controller */
    fsd_stage_controller_t *controller = NULL;
    err = fsd_stage_controller_create(&controller, &ctx->opts, src_blocks, dest_blocks);
    if (err != FSD_SUCCESS) {
        fsd_mmap_close(dest_reader);
        fsd_mmap_close(src_reader);
        return err;
    }

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
        fsd_mmap_close(src_reader);
        return err;
    }

    /* Get block tracker with results */
    fsd_block_tracker_t *tracker = fsd_stage_controller_get_tracker(controller);

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
        fsd_mmap_close(src_reader);
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
        fsd_mmap_close(src_reader);
        return FSD_ERR_IO;
    }

    /* Create writers */
    fsd_buffered_writer_t *op_writer = NULL;
    fsd_buffered_writer_t *diff_writer = NULL;
    fsd_buffered_writer_t *lit_writer = NULL;

    fsd_writer_create_from_file(&op_writer, op_file, 0);
    fsd_writer_create_from_file(&diff_writer, diff_file, 0);
    fsd_writer_create_from_file(&lit_writer, lit_file, 0);

    /* Create encoder and encode operations */
    fsd_op_encoder_t *encoder = NULL;
    fsd_op_encoder_create(&encoder, block_size);

    err = fsd_op_encoder_encode(encoder, tracker, op_writer, diff_writer, lit_writer, dest_data);

    /* Flush writers */
    fsd_writer_flush(op_writer);
    fsd_writer_flush(diff_writer);
    fsd_writer_flush(lit_writer);

    size_t op_size = fsd_writer_bytes_written(op_writer);
    size_t diff_size = fsd_writer_bytes_written(diff_writer);
    size_t lit_size = fsd_writer_bytes_written(lit_writer);

    /* Write final output file */
    FILE *output = fopen(output_path, "wb");
    if (!output) {
        err = FSD_ERR_IO;
        goto cleanup;
    }

    /* Write header */
    err = fsd_header_write(output, dest_blocks, ctx->opts.block_size_log2, op_size, diff_size);
    if (err != FSD_SUCCESS) {
        fclose(output);
        goto cleanup;
    }

    /* Copy streams to output */
    uint8_t copy_buf[65536];
    size_t n;

    /* Copy operation stream */
    rewind(op_file);
    while ((n = fread(copy_buf, 1, sizeof(copy_buf), op_file)) > 0) {
        if (fwrite(copy_buf, 1, n, output) != n) {
            err = FSD_ERR_IO;
            fclose(output);
            goto cleanup;
        }
    }

    /* Copy diff stream */
    rewind(diff_file);
    while ((n = fread(copy_buf, 1, sizeof(copy_buf), diff_file)) > 0) {
        if (fwrite(copy_buf, 1, n, output) != n) {
            err = FSD_ERR_IO;
            fclose(output);
            goto cleanup;
        }
    }

    /* Copy literal stream */
    rewind(lit_file);
    while ((n = fread(copy_buf, 1, sizeof(copy_buf), lit_file)) > 0) {
        if (fwrite(copy_buf, 1, n, output) != n) {
            err = FSD_ERR_IO;
            fclose(output);
            goto cleanup;
        }
    }

    fclose(output);

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
    fsd_stage_controller_destroy(controller);
    fsd_mmap_close(dest_reader);
    fsd_mmap_close(src_reader);

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
