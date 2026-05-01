/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file test_source_reader.c
 * @brief Tests for source_reader (mmap and O_DIRECT backends).
 */

#include "io/source_reader.h"
#include "../src/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        return 1; \
    } \
} while(0)

#define TEST_FILE_SIZE (64 * 1024)  /* 64 KiB, multiple of any plausible DIO alignment */

static char test_file[512];

static void init_temp_path(void) {
    char temp_dir[256];
    if (fsd_get_temp_dir(temp_dir) < 0) {
        strcpy(temp_dir, ".");
    }
    snprintf(test_file, sizeof(test_file), "%s/fsdiff_test_source_reader.bin", temp_dir);
}

static int create_test_file(size_t size) {
    FILE *f = fopen(test_file, "wb");
    if (!f) return -1;

    uint8_t *buf = malloc(size);
    if (!buf) { fclose(f); return -1; }
    for (size_t i = 0; i < size; i++) {
        buf[i] = (uint8_t)((i * 31u + 7u) & 0xff);  /* deterministic pattern */
    }

    size_t written = fwrite(buf, 1, size, f);
    free(buf);
    fclose(f);
    return (written == size) ? 0 : -1;
}

static uint8_t expected_byte(size_t i) {
    return (uint8_t)((i * 31u + 7u) & 0xff);
}

static void cleanup(void) { fsd_unlink(test_file); }

/* ------------------------------------------------------------------- */

static int test_mmap_open_read(void) {
    if (create_test_file(TEST_FILE_SIZE) < 0) return 1;

    fsd_source_reader_t *r = NULL;
    fsd_error_t err = fsd_source_reader_open(&r, test_file, FSD_SOURCE_MMAP);
    TEST_ASSERT(err == FSD_SUCCESS, "mmap open should succeed");
    TEST_ASSERT(fsd_source_reader_size(r) == TEST_FILE_SIZE, "size should match");

    /* Aligned read */
    uint8_t buf[4096];
    err = fsd_source_reader_read_at(r, 0, sizeof(buf), buf);
    TEST_ASSERT(err == FSD_SUCCESS, "mmap read_at(0,4096) should succeed");
    for (size_t i = 0; i < sizeof(buf); i++) {
        TEST_ASSERT(buf[i] == expected_byte(i), "mmap read_at: aligned bytes match");
    }

    /* Unaligned read */
    err = fsd_source_reader_read_at(r, 7, 100, buf);
    TEST_ASSERT(err == FSD_SUCCESS, "mmap read_at unaligned should succeed");
    for (size_t i = 0; i < 100; i++) {
        TEST_ASSERT(buf[i] == expected_byte(i + 7), "mmap read_at: unaligned bytes match");
    }

    /* data() returns base pointer */
    const void *data = NULL;
    err = fsd_source_reader_data(r, &data);
    TEST_ASSERT(err == FSD_SUCCESS && data != NULL, "mmap data() should succeed");
    TEST_ASSERT(((const uint8_t *)data)[42] == expected_byte(42), "mmap data() bytes match");

    fsd_source_reader_close(r);
    cleanup();
    return 0;
}

static int test_auto_picks_mmap_for_regular_file(void) {
    if (create_test_file(TEST_FILE_SIZE) < 0) return 1;

    /* AUTO on a regular file should succeed unconditionally — that's the
     * mmap path. We can't directly inspect which backend was chosen, but
     * the success itself rules out the direct-open-fails branch (DIO is
     * not supported on every tmpfs/etc.). */
    fsd_source_reader_t *r = NULL;
    fsd_error_t err = fsd_source_reader_open(&r, test_file, FSD_SOURCE_AUTO);
    TEST_ASSERT(err == FSD_SUCCESS, "auto open on regular file should succeed");
    TEST_ASSERT(fsd_source_reader_size(r) == TEST_FILE_SIZE, "auto: size matches");

    fsd_source_reader_close(r);
    cleanup();
    return 0;
}

static int test_direct_backend_if_supported(void) {
    if (create_test_file(TEST_FILE_SIZE) < 0) return 1;

    /* Direct backend on a regular file may fail if the temp dir's FS
     * doesn't support O_DIRECT (tmpfs, FUSE). Treat that as a skip rather
     * than a failure so the test suite stays portable. */
    fsd_source_reader_t *r = NULL;
    fsd_error_t err = fsd_source_reader_open(&r, test_file, FSD_SOURCE_DIRECT);
    if (err != FSD_SUCCESS) {
        printf("  (skipped: O_DIRECT not supported on temp FS, err=%d)\n", err);
        cleanup();
        return 0;
    }

    TEST_ASSERT(fsd_source_reader_size(r) == TEST_FILE_SIZE, "direct: size matches");

    /* Aligned read */
    uint8_t buf[8192];
    err = fsd_source_reader_read_at(r, 4096, sizeof(buf), buf);
    TEST_ASSERT(err == FSD_SUCCESS, "direct read_at aligned should succeed");
    for (size_t i = 0; i < sizeof(buf); i++) {
        TEST_ASSERT(buf[i] == expected_byte(i + 4096), "direct read_at: aligned bytes match");
    }

    /* Unaligned offset and length, crossing alignment boundaries */
    err = fsd_source_reader_read_at(r, 513, 4099, buf);
    TEST_ASSERT(err == FSD_SUCCESS, "direct read_at unaligned should succeed");
    for (size_t i = 0; i < 4099; i++) {
        TEST_ASSERT(buf[i] == expected_byte(i + 513), "direct read_at: unaligned bytes match");
    }

    /* Read up to last byte */
    uint8_t tail[256];
    err = fsd_source_reader_read_at(r, TEST_FILE_SIZE - 256, 256, tail);
    TEST_ASSERT(err == FSD_SUCCESS, "direct read tail should succeed");
    for (size_t i = 0; i < 256; i++) {
        TEST_ASSERT(tail[i] == expected_byte(TEST_FILE_SIZE - 256 + i), "direct tail: bytes match");
    }

    /* data() should slurp and return a pointer to the full image */
    const void *data = NULL;
    err = fsd_source_reader_data(r, &data);
    TEST_ASSERT(err == FSD_SUCCESS && data != NULL, "direct data() should succeed");
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < TEST_FILE_SIZE; i += 1024) {
        TEST_ASSERT(bytes[i] == expected_byte(i), "direct data() spot check");
    }
    /* Second call should be cheap (cached) */
    const void *data2 = NULL;
    err = fsd_source_reader_data(r, &data2);
    TEST_ASSERT(err == FSD_SUCCESS && data2 == data, "direct data() second call returns same buffer");

    fsd_source_reader_close(r);
    cleanup();
    return 0;
}

static int test_read_past_eof(void) {
    if (create_test_file(TEST_FILE_SIZE) < 0) return 1;

    fsd_source_reader_t *r = NULL;
    fsd_error_t err = fsd_source_reader_open(&r, test_file, FSD_SOURCE_MMAP);
    TEST_ASSERT(err == FSD_SUCCESS, "open should succeed");

    uint8_t buf[16];
    err = fsd_source_reader_read_at(r, TEST_FILE_SIZE - 8, 16, buf);
    TEST_ASSERT(err == FSD_ERR_TRUNCATED, "read past EOF should return TRUNCATED");

    err = fsd_source_reader_read_at(r, TEST_FILE_SIZE + 1, 1, buf);
    TEST_ASSERT(err == FSD_ERR_TRUNCATED, "read at offset > size should return TRUNCATED");

    fsd_source_reader_close(r);
    cleanup();
    return 0;
}

static int test_nonexistent_file(void) {
    fsd_source_reader_t *r = NULL;
    fsd_error_t err = fsd_source_reader_open(&r, "/tmp/fsdiff_nonexistent_xyz_98765.bin",
                                             FSD_SOURCE_AUTO);
    TEST_ASSERT(err == FSD_ERR_FILE_NOT_FOUND, "missing file should return FILE_NOT_FOUND");
    TEST_ASSERT(r == NULL, "reader should be NULL on error");
    return 0;
}

int main(void) {
    int failures = 0;
    printf("Running source_reader tests...\n");

    init_temp_path();

    failures += test_mmap_open_read();
    failures += test_auto_picks_mmap_for_regular_file();
    failures += test_direct_backend_if_supported();
    failures += test_read_past_eof();
    failures += test_nonexistent_file();

    if (failures == 0) {
        printf("All source_reader tests passed!\n");
        return 0;
    }
    printf("%d test(s) failed\n", failures);
    return 1;
}
