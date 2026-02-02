/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file operation_encoder.h
 * @brief Encode block matching results to BKDF operation stream
 */

#ifndef FSDIFF_OPERATION_ENCODER_H
#define FSDIFF_OPERATION_ENCODER_H

#include <fsdiff/types.h>
#include <fsdiff/error.h>
#include "../core/block_tracker.h"
#include "../io/buffered_writer.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Operation encoder handle */
typedef struct fsd_op_encoder fsd_op_encoder_t;

/**
 * Create operation encoder.
 *
 * @param encoder_out  Output pointer for encoder
 * @param block_size   Block size in bytes
 * @return             FSD_SUCCESS or error code
 */
fsd_error_t fsd_op_encoder_create(fsd_op_encoder_t **encoder_out,
                                  size_t block_size);

/**
 * Encode all operations from block tracker.
 *
 * Iterates through blocks in order, coalescing consecutive operations
 * of the same type where possible.
 *
 * @param encoder       Encoder handle
 * @param tracker       Block tracker with match results
 * @param op_writer     Writer for operation stream
 * @param diff_writer   Writer for diff stream
 * @param lit_writer    Writer for literal stream
 * @param dest_data     Destination image data (for literals)
 * @return              FSD_SUCCESS or error code
 */
fsd_error_t fsd_op_encoder_encode(fsd_op_encoder_t *encoder,
                                  const fsd_block_tracker_t *tracker,
                                  fsd_buffered_writer_t *op_writer,
                                  fsd_buffered_writer_t *diff_writer,
                                  fsd_buffered_writer_t *lit_writer,
                                  const uint8_t *dest_data);

/**
 * Get size of operation stream.
 *
 * @param encoder  Encoder handle
 * @return         Bytes written to operation stream
 */
size_t fsd_op_encoder_op_size(const fsd_op_encoder_t *encoder);

/**
 * Get size of diff stream.
 *
 * @param encoder  Encoder handle
 * @return         Bytes written to diff stream
 */
size_t fsd_op_encoder_diff_size(const fsd_op_encoder_t *encoder);

/**
 * Get size of literal stream.
 *
 * @param encoder  Encoder handle
 * @return         Bytes written to literal stream
 */
size_t fsd_op_encoder_literal_size(const fsd_op_encoder_t *encoder);

/**
 * Destroy encoder.
 *
 * @param encoder  Encoder handle (NULL is safe)
 */
void fsd_op_encoder_destroy(fsd_op_encoder_t *encoder);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_OPERATION_ENCODER_H */
