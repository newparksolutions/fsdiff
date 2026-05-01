/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file source_reader.c
 * @brief Source reader implementation with mmap and O_DIRECT backends.
 */

#if defined(__linux__)
/* _GNU_SOURCE pulls in O_DIRECT (and includes _POSIX_C_SOURCE 200809L). */
#define _GNU_SOURCE
#elif defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif

#include "source_reader.h"
#include "mmap_reader.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

#if defined(__linux__)
#include <sys/ioctl.h>
#include <linux/fs.h>
#endif

/* Default DIO alignment when we cannot query the device. */
#define FSD_DIO_DEFAULT_ALIGN 4096u

/* Scratch buffer size for streaming read_at on the direct backend.
 * Sized to amortise pread cost across many small reads while staying
 * well under cache-pressure thresholds on embedded targets. */
#define FSD_DIO_SCRATCH_SIZE (1u * 1024u * 1024u)

typedef enum {
    FSD_BACKEND_MMAP,
    FSD_BACKEND_DIRECT,
} fsd_backend_t;

struct fsd_source_reader {
    fsd_backend_t backend;
    size_t        file_size;

    /* mmap backend */
    fsd_mmap_reader_t *mmap_reader;

    /* direct backend */
    int      fd;
    size_t   alignment;     /* device logical block size, power of two */
    uint8_t *scratch;       /* aligned scratch_size buffer for read_at */
    size_t   scratch_size;  /* >= alignment */
    uint8_t *slurp;         /* lazily allocated full-image buffer for data() */
};

#ifndef _WIN32

static size_t round_up_pow2(size_t v, size_t align) {
    return (v + align - 1) & ~(align - 1);
}

/**
 * Best-effort size query for a regular file or block device.
 * Returns 0 on success (writing size to *out), -1 if the size cannot be
 * determined or exceeds SIZE_MAX (i.e. won't fit in this process's address
 * space — relevant on 32-bit hosts).
 */
static int query_size(int fd, const struct stat *st, size_t *out) {
    uint64_t raw = 0;
    bool have = false;

    if (S_ISREG(st->st_mode)) {
        if (st->st_size < 0) return -1;
        raw = (uint64_t)st->st_size;
        have = true;
    }
#if defined(__linux__) && defined(BLKGETSIZE64)
    if (!have && S_ISBLK(st->st_mode)) {
        if (ioctl(fd, BLKGETSIZE64, &raw) == 0) {
            have = true;
        }
    }
#endif
    if (!have) {
        /* Fallback: lseek to end. Works for many character/block devices. */
        off_t end = lseek(fd, 0, SEEK_END);
        if (end < 0) return -1;
        (void)lseek(fd, 0, SEEK_SET);
        raw = (uint64_t)end;
    }

    if (raw > (uint64_t)SIZE_MAX) return -1;
    *out = (size_t)raw;
    return 0;
}

/**
 * Best-effort logical-block-size query for the direct backend. Defaults
 * to 4 KiB which is the worst-case alignment for any realistic block
 * device or filesystem we'd target.
 */
static size_t query_alignment(int fd, const struct stat *st) {
#if defined(__linux__) && defined(BLKSSZGET)
    if (S_ISBLK(st->st_mode)) {
        int sector = 0;
        if (ioctl(fd, BLKSSZGET, &sector) == 0 && sector > 0) {
            return (size_t)sector;
        }
    }
#endif
    (void)fd;
    (void)st;
    return FSD_DIO_DEFAULT_ALIGN;
}

static fsd_error_t errno_to_fsd(int e) {
    if (e == ENOENT) return FSD_ERR_FILE_NOT_FOUND;
    if (e == EACCES || e == EPERM) return FSD_ERR_PERMISSION;
    return FSD_ERR_IO;
}

#endif /* !_WIN32 */

/* ------------------------------------------------------------------- */
/* mmap backend                                                        */
/* ------------------------------------------------------------------- */

static fsd_error_t open_mmap(fsd_source_reader_t *r, const char *path) {
    fsd_error_t err = fsd_mmap_open(&r->mmap_reader, path);
    if (err != FSD_SUCCESS) return err;
    r->backend   = FSD_BACKEND_MMAP;
    r->file_size = fsd_mmap_size(r->mmap_reader);
    return FSD_SUCCESS;
}

/* ------------------------------------------------------------------- */
/* direct backend                                                      */
/* ------------------------------------------------------------------- */

#ifndef _WIN32

static fsd_error_t open_direct(fsd_source_reader_t *r, const char *path) {
#ifdef O_DIRECT
    int fd = open(path, O_RDONLY | O_DIRECT);
    if (fd < 0) {
        return errno_to_fsd(errno);
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        int e = errno;
        close(fd);
        return errno_to_fsd(e);
    }

    size_t size = 0;
    if (query_size(fd, &st, &size) < 0) {
        close(fd);
        return FSD_ERR_IO;
    }
    size_t align = query_alignment(fd, &st);

    /* Sanity: alignment must be a power of two. If something exotic comes
     * back from BLKSSZGET, fall back to the safe default. */
    if (align == 0 || (align & (align - 1)) != 0) {
        align = FSD_DIO_DEFAULT_ALIGN;
    }

    size_t scratch_size = FSD_DIO_SCRATCH_SIZE;
    if (scratch_size < align) scratch_size = align;
    scratch_size = round_up_pow2(scratch_size, align);

    void *scratch = NULL;
    if (posix_memalign(&scratch, align, scratch_size) != 0) {
        close(fd);
        return FSD_ERR_OUT_OF_MEMORY;
    }

    r->backend      = FSD_BACKEND_DIRECT;
    r->fd           = fd;
    r->file_size    = size;
    r->alignment    = align;
    r->scratch      = (uint8_t *)scratch;
    r->scratch_size = scratch_size;
    return FSD_SUCCESS;
#else
    (void)r;
    (void)path;
    return FSD_ERR_IO;
#endif
}

/**
 * pread exactly len bytes (handles short reads). Bytes past EOF are
 * zero-filled — required because DIO reads into an aligned buffer that
 * may extend past the end of the source.
 */
static fsd_error_t pread_full(int fd, void *buf, size_t len, off_t offset,
                              size_t file_size) {
    uint8_t *p = (uint8_t *)buf;
    size_t   total = 0;
    while (total < len) {
        ssize_t n = pread(fd, p + total, len - total, offset + (off_t)total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return FSD_ERR_IO;
        }
        if (n == 0) {
            /* Past EOF — zero-fill the rest. */
            if ((size_t)offset + total >= file_size) {
                memset(p + total, 0, len - total);
                return FSD_SUCCESS;
            }
            /* Short read before EOF is unexpected. */
            return FSD_ERR_IO;
        }
        total += (size_t)n;
    }
    return FSD_SUCCESS;
}

static fsd_error_t direct_read_at(fsd_source_reader_t *r,
                                  uint64_t offset, size_t len, void *dst) {
    if (len == 0) return FSD_SUCCESS;
    /* Overflow-safe bounds check: len > file_size - offset can't wrap. */
    if (offset > r->file_size || len > r->file_size - offset) {
        return FSD_ERR_TRUNCATED;
    }

    uint8_t *out = (uint8_t *)dst;
    size_t   align = r->alignment;
    size_t   align_mask = align - 1;

    while (len > 0) {
        uint64_t aligned_off = offset & ~(uint64_t)align_mask;
        size_t   skip        = (size_t)(offset - aligned_off);

        /* Aligned chunk we can fit in scratch this iteration. */
        size_t chunk = r->scratch_size;
        /* Don't read past size rounded up — pread_full zero-pads tail. */
        size_t max_aligned = round_up_pow2(r->file_size, align);
        if (aligned_off + chunk > max_aligned) {
            chunk = (size_t)(max_aligned - aligned_off);
        }

        fsd_error_t err = pread_full(r->fd, r->scratch, chunk,
                                     (off_t)aligned_off, r->file_size);
        if (err != FSD_SUCCESS) return err;

        size_t available = chunk - skip;
        size_t copy      = (len < available) ? len : available;
        memcpy(out, r->scratch + skip, copy);

        out    += copy;
        offset += copy;
        len    -= copy;
    }

    return FSD_SUCCESS;
}

static fsd_error_t direct_materialize(fsd_source_reader_t *r) {
    if (r->slurp) return FSD_SUCCESS;
    if (r->file_size == 0) {
        /* Nothing to slurp; return a non-NULL but unreadable pointer. */
        r->slurp = ((uint8_t *)NULL) + 1;
        return FSD_SUCCESS;
    }

    size_t alloc_size = round_up_pow2(r->file_size, r->alignment);
    void *buf = NULL;
    if (posix_memalign(&buf, r->alignment, alloc_size) != 0) {
        return FSD_ERR_OUT_OF_MEMORY;
    }

    fsd_error_t err = pread_full(r->fd, buf, alloc_size, 0, r->file_size);
    if (err != FSD_SUCCESS) {
        free(buf);
        return err;
    }

    r->slurp = (uint8_t *)buf;
    return FSD_SUCCESS;
}

#endif /* !_WIN32 */

/* ------------------------------------------------------------------- */
/* Public API                                                          */
/* ------------------------------------------------------------------- */

fsd_error_t fsd_source_reader_open(fsd_source_reader_t **reader_out,
                                   const char *path,
                                   fsd_source_mode_t mode) {
    if (!reader_out || !path) return FSD_ERR_INVALID_ARG;

    fsd_source_reader_t *r = calloc(1, sizeof(*r));
    if (!r) return FSD_ERR_OUT_OF_MEMORY;
    r->fd = -1;

#ifdef _WIN32
    /* Direct backend not implemented on Windows yet. AUTO uses mmap;
     * explicit DIRECT fails so the caller sees the gap loudly rather
     * than silently getting buffered reads. */
    if (mode == FSD_SOURCE_DIRECT) {
        free(r);
        return FSD_ERR_IO;
    }
    fsd_error_t err = open_mmap(r, path);
    if (err != FSD_SUCCESS) { free(r); return err; }
#else
    fsd_source_mode_t resolved = mode;

    if (resolved == FSD_SOURCE_AUTO) {
        struct stat st;
        if (stat(path, &st) < 0) {
            int e = errno;
            free(r);
            return errno_to_fsd(e);
        }
        resolved = S_ISBLK(st.st_mode) ? FSD_SOURCE_DIRECT : FSD_SOURCE_MMAP;
    }

    fsd_error_t err;
    if (resolved == FSD_SOURCE_DIRECT) {
        err = open_direct(r, path);
    } else {
        err = open_mmap(r, path);
    }
    if (err != FSD_SUCCESS) {
        free(r);
        return err;
    }
#endif

    *reader_out = r;
    return FSD_SUCCESS;
}

fsd_error_t fsd_source_reader_read_at(fsd_source_reader_t *r,
                                      uint64_t offset, size_t len, void *dst) {
    if (!r || (!dst && len > 0)) return FSD_ERR_INVALID_ARG;
    if (len == 0) return FSD_SUCCESS;
    /* Overflow-safe bounds check: len > file_size - offset can't wrap. */
    if (offset > r->file_size || len > r->file_size - offset) {
        return FSD_ERR_TRUNCATED;
    }

    if (r->backend == FSD_BACKEND_MMAP) {
        const uint8_t *base = (const uint8_t *)fsd_mmap_data(r->mmap_reader);
        memcpy(dst, base + offset, len);
        return FSD_SUCCESS;
    }

#ifndef _WIN32
    return direct_read_at(r, offset, len, dst);
#else
    return FSD_ERR_IO;
#endif
}

fsd_error_t fsd_source_reader_data(fsd_source_reader_t *r,
                                   const void **data_out) {
    if (!r || !data_out) return FSD_ERR_INVALID_ARG;

    if (r->backend == FSD_BACKEND_MMAP) {
        *data_out = fsd_mmap_data(r->mmap_reader);
        return FSD_SUCCESS;
    }

#ifndef _WIN32
    fsd_error_t err = direct_materialize(r);
    if (err != FSD_SUCCESS) return err;
    *data_out = r->slurp;
    return FSD_SUCCESS;
#else
    return FSD_ERR_IO;
#endif
}

size_t fsd_source_reader_size(const fsd_source_reader_t *r) {
    return r ? r->file_size : 0;
}

void fsd_source_reader_close(fsd_source_reader_t *r) {
    if (!r) return;

    if (r->mmap_reader) {
        fsd_mmap_close(r->mmap_reader);
    }

#ifndef _WIN32
    if (r->scratch) free(r->scratch);
    if (r->slurp && r->file_size > 0) free(r->slurp);
    if (r->fd >= 0) close(r->fd);
#endif

    free(r);
}
