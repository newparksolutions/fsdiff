/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file ext_reader.h
 * @brief Minimal read-only ext2/3/4 walker over an in-memory image.
 *
 * Purpose-built for the filesystem-aware matching stage: enumerates every
 * regular-file directory entry (one callback per dirent, so hard links are
 * reported under every path) together with the file's logical-to-physical
 * extent list. Nothing is written; nothing outside the image buffer is read.
 *
 * The on-disk format is decoded from first principles (offset constants plus
 * little-endian load helpers — no packed structs, no kernel or e2fsprogs
 * headers). Only a conservative whitelist of INCOMPAT features is accepted;
 * anything unknown fails the probe cleanly and the caller skips
 * filesystem-aware matching for that image. All RO_COMPAT features are
 * acceptable because the reader never validates or updates metadata
 * (checksums are ignored, not verified).
 */

#ifndef FSDIFF_EXT_READER_H
#define FSDIFF_EXT_READER_H

#include <fsdiff/error.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle for an opened filesystem */
typedef struct fsd_extfs fsd_extfs_t;

/** One contiguous file mapping: `count` filesystem blocks of file data
 *  starting at file-logical block `logical`, stored at filesystem block
 *  `physical`. Unwritten (fallocated) extents and holes are never reported. */
typedef struct {
    uint64_t logical;
    uint64_t physical;
    uint64_t count;
} fsd_ext_extent_t;

/**
 * Callback invoked once per regular-file directory entry.
 *
 * @param user       Caller context
 * @param path       Full path from the filesystem root (e.g. "/usr/bin/env");
 *                   valid only for the duration of the call
 * @param inode_no   Inode number
 * @param file_size  File size in bytes
 * @param extents    File mappings, ascending by logical block; valid only for
 *                   the duration of the call
 * @param n_extents  Number of mappings
 * @return           0 to continue the walk, nonzero to stop it
 */
typedef int (*fsd_extfs_file_cb)(void *user, const char *path,
                                 uint64_t inode_no, uint64_t file_size,
                                 const fsd_ext_extent_t *extents,
                                 size_t n_extents);

/**
 * Probe and open a bare ext2/3/4 filesystem image.
 *
 * Fails cleanly (no partial state) unless the superblock magic, geometry and
 * feature flags all check out. `reason_out`, if non-NULL, receives a static
 * string describing why a probe failed (for verbose diagnostics).
 *
 * @return FSD_SUCCESS, FSD_ERR_BAD_MAGIC (not a supported ext filesystem),
 *         FSD_ERR_CORRUPT_DATA (recognized but inconsistent), or
 *         FSD_ERR_OUT_OF_MEMORY / FSD_ERR_INVALID_ARG.
 */
fsd_error_t fsd_extfs_open(fsd_extfs_t **fs_out,
                           const uint8_t *image, size_t image_size,
                           const char **reason_out);

/** Filesystem block size in bytes (1024/2048/4096). */
uint32_t fsd_extfs_block_size(const fsd_extfs_t *fs);

/** Total filesystem blocks (fs block size units). */
uint64_t fsd_extfs_block_count(const fsd_extfs_t *fs);

/**
 * Walk the directory tree from the root inode, invoking `cb` for every
 * regular-file directory entry. Structural problems in individual files or
 * directories cause them to be skipped, not the walk to fail.
 *
 * @return FSD_SUCCESS (including when the callback stopped the walk) or
 *         FSD_ERR_OUT_OF_MEMORY.
 */
fsd_error_t fsd_extfs_walk(fsd_extfs_t *fs, fsd_extfs_file_cb cb, void *user);

/** Release the handle (the image buffer is caller-owned; NULL is safe). */
void fsd_extfs_close(fsd_extfs_t *fs);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_EXT_READER_H */
