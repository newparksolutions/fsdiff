/*
 * fuzz_common.h - shared helpers for the fsdiff libFuzzer harnesses.
 *
 * Everything the library touches is a memfd, addressed through
 * /proc/self/fd/N, so the harnesses exercise the real path-based public
 * API (open, mmap, fopen) without touching the disk.
 */

#ifndef FSDIFF_FUZZ_COMMON_H
#define FSDIFF_FUZZ_COMMON_H

#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

/* A memfd plus the path the library is given for it. */
typedef struct {
    int  fd;
    char path[32];
} fz_file_t;

static inline void fz_die(const char *what) {
    perror(what);
    abort();
}

static inline void fz_file_init(fz_file_t *f, const char *name) {
    f->fd = memfd_create(name, 0);
    if (f->fd < 0) fz_die("memfd_create");
    snprintf(f->path, sizeof(f->path), "/proc/self/fd/%d", f->fd);
}

/* Replace the file's contents with data[0..len). */
static inline void fz_file_set(fz_file_t *f, const void *data, size_t len) {
    if (ftruncate(f->fd, 0) != 0) fz_die("ftruncate");
    const uint8_t *p = (const uint8_t *)data;
    size_t done = 0;
    while (done < len) {
        ssize_t n = pwrite(f->fd, p + done, len - done, (off_t)done);
        if (n <= 0) fz_die("pwrite");
        done += (size_t)n;
    }
}

static inline size_t fz_file_size(const fz_file_t *f) {
    struct stat st;
    if (fstat(f->fd, &st) != 0) fz_die("fstat");
    return (size_t)st.st_size;
}

/* Read the whole file into a malloc'd buffer (caller frees). */
static inline uint8_t *fz_file_read(const fz_file_t *f, size_t *len_out) {
    size_t len = fz_file_size(f);
    uint8_t *buf = malloc(len ? len : 1);
    if (!buf) fz_die("malloc");
    size_t done = 0;
    while (done < len) {
        ssize_t n = pread(f->fd, buf + done, len - done, (off_t)done);
        if (n <= 0) fz_die("pread");
        done += (size_t)n;
    }
    *len_out = len;
    return buf;
}

/* Pattern seed for fuzz_patch's source images; gen_seeds must match. */
#define FZ_SOURCE_SEED 0x5EEDF00DULL

/* Deterministic, non-repeating byte pattern (splitmix64 of the position),
 * so a copy from the wrong offset produces visibly different output. */
static inline void fz_fill_pattern(uint8_t *buf, size_t len, uint64_t seed) {
    for (size_t i = 0; i < len; i += 8) {
        uint64_t z = seed + (uint64_t)(i / 8 + 1) * 0x9E3779B97F4A7C15ULL;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z ^= z >> 31;
        for (size_t j = 0; j < 8 && i + j < len; j++) {
            buf[i + j] = (uint8_t)(z >> (8 * j));
        }
    }
}

static inline void fz_put_u64_le(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static inline uint64_t fz_get_u64_le(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

/* Write a 32-byte BKDF header. */
static inline void fz_put_header(uint8_t *h, uint8_t block_size_log2,
                                 uint64_t dest_blocks, uint64_t op_len,
                                 uint64_t diff_len) {
    h[0] = 'B'; h[1] = 'K'; h[2] = 'D'; h[3] = 'F';
    h[4] = 1;
    h[5] = block_size_log2;
    h[6] = 0; h[7] = 0;
    fz_put_u64_le(&h[8], dest_blocks);
    fz_put_u64_le(&h[16], op_len);
    fz_put_u64_le(&h[24], diff_len);
}

/* Sequential reader over the fuzz input; returns zeros once exhausted. */
typedef struct {
    const uint8_t *p;
    size_t         left;
} fz_reader_t;

static inline uint8_t fz_u8(fz_reader_t *r) {
    if (r->left == 0) return 0;
    r->left--;
    return *r->p++;
}

static inline uint16_t fz_u16(fz_reader_t *r) {
    uint16_t lo = fz_u8(r);
    return (uint16_t)(lo | (uint16_t)fz_u8(r) << 8);
}

#endif /* FSDIFF_FUZZ_COMMON_H */
