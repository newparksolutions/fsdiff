/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file platform.h
 * @brief Platform-specific compatibility wrappers
 *
 * NOTE: This header requires POSIX.1-2008 (_POSIX_C_SOURCE 200809L) on Unix.
 * Including source files should define this before any includes.
 */

#ifndef FSDIFF_PLATFORM_H
#define FSDIFF_PLATFORM_H

#include <string.h>
#include <stdio.h>

/*===========================================================================
 * Atomic operations
 *===========================================================================*/
#if !defined(__STDC_NO_ATOMICS__)
#include <stdatomic.h>
#define FSD_ATOMIC _Atomic
#define fsd_atomic_load(x) atomic_load(&(x))
#define fsd_atomic_store(x, v) atomic_store(&(x), (v))
#else
/* Fallback for compilers without C11 atomics */
#define FSD_ATOMIC volatile
#define fsd_atomic_load(x) (x)
#define fsd_atomic_store(x, v) ((x) = (v))
#endif

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>

/* Windows equivalents */
#define fsd_unlink(path) _unlink(path)
#define fsd_close(fd) _close(fd)
#define fsd_open(path, flags, mode) _open(path, flags, mode)

/* File flags */
#define FSD_O_RDWR (_O_RDWR | _O_BINARY)
#define FSD_O_CREAT _O_CREAT
#define FSD_O_EXCL _O_EXCL
#define FSD_O_TRUNC _O_TRUNC

/* File permissions */
#define FSD_MODE_RW (_S_IREAD | _S_IWRITE)

#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>

/* Declare mkstemp if not already declared (POSIX.1-2008) */
#if !defined(__USE_XOPEN2K8) && !defined(__APPLE__) && !defined(__FreeBSD__)
extern int mkstemp(char *template);
#endif

/* Unix standard */
#define fsd_unlink(path) unlink(path)
#define fsd_close(fd) close(fd)
#define fsd_open(path, flags, mode) open(path, flags, mode)

/* File flags */
#define FSD_O_RDWR O_RDWR
#define FSD_O_CREAT O_CREAT
#define FSD_O_EXCL O_EXCL
#define FSD_O_TRUNC O_TRUNC

/* File permissions */
#define FSD_MODE_RW (S_IRUSR | S_IWUSR)

#endif

/**
 * Get platform-specific temporary directory path (without trailing separator).
 *
 * @param buf  Output buffer (must be at least 256 bytes)
 * @return     0 on success, -1 on error
 */
static inline int fsd_get_temp_dir(char *buf) {
#ifdef _WIN32
    DWORD len = GetTempPathA(256, buf);
    if (len == 0 || len >= 256) {
        return -1;
    }
    /* Remove trailing backslash if present */
    if (len > 0 && buf[len - 1] == '\\') {
        buf[len - 1] = '\0';
    }
    return 0;
#else
    strcpy(buf, "/tmp");
    return 0;
#endif
}

/**
 * Construct a path in the temp directory.
 * Uses rotating buffers to allow multiple calls in same expression.
 * Not thread-safe - for test use only.
 *
 * @param filename  Filename to append
 * @return          Full path (static buffer, valid for next 4 calls)
 */
static inline const char *fsd_temp_path(const char *filename) {
    static char path_bufs[4][512];
    static int buf_index = 0;
    static char temp_dir[256];
    static int initialized = 0;

    if (!initialized) {
        if (fsd_get_temp_dir(temp_dir) < 0) {
            strcpy(temp_dir, ".");
        }
        initialized = 1;
    }

    char *buf = path_bufs[buf_index];
    buf_index = (buf_index + 1) % 4;

    snprintf(buf, 512, "%s/%s", temp_dir, filename);
    return buf;
}

/**
 * Create a temporary file in a platform-specific location.
 *
 * @param template_prefix  Prefix for temp file (e.g., "fsdiff_op")
 * @param path_out         Output buffer for path (must be at least 256 bytes)
 * @return                 File descriptor or -1 on error
 */
static inline int fsd_create_temp_file(const char *template_prefix, char *path_out) {
#ifdef _WIN32
    char temp_dir[MAX_PATH];
    DWORD len = GetTempPathA(MAX_PATH, temp_dir);
    if (len == 0 || len > MAX_PATH) {
        return -1;
    }

    /* Generate unique filename (GetTempFileNameA atomically creates the file) */
    char temp_path[MAX_PATH];
    if (GetTempFileNameA(temp_dir, template_prefix, 0, temp_path) == 0) {
        return -1;
    }

    /* Open the file already created by GetTempFileNameA for read/write */
    int fd = fsd_open(temp_path, FSD_O_RDWR | FSD_O_TRUNC, FSD_MODE_RW);
    if (fd < 0) {
        DeleteFileA(temp_path);
        return -1;
    }

    /* Copy path to output */
    strncpy(path_out, temp_path, 255);
    path_out[255] = '\0';

    return fd;
#else
    /* Unix: use mkstemp */
    snprintf(path_out, 256, "/tmp/%s_XXXXXX", template_prefix);
    return mkstemp(path_out);
#endif
}

#endif /* FSDIFF_PLATFORM_H */
