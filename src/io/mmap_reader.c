/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file mmap_reader.c
 * @brief Memory-mapped file reader implementation
 */

/* Enable POSIX extensions for madvise (performance hint, optional) */
#if defined(__linux__) || defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif

#include "mmap_reader.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#endif

fsd_error_t fsd_mmap_open(fsd_mmap_reader_t **reader_out, const char *path) {
    if (!reader_out || !path) {
        return FSD_ERR_INVALID_ARG;
    }

    fsd_mmap_reader_t *reader = calloc(1, sizeof(fsd_mmap_reader_t));
    if (!reader) {
        return FSD_ERR_OUT_OF_MEMORY;
    }

#ifdef _WIN32
    /* Windows implementation */
    reader->file_handle = CreateFileA(
        path,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (reader->file_handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        free(reader);
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
            return FSD_ERR_FILE_NOT_FOUND;
        }
        if (err == ERROR_ACCESS_DENIED) {
            return FSD_ERR_PERMISSION;
        }
        return FSD_ERR_IO;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(reader->file_handle, &size)) {
        CloseHandle(reader->file_handle);
        free(reader);
        return FSD_ERR_IO;
    }
    reader->file_size = (size_t)size.QuadPart;

    if (reader->file_size == 0) {
        /* Empty file - no mapping needed */
        reader->base_addr = NULL;
        reader->mapping_handle = NULL;
        *reader_out = reader;
        return FSD_SUCCESS;
    }

    reader->mapping_handle = CreateFileMappingA(
        reader->file_handle,
        NULL,
        PAGE_READONLY,
        0, 0,
        NULL
    );

    if (!reader->mapping_handle) {
        CloseHandle(reader->file_handle);
        free(reader);
        return FSD_ERR_MMAP_FAILED;
    }

    reader->base_addr = MapViewOfFile(
        reader->mapping_handle,
        FILE_MAP_READ,
        0, 0,
        0
    );

    if (!reader->base_addr) {
        CloseHandle(reader->mapping_handle);
        CloseHandle(reader->file_handle);
        free(reader);
        return FSD_ERR_MMAP_FAILED;
    }

#else
    /* Unix implementation */
    reader->fd = open(path, O_RDONLY);
    if (reader->fd < 0) {
        free(reader);
        if (errno == ENOENT) {
            return FSD_ERR_FILE_NOT_FOUND;
        }
        if (errno == EACCES || errno == EPERM) {
            return FSD_ERR_PERMISSION;
        }
        return FSD_ERR_IO;
    }

    struct stat st;
    if (fstat(reader->fd, &st) < 0) {
        close(reader->fd);
        free(reader);
        return FSD_ERR_IO;
    }
    reader->file_size = (size_t)st.st_size;

    if (reader->file_size == 0) {
        /* Empty file - no mapping needed */
        reader->base_addr = NULL;
        *reader_out = reader;
        return FSD_SUCCESS;
    }

    reader->base_addr = mmap(
        NULL,
        reader->file_size,
        PROT_READ,
        MAP_PRIVATE,
        reader->fd,
        0
    );

    if (reader->base_addr == MAP_FAILED) {
        close(reader->fd);
        free(reader);
        return FSD_ERR_MMAP_FAILED;
    }

    /* Advise kernel about sequential access pattern (optional optimization) */
#ifdef MADV_SEQUENTIAL
    (void)madvise(reader->base_addr, reader->file_size, MADV_SEQUENTIAL);
#endif
#endif

    *reader_out = reader;
    return FSD_SUCCESS;
}

const void *fsd_mmap_data(const fsd_mmap_reader_t *reader) {
    return reader ? reader->base_addr : NULL;
}

size_t fsd_mmap_size(const fsd_mmap_reader_t *reader) {
    return reader ? reader->file_size : 0;
}

void fsd_mmap_close(fsd_mmap_reader_t *reader) {
    if (!reader) return;

#ifdef _WIN32
    if (reader->base_addr) {
        UnmapViewOfFile(reader->base_addr);
    }
    if (reader->mapping_handle) {
        CloseHandle(reader->mapping_handle);
    }
    if (reader->file_handle && reader->file_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(reader->file_handle);
    }
#else
    if (reader->base_addr && reader->file_size > 0) {
        munmap(reader->base_addr, reader->file_size);
    }
    if (reader->fd >= 0) {
        close(reader->fd);
    }
#endif

    free(reader);
}
