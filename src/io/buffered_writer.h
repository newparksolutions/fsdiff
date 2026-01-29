/**
 * @file buffered_writer.h
 * @brief Buffered file writer for efficient output
 */

#ifndef FSDIFF_BUFFERED_WRITER_H
#define FSDIFF_BUFFERED_WRITER_H

#include <fsdiff/types.h>
#include <fsdiff/error.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Default buffer size (64 KiB) */
#define FSD_WRITER_DEFAULT_BUFFER  (64 * 1024)

/** Buffered writer handle */
typedef struct fsd_buffered_writer {
    FILE *file;              /**< Output file */
    uint8_t *buffer;         /**< Write buffer */
    size_t buffer_size;      /**< Buffer capacity */
    size_t buffer_pos;       /**< Current position in buffer */
    size_t total_written;    /**< Total bytes written to file */
    bool owns_file;          /**< Whether we should close the file */
} fsd_buffered_writer_t;

/**
 * Create a buffered writer for a file path.
 *
 * @param writer_out   Output pointer for writer
 * @param path         Path to output file
 * @param buffer_size  Buffer size (0 for default)
 * @return             FSD_SUCCESS or error code
 */
fsd_error_t fsd_writer_create(fsd_buffered_writer_t **writer_out,
                              const char *path,
                              size_t buffer_size);

/**
 * Create a buffered writer for an existing FILE handle.
 *
 * @param writer_out   Output pointer for writer
 * @param file         FILE handle (not closed by writer)
 * @param buffer_size  Buffer size (0 for default)
 * @return             FSD_SUCCESS or error code
 */
fsd_error_t fsd_writer_create_from_file(fsd_buffered_writer_t **writer_out,
                                        FILE *file,
                                        size_t buffer_size);

/**
 * Write data to the buffer.
 *
 * @param writer  Writer handle
 * @param data    Data to write
 * @param len     Length in bytes
 * @return        FSD_SUCCESS or error code
 */
fsd_error_t fsd_writer_write(fsd_buffered_writer_t *writer,
                             const void *data,
                             size_t len);

/**
 * Write a single byte.
 *
 * @param writer  Writer handle
 * @param byte    Byte to write
 * @return        FSD_SUCCESS or error code
 */
fsd_error_t fsd_writer_write_byte(fsd_buffered_writer_t *writer, uint8_t byte);

/**
 * Write a little-endian uint16.
 *
 * @param writer  Writer handle
 * @param value   Value to write
 * @return        FSD_SUCCESS or error code
 */
fsd_error_t fsd_writer_write_u16_le(fsd_buffered_writer_t *writer, uint16_t value);

/**
 * Write a little-endian uint32.
 *
 * @param writer  Writer handle
 * @param value   Value to write
 * @return        FSD_SUCCESS or error code
 */
fsd_error_t fsd_writer_write_u32_le(fsd_buffered_writer_t *writer, uint32_t value);

/**
 * Write a little-endian uint64.
 *
 * @param writer  Writer handle
 * @param value   Value to write
 * @return        FSD_SUCCESS or error code
 */
fsd_error_t fsd_writer_write_u64_le(fsd_buffered_writer_t *writer, uint64_t value);

/**
 * Flush the buffer to disk.
 *
 * @param writer  Writer handle
 * @return        FSD_SUCCESS or error code
 */
fsd_error_t fsd_writer_flush(fsd_buffered_writer_t *writer);

/**
 * Get total bytes written.
 *
 * @param writer  Writer handle
 * @return        Total bytes written
 */
size_t fsd_writer_bytes_written(const fsd_buffered_writer_t *writer);

/**
 * Close the writer and flush any remaining data.
 *
 * @param writer  Writer handle (NULL is safe)
 * @return        FSD_SUCCESS or error code from final flush
 */
fsd_error_t fsd_writer_close(fsd_buffered_writer_t *writer);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_BUFFERED_WRITER_H */
