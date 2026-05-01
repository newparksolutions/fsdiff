/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file patch.c
 * @brief Public patch API implementation
 */

#include <fsdiff/fsdiff.h>
#include "platform.h"
#include "encoding/bkdf_header.h"
#include "io/mmap_reader.h"
#include "io/source_reader.h"
#include "simd/simd_dispatch.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct fsd_patch_ctx {
    fsd_patch_options_t opts;
    fsd_progress_fn progress_cb;
    void *progress_user_data;
};

void fsd_patch_options_init(fsd_patch_options_t *opts) {
    if (!opts) return;
    memset(opts, 0, sizeof(*opts));
    opts->verify_output = false;
    opts->source_mode = FSD_SOURCE_AUTO;
    opts->allocator = NULL;
}

fsd_error_t fsd_patch_create(fsd_patch_ctx_t **ctx_out,
                             const fsd_patch_options_t *opts) {
    if (!ctx_out) {
        return FSD_ERR_INVALID_ARG;
    }

    fsd_patch_ctx_t *ctx = calloc(1, sizeof(fsd_patch_ctx_t));
    if (!ctx) {
        return FSD_ERR_OUT_OF_MEMORY;
    }

    if (opts) {
        ctx->opts = *opts;
    } else {
        fsd_patch_options_init(&ctx->opts);
    }

    ctx->progress_cb = NULL;
    ctx->progress_user_data = NULL;

    *ctx_out = ctx;
    return FSD_SUCCESS;
}

/**
 * Decode base-128 varint from stream (little-endian).
 * Advances pointer past the varint bytes.
 *
 * @param ptr        Pointer to current position in stream (updated)
 * @param end        End of valid data
 * @param value_out  Output value
 * @return           FSD_SUCCESS, FSD_ERR_TRUNCATED, or FSD_ERR_CORRUPT_DATA
 */
static fsd_error_t read_varint(const uint8_t **ptr, const uint8_t *end, size_t *value_out) {
    size_t value = 0;
    size_t shift = 0;

    while (*ptr < end) {
        uint8_t byte = *(*ptr)++;
        value |= (size_t)(byte & 0x7F) << shift;

        if ((byte & 0x80) == 0) {
            /* Last byte (continuation bit not set) */
            *value_out = value;
            return FSD_SUCCESS;
        }

        shift += 7;
        if (shift >= 64) {
            /* Overflow - varint too large */
            return FSD_ERR_CORRUPT_DATA;
        }
    }

    return FSD_ERR_TRUNCATED;
}

/* Read variable-length count from operation stream */
static fsd_error_t read_count(const uint8_t **ptr, const uint8_t *end,
                              uint8_t first_byte, uint64_t *count_out) {
    uint8_t count_enc = (first_byte >> 3) & 0x3;

    switch (count_enc) {
    case 0:  /* 1 byte follows */
        if (*ptr >= end) return FSD_ERR_TRUNCATED;
        *count_out = (uint64_t)(*(*ptr)++) + 1;
        break;
    case 1:  /* 2 bytes LE follow */
        if (*ptr + 2 > end) return FSD_ERR_TRUNCATED;
        *count_out = (uint64_t)((*ptr)[0] | ((*ptr)[1] << 8)) + 1;
        *ptr += 2;
        break;
    case 2:  /* 4 bytes LE follow */
        if (*ptr + 4 > end) return FSD_ERR_TRUNCATED;
        *count_out = (uint32_t)(*ptr)[0] | ((uint32_t)(*ptr)[1] << 8) |
                     ((uint32_t)(*ptr)[2] << 16) | ((uint32_t)(*ptr)[3] << 24);
        *ptr += 4;
        break;
    case 3:  /* Inline: bits[2:0] + 1 */
        *count_out = (first_byte & 0x7) + 1;
        break;
    }

    return FSD_SUCCESS;
}

fsd_error_t fsd_patch_apply(fsd_patch_ctx_t *ctx,
                            const char *src_path,
                            const char *patch_path,
                            const char *output_path) {
    if (!ctx || !src_path || !patch_path || !output_path) {
        return FSD_ERR_INVALID_ARG;
    }

    fsd_error_t err;

    /* Open source. Auto-detect picks O_DIRECT for block devices to avoid
     * the bdev page cache aliased with the FS driver's dirty metadata
     * buffers on a read-only mount; regular files use mmap. */
    fsd_source_reader_t *src_reader = NULL;
    err = fsd_source_reader_open(&src_reader, src_path, ctx->opts.source_mode);
    if (err != FSD_SUCCESS) return err;

    /* Open patch file (mmap is fine — patches are normally separate files,
     * not on the partition being patched). */
    fsd_mmap_reader_t *patch_reader = NULL;
    err = fsd_mmap_open(&patch_reader, patch_path);
    if (err != FSD_SUCCESS) {
        fsd_source_reader_close(src_reader);
        return err;
    }

    const uint8_t *patch_data = fsd_mmap_data(patch_reader);
    size_t patch_size = fsd_mmap_size(patch_reader);
    size_t src_size = fsd_source_reader_size(src_reader);

    /* Read and validate header */
    fsd_header_t header;
    err = fsd_header_read_memory(patch_data, patch_size, &header);
    if (err != FSD_SUCCESS) {
        fsd_mmap_close(patch_reader);
        fsd_source_reader_close(src_reader);
        return err;
    }

    size_t block_size = 1ULL << header.block_size_log2;

    /* Cap dest_blocks so that dest_blocks * block_size fits in int64_t.
     * Subsequent block-offset arithmetic uses signed 64-bit (COPY_RELOCATE
     * and COPY_ADD), so this single check makes all downstream multiplications
     * and casts well-defined; without it a malicious header can wrap. */
    if (header.dest_blocks > (uint64_t)INT64_MAX / block_size) {
        fsd_mmap_close(patch_reader);
        fsd_source_reader_close(src_reader);
        return FSD_ERR_CORRUPT_DATA;
    }

    /* Validate stream lengths don't exceed patch file size */
    if (patch_size < FSD_HEADER_SIZE) {
        fsd_mmap_close(patch_reader);
        fsd_source_reader_close(src_reader);
        return FSD_ERR_TRUNCATED;
    }
    if (header.op_stream_len > patch_size - FSD_HEADER_SIZE) {
        fsd_mmap_close(patch_reader);
        fsd_source_reader_close(src_reader);
        return FSD_ERR_CORRUPT_DATA;
    }
    if (header.diff_stream_len > patch_size - FSD_HEADER_SIZE - header.op_stream_len) {
        fsd_mmap_close(patch_reader);
        fsd_source_reader_close(src_reader);
        return FSD_ERR_CORRUPT_DATA;
    }

    /* Locate streams */
    const uint8_t *op_stream = patch_data + FSD_HEADER_SIZE;
    const uint8_t *op_end = op_stream + header.op_stream_len;
    const uint8_t *diff_stream = op_end;
    const uint8_t *diff_end = diff_stream + header.diff_stream_len;
    const uint8_t *lit_stream = diff_end;
    const uint8_t *lit_end = patch_data + patch_size;

    /* Create output file */
    FILE *output = fopen(output_path, "wb");
    if (!output) {
        fsd_mmap_close(patch_reader);
        fsd_source_reader_close(src_reader);
        return FSD_ERR_IO;
    }

    /* Allocate output buffer (one block at a time) */
    uint8_t *block_buf = malloc(block_size);
    if (!block_buf) {
        fclose(output);
        fsd_mmap_close(patch_reader);
        fsd_source_reader_close(src_reader);
        return FSD_ERR_OUT_OF_MEMORY;
    }

    /* Process operations */
    const uint8_t *op_ptr = op_stream;
    const uint8_t *diff_ptr = diff_stream;
    const uint8_t *lit_ptr = lit_stream;
    uint64_t dest_block = 0;

    while (dest_block < header.dest_blocks && op_ptr < op_end) {
        uint8_t first_byte = *op_ptr++;
        uint8_t op_type = (first_byte >> 5) & 0x7;
        uint64_t count = 1;

        /* OP_COPY_ADD reads count/offset in different order, handle specially */
        if (op_type != FSD_OP_COPY_ADD) {
            err = read_count(&op_ptr, op_end, first_byte, &count);
            if (err != FSD_SUCCESS) goto error;
            /* Bound count so dest_block + count cannot exceed dest_blocks.
             * Guards against unbounded work and stops a malicious patch from
             * driving downstream arithmetic past the (already-validated)
             * dest_blocks ceiling. */
            if (count > header.dest_blocks - dest_block) {
                err = FSD_ERR_CORRUPT_DATA;
                goto error;
            }
        }

        switch (op_type) {
        case FSD_OP_COPY_IDENTITY:
            /* Copy from source at same position */
            for (uint64_t i = 0; i < count; i++) {
                uint64_t src_offset = (dest_block + i) * block_size;
                /* Bounds check source read */
                if (src_offset + block_size > src_size) {
                    err = FSD_ERR_CORRUPT_DATA;
                    goto error;
                }
                err = fsd_source_reader_read_at(src_reader, src_offset, block_size, block_buf);
                if (err != FSD_SUCCESS) goto error;
                if (fwrite(block_buf, 1, block_size, output) != block_size) {
                    err = FSD_ERR_IO;
                    goto error;
                }
            }
            break;

        case FSD_OP_COPY_RELOCATE: {
            /* Read offset */
            uint8_t offset_enc = (first_byte >> 1) & 0x3;
            uint8_t sign = first_byte & 0x1;
            int64_t offset = 0;

            switch (offset_enc) {
            case 0:
                if (op_ptr >= op_end) { err = FSD_ERR_TRUNCATED; goto error; }
                offset = *op_ptr++;
                break;
            case 1:
                if (op_ptr + 2 > op_end) { err = FSD_ERR_TRUNCATED; goto error; }
                offset = op_ptr[0] | (op_ptr[1] << 8);
                op_ptr += 2;
                break;
            case 2:
                if (op_ptr + 4 > op_end) { err = FSD_ERR_TRUNCATED; goto error; }
                offset = (uint32_t)op_ptr[0] | ((uint32_t)op_ptr[1] << 8) |
                        ((uint32_t)op_ptr[2] << 16) | ((uint32_t)op_ptr[3] << 24);
                op_ptr += 4;
                break;
            }

            if (sign) offset = -offset;

            /* Copy from source at offset position */
            for (uint64_t i = 0; i < count; i++) {
                int64_t src_block = (int64_t)(dest_block + i) + offset;
                if (src_block < 0) { err = FSD_ERR_CORRUPT_DATA; goto error; }
                uint64_t src_offset = (uint64_t)src_block * block_size;
                /* Bounds check source read */
                if (src_offset + block_size > src_size) {
                    err = FSD_ERR_CORRUPT_DATA;
                    goto error;
                }
                err = fsd_source_reader_read_at(src_reader, src_offset, block_size, block_buf);
                if (err != FSD_SUCCESS) goto error;
                if (fwrite(block_buf, 1, block_size, output) != block_size) {
                    err = FSD_ERR_IO;
                    goto error;
                }
            }
            break;
        }

        case FSD_OP_ZERO:
            /* Write zero blocks */
            memset(block_buf, 0, block_size);
            for (uint64_t i = 0; i < count; i++) {
                if (fwrite(block_buf, 1, block_size, output) != block_size) {
                    err = FSD_ERR_IO;
                    goto error;
                }
            }
            break;

        case FSD_OP_ONE:
            /* Write 0xFF blocks */
            memset(block_buf, 0xFF, block_size);
            for (uint64_t i = 0; i < count; i++) {
                if (fwrite(block_buf, 1, block_size, output) != block_size) {
                    err = FSD_ERR_IO;
                    goto error;
                }
            }
            break;

        case FSD_OP_COPY_ADD: {
            /* OP_COPY_ADD: [4:3][count_enc:2][offset_enc:2][diff_fmt:1]
             *              [byte_offset] [count] [diff_len:4]
             * - offset_enc: 0=2 bytes, 1=3 bytes, 2=4 bytes
             * - diff_fmt: 0=dense, 1=sparse
             */
            uint8_t count_enc = (first_byte >> 3) & 0x3;
            uint8_t offset_enc = (first_byte >> 1) & 0x3;
            uint8_t diff_fmt = first_byte & 0x1;

            /* COPY_ADD has no inline-count form: bits[2:0] are offset_enc + diff_fmt.
             * Reject rather than silently mis-decode the flag bits as a count. */
            if (count_enc == 3) {
                err = FSD_ERR_CORRUPT_DATA;
                goto error;
            }

            /* Read byte_offset (signed, two's complement). Each byte is
             * widened to uint32_t before shifting to keep the high-bit case
             * (op_ptr[N] & 0x80) inside unsigned arithmetic; shifting into
             * the sign of a signed int is UB. */
            int64_t byte_offset = 0;
            switch (offset_enc) {
            case 0: {  /* 2 bytes */
                if (op_ptr + 2 > op_end) { err = FSD_ERR_TRUNCATED; goto error; }
                uint32_t raw = (uint32_t)op_ptr[0] | ((uint32_t)op_ptr[1] << 8);
                byte_offset = (int16_t)raw;
                op_ptr += 2;
                break;
            }
            case 1: {  /* 3 bytes */
                if (op_ptr + 3 > op_end) { err = FSD_ERR_TRUNCATED; goto error; }
                uint32_t raw = (uint32_t)op_ptr[0] | ((uint32_t)op_ptr[1] << 8) |
                               ((uint32_t)op_ptr[2] << 16);
                byte_offset = (raw & 0x800000u) ? (int64_t)(raw | 0xFFFFFFFFFF000000LL)
                                                : (int64_t)raw;
                op_ptr += 3;
                break;
            }
            case 2: {  /* 4 bytes */
                if (op_ptr + 4 > op_end) { err = FSD_ERR_TRUNCATED; goto error; }
                uint32_t raw = (uint32_t)op_ptr[0] | ((uint32_t)op_ptr[1] << 8) |
                               ((uint32_t)op_ptr[2] << 16) | ((uint32_t)op_ptr[3] << 24);
                byte_offset = (int32_t)raw;
                op_ptr += 4;
                break;
            }
            }

            /* Read count (count_enc=3 already rejected above). */
            switch (count_enc) {
            case 0:
                if (op_ptr >= op_end) { err = FSD_ERR_TRUNCATED; goto error; }
                count = (uint64_t)(*op_ptr++) + 1;
                break;
            case 1:
                if (op_ptr + 2 > op_end) { err = FSD_ERR_TRUNCATED; goto error; }
                count = (uint64_t)(op_ptr[0] | (op_ptr[1] << 8)) + 1;
                op_ptr += 2;
                break;
            case 2:
                if (op_ptr + 4 > op_end) { err = FSD_ERR_TRUNCATED; goto error; }
                count = (uint32_t)op_ptr[0] | ((uint32_t)op_ptr[1] << 8) |
                        ((uint32_t)op_ptr[2] << 16) | ((uint32_t)op_ptr[3] << 24);
                op_ptr += 4;
                break;
            }

            /* Same count bound as the non-COPY_ADD path. */
            if (count > header.dest_blocks - dest_block) {
                err = FSD_ERR_CORRUPT_DATA;
                goto error;
            }

            /* Read diff_len */
            if (op_ptr + 4 > op_end) { err = FSD_ERR_TRUNCATED; goto error; }
            uint32_t diff_len = (uint32_t)op_ptr[0] | ((uint32_t)op_ptr[1] << 8) |
                               ((uint32_t)op_ptr[2] << 16) | ((uint32_t)op_ptr[3] << 24);
            op_ptr += 4;

            /* Check diff stream bounds */
            if (diff_ptr + diff_len > diff_end) {
                err = FSD_ERR_TRUNCATED;
                goto error;
            }

            /* diff_cursor: local pointer that advances through this operation's diff data
             * diff_ptr: global stream position, advanced to end of this operation's data */
            const uint8_t *diff_cursor = diff_ptr;
            const uint8_t *diff_region_end = diff_ptr + diff_len;
            diff_ptr = diff_region_end;

            /* Process each block */
            for (uint64_t blk = 0; blk < count; blk++) {
                /* Compute source byte position for this block */
                int64_t src_byte_pos = (int64_t)((dest_block + blk) * block_size) + byte_offset;
                if (src_byte_pos < 0) { err = FSD_ERR_CORRUPT_DATA; goto error; }
                /* Bounds check source read */
                if ((size_t)src_byte_pos + block_size > src_size) {
                    err = FSD_ERR_CORRUPT_DATA;
                    goto error;
                }

                /* Read source block into buffer */
                err = fsd_source_reader_read_at(src_reader, (uint64_t)src_byte_pos,
                                                block_size, block_buf);
                if (err != FSD_SUCCESS) goto error;

                if (diff_fmt == 0) {
                    /* Dense format: one diff byte per output byte. Compute
                     * the per-block offset in uint64_t — on 32-bit hosts a
                     * 32-bit size_t multiplication can wrap before the
                     * bounds check sees it. diff_len is uint32_t (≤4 GiB),
                     * so the post-bounds-check value always fits in size_t. */
                    uint64_t blk_offset = (uint64_t)blk * (uint64_t)block_size;
                    if (blk_offset > diff_len ||
                        block_size > (size_t)(diff_len - blk_offset)) {
                        err = FSD_ERR_TRUNCATED;
                        goto error;
                    }
                    for (size_t k = 0; k < block_size; k++) {
                        block_buf[k] += diff_cursor[(size_t)blk_offset + k];
                    }
                } else {
                    /* Sparse format: alternating copy-add/copy runs with base-128 lengths */
                    size_t out_pos = 0;
                    while (out_pos < block_size) {
                        /* Copy-add mode: read varint length */
                        size_t add_len;
                        err = read_varint(&diff_cursor, diff_region_end, &add_len);
                        if (err != FSD_SUCCESS) goto error;

                        /* Apply diff values - must consume all add_len bytes to maintain stream alignment */
                        if (diff_cursor + add_len > diff_region_end) {
                            err = FSD_ERR_TRUNCATED;
                            goto error;
                        }
                        for (size_t k = 0; k < add_len; k++) {
                            uint8_t diff_val = *diff_cursor++;
                            if (out_pos < block_size) {
                                block_buf[out_pos++] += diff_val;
                            }
                        }

                        if (out_pos >= block_size) break;

                        /* Copy mode: read varint length */
                        size_t copy_len;
                        err = read_varint(&diff_cursor, diff_region_end, &copy_len);
                        if (err != FSD_SUCCESS) goto error;

                        /* Advance output position */
                        out_pos += copy_len;
                        if (out_pos > block_size) {
                            /* Malformed: copy would exceed block boundary */
                            err = FSD_ERR_CORRUPT_DATA;
                            goto error;
                        }

                        /* Detect infinite loop: both lengths zero means no progress */
                        if (add_len == 0 && copy_len == 0 && out_pos < block_size) {
                            err = FSD_ERR_CORRUPT_DATA;
                            goto error;
                        }
                    }
                }

                /* Write result */
                if (fwrite(block_buf, 1, block_size, output) != block_size) {
                    err = FSD_ERR_IO;
                    goto error;
                }
            }
            break;
        }

        case FSD_OP_LITERAL:
            /* Write literal data */
            for (uint64_t i = 0; i < count; i++) {
                if (lit_ptr + block_size > lit_end) {
                    err = FSD_ERR_TRUNCATED;
                    goto error;
                }
                if (fwrite(lit_ptr, 1, block_size, output) != block_size) {
                    err = FSD_ERR_IO;
                    goto error;
                }
                lit_ptr += block_size;
            }
            break;

        default:
            err = FSD_ERR_BAD_OPERATION;
            goto error;
        }

        dest_block += count;

        /* Report progress */
        if (ctx->progress_cb) {
            ctx->progress_cb(ctx->progress_user_data, dest_block, header.dest_blocks);
        }
    }

    err = FSD_SUCCESS;

error:
    free(block_buf);
    fclose(output);
    fsd_mmap_close(patch_reader);
    fsd_source_reader_close(src_reader);

    if (err != FSD_SUCCESS) {
        fsd_unlink(output_path);
    }

    return err;
}

fsd_error_t fsd_patch_read_header(const char *patch_path,
                                  fsd_header_t *header_out) {
    if (!patch_path || !header_out) {
        return FSD_ERR_INVALID_ARG;
    }

    FILE *file = fopen(patch_path, "rb");
    if (!file) {
        return FSD_ERR_FILE_NOT_FOUND;
    }

    fsd_error_t err = fsd_header_read(file, header_out);
    fclose(file);
    return err;
}

fsd_error_t fsd_patch_output_size(const char *patch_path,
                                  uint64_t *size_out) {
    if (!patch_path || !size_out) {
        return FSD_ERR_INVALID_ARG;
    }

    fsd_header_t header;
    fsd_error_t err = fsd_patch_read_header(patch_path, &header);
    if (err != FSD_SUCCESS) {
        return err;
    }

    uint64_t block_size = 1ULL << header.block_size_log2;
    if (header.dest_blocks > UINT64_MAX / block_size) {
        return FSD_ERR_CORRUPT_DATA;
    }
    *size_out = header.dest_blocks * block_size;
    return FSD_SUCCESS;
}

void fsd_patch_set_progress(fsd_patch_ctx_t *ctx,
                            fsd_progress_fn callback,
                            void *user_data) {
    if (!ctx) return;
    ctx->progress_cb = callback;
    ctx->progress_user_data = user_data;
}

void fsd_patch_destroy(fsd_patch_ctx_t *ctx) {
    free(ctx);
}

/* Error message strings */
const char *fsd_strerror(fsd_error_t err) {
    switch (err) {
    case FSD_SUCCESS:             return "Success";
    case FSD_ERR_INVALID_ARG:     return "Invalid argument";
    case FSD_ERR_OUT_OF_MEMORY:   return "Out of memory";
    case FSD_ERR_IO:              return "I/O error";
    case FSD_ERR_CORRUPT_DATA:    return "Corrupt data";
    case FSD_ERR_BAD_MAGIC:       return "Invalid magic number";
    case FSD_ERR_BAD_VERSION:     return "Unsupported version";
    case FSD_ERR_BAD_BLOCK_SIZE:  return "Invalid block size";
    case FSD_ERR_SIZE_MISMATCH:   return "Size mismatch";
    case FSD_ERR_NOT_INITIALIZED: return "Not initialized";
    case FSD_ERR_CANCELLED:       return "Operation cancelled";
    case FSD_ERR_FILE_NOT_FOUND:  return "File not found";
    case FSD_ERR_PERMISSION:      return "Permission denied";
    case FSD_ERR_MMAP_FAILED:     return "Memory mapping failed";
    case FSD_ERR_TRUNCATED:       return "Unexpected end of file";
    case FSD_ERR_BAD_OPERATION:   return "Unknown operation";
    default:                      return "Unknown error";
    }
}

/* Library initialization.
 *
 * Three-state init: 0 = uninitialised, 1 = in progress, 2 = done.
 * Exactly one thread wins the CAS from 0 to 1, runs fsd_simd_init (which
 * mutates the global g_fsd_simd dispatch table), and publishes 2 with a
 * release store. Other threads either see 2 already and return, or wait
 * on an acquire load. This avoids torn reads of g_fsd_simd by readers
 * concurrent with a still-running fsd_simd_init. */
static FSD_ATOMIC int g_init_state = 0;

fsd_error_t fsd_init(void) {
#if !defined(__STDC_NO_ATOMICS__)
    if (atomic_load_explicit(&g_init_state, memory_order_acquire) == 2) {
        return FSD_SUCCESS;
    }

    int expected = 0;
    if (atomic_compare_exchange_strong_explicit(
            &g_init_state, &expected, 1,
            memory_order_acquire, memory_order_relaxed)) {
        fsd_simd_init();
        atomic_store_explicit(&g_init_state, 2, memory_order_release);
    } else {
        /* Another thread is initialising — wait for it to publish state 2. */
        while (atomic_load_explicit(&g_init_state, memory_order_acquire) != 2) {
            /* spin */
        }
    }
#else
    /* No C11 atomics: fall back to the previous best-effort behaviour.
     * Concurrent callers can race; documented as a constraint of this
     * compiler/platform combination. */
    if (g_init_state != 2) {
        fsd_simd_init();
        g_init_state = 2;
    }
#endif
    return FSD_SUCCESS;
}

void fsd_cleanup(void) {
    fsd_atomic_store(g_init_state, 0);
}

const char *fsd_version(void) {
    return "1.0.0";
}

const char *fsd_build_info(void) {
#if defined(__clang__)
    return "Clang " __clang_version__;
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#elif defined(__INTEL_COMPILER)
    return "ICC";
#else
    return "Unknown compiler";
#endif
}
