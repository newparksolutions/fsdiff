/**
 * @file platform.h
 * @brief Platform-specific compatibility wrappers
 */

#ifndef FSDIFF_PLATFORM_H
#define FSDIFF_PLATFORM_H

#include <string.h>
#include <stdio.h>

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

    /* Generate unique filename */
    char temp_path[MAX_PATH];
    if (GetTempFileNameA(temp_dir, template_prefix, 0, temp_path) == 0) {
        return -1;
    }

    /* Open the file */
    int fd = fsd_open(temp_path, FSD_O_RDWR | FSD_O_CREAT | FSD_O_EXCL, FSD_MODE_RW);
    if (fd < 0) {
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
