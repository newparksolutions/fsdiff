/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file source_reader.h
 * @brief Source-image reader with mmap and O_DIRECT backends.
 *
 * fsdiff is designed to apply A/B updates to read-only-mounted block
 * devices (e.g. /dev/mmcblk0p5). When the source argument is the bdev
 * underlying a mounted FS, the kernel's bdev page cache is shared with
 * the FS driver: the driver dirties in-memory metadata buffers (journal
 * replay state, group-descriptor caches, bitmap caches) without writing
 * them back on a read-only mount. Any non-O_DIRECT read of that bdev
 * therefore returns the modified-in-memory bytes — not what is actually
 * on disk. O_DIRECT goes via blkdev_direct_IO and bypasses the bdev page
 * cache entirely, returning on-disk bytes.
 *
 * This abstraction picks the right backend automatically:
 *   - regular files  -> mmap backend (fast random access)
 *   - block devices  -> O_DIRECT pread (correct on mounted partitions)
 *
 * Callers can override via the mode argument.
 */

#ifndef FSDIFF_SOURCE_READER_H
#define FSDIFF_SOURCE_READER_H

#include <fsdiff/types.h>
#include <fsdiff/error.h>
#include <fsdiff/options.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fsd_source_reader fsd_source_reader_t;

/**
 * Open a source for reading.
 *
 * For FSD_SOURCE_AUTO, fstat() picks the backend:
 *   S_ISBLK -> direct backend (O_DIRECT pread)
 *   else    -> mmap backend
 *
 * Forcing FSD_SOURCE_DIRECT against a filesystem that does not support
 * O_DIRECT (tmpfs, FUSE, etc.) returns FSD_ERR_IO; the function never
 * silently falls back to buffered reads.
 *
 * @param reader_out  Output pointer for reader handle
 * @param path        Source path (regular file or block device)
 * @param mode        Backend selection
 * @return            FSD_SUCCESS or error code
 */
fsd_error_t fsd_source_reader_open(fsd_source_reader_t **reader_out,
                                   const char *path,
                                   fsd_source_mode_t mode);

/**
 * Read len bytes from offset into dst. Bypasses the bdev page cache when
 * the direct backend is in use. dst does not need to be aligned.
 *
 * It is an error to read past the end of the source (FSD_ERR_TRUNCATED).
 *
 * @param reader  Reader handle
 * @param offset  Byte offset in source
 * @param len     Number of bytes to read
 * @param dst     Destination buffer (caller-owned, any alignment)
 * @return        FSD_SUCCESS or error code
 */
fsd_error_t fsd_source_reader_read_at(fsd_source_reader_t *reader,
                                      uint64_t offset,
                                      size_t len,
                                      void *dst);

/**
 * Get a pointer to the entire source as a contiguous buffer.
 *
 * For the mmap backend this is the mmap base. For the direct backend
 * the entire source is slurped into an aligned buffer on first call —
 * subsequent calls return the cached pointer. Cost is O(file_size) in
 * memory; intended for diff mode where the matching stages need random
 * access without coordinating offsets.
 *
 * @param reader    Reader handle
 * @param data_out  Output pointer to base of source data
 * @return          FSD_SUCCESS or error code
 */
fsd_error_t fsd_source_reader_data(fsd_source_reader_t *reader,
                                   const void **data_out);

/**
 * Get the total size of the source in bytes.
 */
size_t fsd_source_reader_size(const fsd_source_reader_t *reader);

/**
 * Close and free the reader. NULL is safe.
 */
void fsd_source_reader_close(fsd_source_reader_t *reader);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_SOURCE_READER_H */
