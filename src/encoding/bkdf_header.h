/**
 * @file bkdf_header.h
 * @brief BKDF file header read/write operations
 */

#ifndef FSDIFF_BKDF_HEADER_H
#define FSDIFF_BKDF_HEADER_H

#include <fsdiff/types.h>
#include <fsdiff/error.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Write BKDF header to file.
 *
 * @param file           Output file
 * @param dest_blocks    Number of destination blocks
 * @param block_size_log2  Block size as log2
 * @param op_stream_len  Length of operation stream
 * @param diff_stream_len  Length of diff stream
 * @return               FSD_SUCCESS or error code
 */
fsd_error_t fsd_header_write(FILE *file,
                             uint64_t dest_blocks,
                             uint8_t block_size_log2,
                             uint64_t op_stream_len,
                             uint64_t diff_stream_len);

/**
 * Read and validate BKDF header from file.
 *
 * @param file        Input file
 * @param header_out  Output structure for header
 * @return            FSD_SUCCESS or error code
 */
fsd_error_t fsd_header_read(FILE *file, fsd_header_t *header_out);

/**
 * Read and validate BKDF header from memory.
 *
 * @param data        Input data (at least 32 bytes)
 * @param len         Length of data
 * @param header_out  Output structure for header
 * @return            FSD_SUCCESS or error code
 */
fsd_error_t fsd_header_read_memory(const void *data,
                                   size_t len,
                                   fsd_header_t *header_out);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_BKDF_HEADER_H */
