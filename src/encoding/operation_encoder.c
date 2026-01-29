/**
 * @file operation_encoder.c
 * @brief Operation encoder implementation
 */

#include "operation_encoder.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>  /* For SIZE_MAX */

/**
 * Encode size_t as base-128 varint (little-endian).
 * Each byte uses bit 7 as continuation flag (1=more bytes, 0=last).
 * Bits 6-0 contain 7 bits of data.
 *
 * @param buf    Output buffer (must have room for up to 10 bytes)
 * @param value  Value to encode
 * @return       Number of bytes written (1-10)
 */
static size_t encode_varint(uint8_t *buf, size_t value) {
    size_t len = 0;
    while (value >= 128) {
        buf[len++] = (uint8_t)((value & 0x7F) | 0x80);
        value >>= 7;
    }
    buf[len++] = (uint8_t)(value & 0x7F);
    return len;
}

struct fsd_op_encoder {
    size_t block_size;
    size_t op_bytes;
    size_t diff_bytes;
    size_t literal_bytes;
};

/* Encode count using count encoding scheme */
static fsd_error_t encode_count(fsd_buffered_writer_t *writer,
                                uint8_t op_byte,
                                uint64_t count) {
    fsd_error_t err;

    if (count <= 8) {
        /* Inline: bits[4:3] = 3, bits[2:0] = count - 1 */
        uint8_t byte = op_byte | (3 << 3) | ((count - 1) & 0x7);
        return fsd_writer_write_byte(writer, byte);
    } else if (count <= 256) {
        /* 1 byte: bits[4:3] = 0, followed by (count - 1) */
        uint8_t byte = op_byte | (0 << 3);
        err = fsd_writer_write_byte(writer, byte);
        if (err != FSD_SUCCESS) return err;
        return fsd_writer_write_byte(writer, (uint8_t)(count - 1));
    } else if (count <= 65536) {
        /* 2 bytes: bits[4:3] = 1, followed by (count - 1) LE */
        uint8_t byte = op_byte | (1 << 3);
        err = fsd_writer_write_byte(writer, byte);
        if (err != FSD_SUCCESS) return err;
        return fsd_writer_write_u16_le(writer, (uint16_t)(count - 1));
    } else {
        /* 4 bytes: bits[4:3] = 2, followed by count LE */
        uint8_t byte = op_byte | (2 << 3);
        err = fsd_writer_write_byte(writer, byte);
        if (err != FSD_SUCCESS) return err;
        return fsd_writer_write_u32_le(writer, (uint32_t)count);
    }
}

fsd_error_t fsd_op_encoder_create(fsd_op_encoder_t **encoder_out,
                                  size_t block_size) {
    if (!encoder_out || block_size == 0) {
        return FSD_ERR_INVALID_ARG;
    }

    fsd_op_encoder_t *encoder = calloc(1, sizeof(fsd_op_encoder_t));
    if (!encoder) {
        return FSD_ERR_OUT_OF_MEMORY;
    }

    encoder->block_size = block_size;
    encoder->op_bytes = 0;
    encoder->diff_bytes = 0;
    encoder->literal_bytes = 0;

    *encoder_out = encoder;
    return FSD_SUCCESS;
}

fsd_error_t fsd_op_encoder_encode(fsd_op_encoder_t *encoder,
                                  const fsd_block_tracker_t *tracker,
                                  fsd_buffered_writer_t *op_writer,
                                  fsd_buffered_writer_t *diff_writer,
                                  fsd_buffered_writer_t *lit_writer,
                                  const uint8_t *dest_data) {
    if (!encoder || !tracker || !op_writer || !diff_writer ||
        !lit_writer || !dest_data) {
        return FSD_ERR_INVALID_ARG;
    }

    size_t block_size = encoder->block_size;
    fsd_error_t err;

    uint64_t i = 0;
    while (i < tracker->count) {
        const fsd_block_state_t *state = fsd_block_tracker_get(tracker, i);

        /* Count consecutive blocks of same type */
        uint64_t run_count = 1;
        fsd_match_type_t match_type = state->match_type;

        /* For relocate, also check if consecutive source blocks */
        if (match_type == FSD_MATCH_RELOCATE) {
            int64_t base_offset = (int64_t)state->src_index - (int64_t)i;

            while (i + run_count < tracker->count) {
                const fsd_block_state_t *next = fsd_block_tracker_get(tracker, i + run_count);
                if (next->match_type != FSD_MATCH_RELOCATE) break;

                int64_t next_offset = (int64_t)next->src_index - (int64_t)(i + run_count);
                if (next_offset != base_offset) break;

                run_count++;
            }
        } else if (match_type == FSD_MATCH_IDENTITY ||
                   match_type == FSD_MATCH_ZERO ||
                   match_type == FSD_MATCH_ONE ||
                   match_type == FSD_MATCH_NONE) {
            /* Can coalesce consecutive blocks of same type */
            while (i + run_count < tracker->count) {
                const fsd_block_state_t *next = fsd_block_tracker_get(tracker, i + run_count);
                if (next->match_type != match_type) break;
                run_count++;
            }
        }

        /* Encode the operation */
        switch (match_type) {
        case FSD_MATCH_IDENTITY: {
            /* OP_COPY_IDENTITY (0): [0:3][count_enc:2][000:3] [count] */
            uint8_t op_byte = (FSD_OP_COPY_IDENTITY << 5);
            err = encode_count(op_writer, op_byte, run_count);
            if (err != FSD_SUCCESS) return err;
            break;
        }

        case FSD_MATCH_RELOCATE: {
            /* OP_COPY_RELOCATE (1): [op:3][count_enc:2][offset_enc:2][sign:1] [count] [offset]
             * Note: For COPY_RELOCATE, we don't use inline count (count_enc=3)
             * because bits 2:0 are used for offset_enc and sign */
            int64_t offset = (int64_t)state->src_index - (int64_t)i;

            /* Determine offset encoding */
            uint64_t abs_offset = (offset < 0) ? (uint64_t)(-offset) : (uint64_t)offset;
            uint8_t sign_bit = (offset < 0) ? 1 : 0;
            uint8_t offset_enc;
            if (abs_offset <= 0xFF) {
                offset_enc = 0;  /* 1 byte */
            } else if (abs_offset <= 0xFFFF) {
                offset_enc = 1;  /* 2 bytes */
            } else {
                offset_enc = 2;  /* 4 bytes */
            }

            /* Determine count encoding - never use inline (3) for COPY_RELOCATE */
            uint8_t count_enc;
            if (run_count <= 256) {
                count_enc = 0;  /* 1 byte */
            } else if (run_count <= 65536) {
                count_enc = 1;  /* 2 bytes */
            } else {
                count_enc = 2;  /* 4 bytes */
            }

            /* Build and write first byte:
             * [op_type:3][count_enc:2][offset_enc:2][sign:1] */
            uint8_t first_byte = (FSD_OP_COPY_RELOCATE << 5) |
                                 (count_enc << 3) |
                                 (offset_enc << 1) |
                                 sign_bit;
            err = fsd_writer_write_byte(op_writer, first_byte);
            if (err != FSD_SUCCESS) return err;

            /* Write count */
            if (count_enc == 0) {
                err = fsd_writer_write_byte(op_writer, (uint8_t)(run_count - 1));
            } else if (count_enc == 1) {
                err = fsd_writer_write_u16_le(op_writer, (uint16_t)(run_count - 1));
            } else {
                err = fsd_writer_write_u32_le(op_writer, (uint32_t)run_count);
            }
            if (err != FSD_SUCCESS) return err;

            /* Write offset */
            if (offset_enc == 0) {
                err = fsd_writer_write_byte(op_writer, (uint8_t)abs_offset);
            } else if (offset_enc == 1) {
                err = fsd_writer_write_u16_le(op_writer, (uint16_t)abs_offset);
            } else {
                err = fsd_writer_write_u32_le(op_writer, (uint32_t)abs_offset);
            }
            if (err != FSD_SUCCESS) return err;

            break;
        }

        case FSD_MATCH_ZERO: {
            /* OP_ZERO (2): [2:3][count_enc:2][000:3] [count] */
            uint8_t op_byte = (FSD_OP_ZERO << 5);
            err = encode_count(op_writer, op_byte, run_count);
            if (err != FSD_SUCCESS) return err;
            break;
        }

        case FSD_MATCH_ONE: {
            /* OP_ONE (3): [3:3][count_enc:2][000:3] [count] */
            uint8_t op_byte = (FSD_OP_ONE << 5);
            err = encode_count(op_writer, op_byte, run_count);
            if (err != FSD_SUCCESS) return err;
            break;
        }

        case FSD_MATCH_PARTIAL: {
            /* OP_COPY_ADD (4): Per spec:
             * [4:3][count_enc:2][offset_enc:2][diff_fmt:1] [byte_offset] [count] [diff_len:4]
             *
             * - offset_enc: Byte offset size (0=2, 1=3, 2=4 bytes)
             * - diff_fmt: 0=dense, 1=sparse
             * - byte_offset: Signed byte offset (two's complement)
             * - count: Number of blocks
             * - diff_len: Bytes to consume from diff stream
             */

            /* Compute byte offset for first block:
             * byte_offset = src_byte_pos - dest_byte_pos
             *             = (src_index * block_size + byte_offset_in_block) - (dest_index * block_size)
             */
            int64_t byte_offset = (int64_t)(state->src_index * block_size + state->byte_offset)
                                - (int64_t)(i * block_size);

            /* Try to coalesce consecutive partial matches with same relative byte offset */
            run_count = 1;
            while (i + run_count < tracker->count) {
                const fsd_block_state_t *next = fsd_block_tracker_get(tracker, i + run_count);
                if (next->match_type != FSD_MATCH_PARTIAL) break;

                int64_t next_byte_offset = (int64_t)(next->src_index * block_size + next->byte_offset)
                                         - (int64_t)((i + run_count) * block_size);
                if (next_byte_offset != byte_offset) break;

                run_count++;
            }

            /* Count non-zero diff bytes across all blocks to decide format */
            size_t nonzero_count = 0;
            for (uint64_t j = 0; j < run_count; j++) {
                const fsd_block_state_t *pstate = fsd_block_tracker_get(tracker, i + j);
                if (pstate->delta) {
                    for (size_t k = 0; k < block_size && k < pstate->delta_size; k++) {
                        if (pstate->delta[k] != 0) nonzero_count++;
                    }
                }
            }

            /* Use sparse if fewer than 512 non-zero bytes per block on average */
            uint8_t diff_fmt = (nonzero_count < run_count * 512) ? 1 : 0;

            /* Pre-calculate diff data to a temporary buffer so we know diff_len */
            /* Check for overflow in size calculation */
            if (run_count > SIZE_MAX / block_size / 2) {
                return FSD_ERR_OUT_OF_MEMORY;
            }
            size_t max_diff_len = run_count * block_size * 2;  /* Worst case for sparse */
            uint8_t *diff_buf = malloc(max_diff_len);
            if (!diff_buf) return FSD_ERR_OUT_OF_MEMORY;

            size_t diff_len = 0;

            if (diff_fmt == 0) {
                /* Dense format: one diff byte per output byte */
                for (uint64_t j = 0; j < run_count; j++) {
                    const fsd_block_state_t *pstate = fsd_block_tracker_get(tracker, i + j);
                    if (pstate->delta && pstate->delta_size >= block_size) {
                        memcpy(diff_buf + diff_len, pstate->delta, block_size);
                    } else if (pstate->delta) {
                        memcpy(diff_buf + diff_len, pstate->delta, pstate->delta_size);
                        memset(diff_buf + diff_len + pstate->delta_size, 0,
                               block_size - pstate->delta_size);
                    } else {
                        memset(diff_buf + diff_len, 0, block_size);
                    }
                    diff_len += block_size;
                }
            } else {
                /* Sparse format: alternating copy-add/copy runs per block
                 * Format: [add_len:varint][add_data...][copy_len:varint][add_len:varint]...
                 * Starts in copy-add mode, alternates until block_size bytes emitted
                 * Uses base-128 encoding for lengths (no 255-byte limit)
                 */
                for (uint64_t j = 0; j < run_count; j++) {
                    const fsd_block_state_t *pstate = fsd_block_tracker_get(tracker, i + j);
                    const uint8_t *delta = pstate->delta;
                    size_t pos = 0;

                    while (pos < block_size) {
                        /* Copy-add mode: include bytes in run, terminating only when
                         * we see two consecutive zeros (or reach end) */
                        size_t add_start = pos;
                        size_t add_len = 0;

                        while (pos < block_size) {
                            bool curr_is_zero = (!delta || delta[pos] == 0);
                            bool next_is_zero = (pos + 1 >= block_size || !delta || delta[pos + 1] == 0);

                            /* Terminate run if we see two consecutive zeros */
                            if (curr_is_zero && next_is_zero) {
                                break;
                            }

                            /* Include this byte in the run */
                            add_len++;
                            pos++;
                        }

                        /* Write copy-add: [len:varint][data...] */
                        uint8_t varint_buf[10];
                        size_t varint_len = encode_varint(varint_buf, add_len);
                        memcpy(diff_buf + diff_len, varint_buf, varint_len);
                        diff_len += varint_len;

                        if (add_len > 0 && delta) {
                            memcpy(diff_buf + diff_len, delta + add_start, add_len);
                            diff_len += add_len;
                        }

                        if (pos >= block_size) break;

                        /* Copy mode: count contiguous zero diffs at pos (no limit) */
                        size_t copy_len = 0;
                        while (pos < block_size) {
                            if (!delta || delta[pos] == 0) {
                                copy_len++;
                                pos++;
                            } else {
                                break;
                            }
                        }

                        /* Write copy: [len:varint] */
                        varint_len = encode_varint(varint_buf, copy_len);
                        memcpy(diff_buf + diff_len, varint_buf, varint_len);
                        diff_len += varint_len;
                    }
                }
            }

            /* Determine byte offset encoding (signed, two's complement)
             * offset_enc: 0=2 bytes, 1=3 bytes, 2=4 bytes */
            int64_t abs_offset = (byte_offset < 0) ? -byte_offset : byte_offset;
            uint8_t offset_enc;
            if (abs_offset <= 0x7FFF) {
                offset_enc = 0;  /* 2 bytes */
            } else if (abs_offset <= 0x7FFFFF) {
                offset_enc = 1;  /* 3 bytes */
            } else {
                offset_enc = 2;  /* 4 bytes */
            }

            /* Determine count encoding - don't use inline (3) for COPY_ADD */
            uint8_t count_enc;
            if (run_count <= 256) {
                count_enc = 0;
            } else if (run_count <= 65536) {
                count_enc = 1;
            } else {
                count_enc = 2;
            }

            /* Build first byte:
             * [op_type:3][count_enc:2][offset_enc:2][diff_fmt:1] */
            uint8_t first_byte = (FSD_OP_COPY_ADD << 5) |
                                 (count_enc << 3) |
                                 (offset_enc << 1) |
                                 diff_fmt;
            err = fsd_writer_write_byte(op_writer, first_byte);
            if (err != FSD_SUCCESS) { free(diff_buf); return err; }

            /* Write byte_offset (signed, two's complement) */
            if (offset_enc == 0) {
                err = fsd_writer_write_u16_le(op_writer, (uint16_t)(int16_t)byte_offset);
            } else if (offset_enc == 1) {
                uint32_t val = (uint32_t)(int32_t)byte_offset;
                err = fsd_writer_write_byte(op_writer, val & 0xFF);
                if (err != FSD_SUCCESS) { free(diff_buf); return err; }
                err = fsd_writer_write_byte(op_writer, (val >> 8) & 0xFF);
                if (err != FSD_SUCCESS) { free(diff_buf); return err; }
                err = fsd_writer_write_byte(op_writer, (val >> 16) & 0xFF);
            } else {
                err = fsd_writer_write_u32_le(op_writer, (uint32_t)(int32_t)byte_offset);
            }
            if (err != FSD_SUCCESS) { free(diff_buf); return err; }

            /* Write count */
            if (count_enc == 0) {
                err = fsd_writer_write_byte(op_writer, (uint8_t)(run_count - 1));
            } else if (count_enc == 1) {
                err = fsd_writer_write_u16_le(op_writer, (uint16_t)(run_count - 1));
            } else {
                err = fsd_writer_write_u32_le(op_writer, (uint32_t)run_count);
            }
            if (err != FSD_SUCCESS) { free(diff_buf); return err; }

            /* Write diff_len */
            err = fsd_writer_write_u32_le(op_writer, (uint32_t)diff_len);
            if (err != FSD_SUCCESS) { free(diff_buf); return err; }

            /* Write diff data */
            err = fsd_writer_write(diff_writer, diff_buf, diff_len);
            free(diff_buf);
            if (err != FSD_SUCCESS) return err;

            break;
        }

        case FSD_MATCH_NONE:
        default: {
            /* OP_LITERAL (5): [5:3][count_enc:2][000:3] [count] */
            uint8_t op_byte = (FSD_OP_LITERAL << 5);
            err = encode_count(op_writer, op_byte, run_count);
            if (err != FSD_SUCCESS) return err;

            /* Write literal data */
            const uint8_t *lit_data = dest_data + (i * block_size);
            err = fsd_writer_write(lit_writer, lit_data, run_count * block_size);
            if (err != FSD_SUCCESS) return err;
            break;
        }
        }

        i += run_count;
    }

    /* Update sizes */
    encoder->op_bytes = fsd_writer_bytes_written(op_writer);
    encoder->diff_bytes = fsd_writer_bytes_written(diff_writer);
    encoder->literal_bytes = fsd_writer_bytes_written(lit_writer);

    return FSD_SUCCESS;
}

size_t fsd_op_encoder_op_size(const fsd_op_encoder_t *encoder) {
    return encoder ? encoder->op_bytes : 0;
}

size_t fsd_op_encoder_diff_size(const fsd_op_encoder_t *encoder) {
    return encoder ? encoder->diff_bytes : 0;
}

size_t fsd_op_encoder_literal_size(const fsd_op_encoder_t *encoder) {
    return encoder ? encoder->literal_bytes : 0;
}

void fsd_op_encoder_destroy(fsd_op_encoder_t *encoder) {
    free(encoder);
}
