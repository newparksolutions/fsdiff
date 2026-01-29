/**
 * @file mmap_reader.h
 * @brief Memory-mapped file reader for large files
 */

#ifndef FSDIFF_MMAP_READER_H
#define FSDIFF_MMAP_READER_H

#include <fsdiff/types.h>
#include <fsdiff/error.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Memory-mapped file reader handle */
typedef struct fsd_mmap_reader {
    void *base_addr;       /**< Base address of mapping */
    size_t file_size;      /**< Size of file in bytes */
    int fd;                /**< File descriptor (Unix) */
#ifdef _WIN32
    void *file_handle;     /**< Windows file handle */
    void *mapping_handle;  /**< Windows mapping handle */
#endif
} fsd_mmap_reader_t;

/**
 * Open a file and map it into memory.
 *
 * @param reader_out  Output pointer for reader handle
 * @param path        Path to file
 * @return            FSD_SUCCESS or error code
 */
fsd_error_t fsd_mmap_open(fsd_mmap_reader_t **reader_out, const char *path);

/**
 * Get pointer to mapped data.
 *
 * @param reader  Reader handle
 * @return        Pointer to mapped data
 */
const void *fsd_mmap_data(const fsd_mmap_reader_t *reader);

/**
 * Get size of mapped file.
 *
 * @param reader  Reader handle
 * @return        File size in bytes
 */
size_t fsd_mmap_size(const fsd_mmap_reader_t *reader);

/**
 * Close the mapping and release resources.
 *
 * @param reader  Reader handle (NULL is safe)
 */
void fsd_mmap_close(fsd_mmap_reader_t *reader);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_MMAP_READER_H */
