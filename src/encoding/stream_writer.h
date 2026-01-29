/**
 * @file stream_writer.h
 * @brief Dense/sparse diff stream encoding
 */

#ifndef FSDIFF_STREAM_WRITER_H
#define FSDIFF_STREAM_WRITER_H

#include <fsdiff/types.h>
#include <fsdiff/error.h>
#include "../io/buffered_writer.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Write dense diff data.
 *
 * One diff byte per output byte: output[i] = source[i] + diff[i] (mod 256)
 *
 * @param writer     Diff stream writer
 * @param source     Source block data
 * @param dest       Destination block data
 * @param len        Length in bytes
 * @return           FSD_SUCCESS or error code
 */
fsd_error_t fsd_stream_write_dense(fsd_buffered_writer_t *writer,
                                   const uint8_t *source,
                                   const uint8_t *dest,
                                   size_t len);

/**
 * Write sparse diff data.
 *
 * Alternating copy/copy-add format for blocks with localized changes.
 *
 * @param writer     Diff stream writer
 * @param source     Source block data
 * @param dest       Destination block data
 * @param len        Block size
 * @param bytes_out  Output: bytes written to stream
 * @return           FSD_SUCCESS or error code
 */
fsd_error_t fsd_stream_write_sparse(fsd_buffered_writer_t *writer,
                                    const uint8_t *source,
                                    const uint8_t *dest,
                                    size_t len,
                                    size_t *bytes_out);

/**
 * Determine which format is more compact.
 *
 * @param source     Source block data
 * @param dest       Destination block data
 * @param len        Block size
 * @return           FSD_DIFF_DENSE or FSD_DIFF_SPARSE
 */
fsd_diff_format_t fsd_stream_choose_format(const uint8_t *source,
                                           const uint8_t *dest,
                                           size_t len);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_STREAM_WRITER_H */
