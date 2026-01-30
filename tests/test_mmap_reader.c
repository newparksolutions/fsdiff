/**
 * @file test_mmap_reader.c
 * @brief Memory-mapped reader tests
 */

#include "io/mmap_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/platform.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        return 1; \
    } \
} while(0)

static const char *test_file = "/tmp/fsdiff_test_mmap.bin";

static int create_test_file(size_t size, uint8_t pattern) {
    FILE *f = fopen(test_file, "wb");
    if (!f) return -1;

    uint8_t *buf = malloc(size);
    if (!buf) {
        fclose(f);
        return -1;
    }

    for (size_t i = 0; i < size; i++) {
        buf[i] = (uint8_t)(pattern + i);
    }

    size_t written = fwrite(buf, 1, size, f);
    free(buf);
    fclose(f);

    return (written == size) ? 0 : -1;
}

static void cleanup_test_file(void) {
    fsd_unlink(test_file);
}

static int test_open_close(void) {
    create_test_file(4096, 0);

    fsd_mmap_reader_t *reader = NULL;
    fsd_error_t err = fsd_mmap_open(&reader, test_file);
    TEST_ASSERT(err == FSD_SUCCESS, "Open should succeed");
    TEST_ASSERT(reader != NULL, "Reader should not be NULL");

    fsd_mmap_close(reader);
    cleanup_test_file();
    return 0;
}

static int test_size(void) {
    create_test_file(8192, 0);

    fsd_mmap_reader_t *reader = NULL;
    fsd_mmap_open(&reader, test_file);

    size_t size = fsd_mmap_size(reader);
    TEST_ASSERT(size == 8192, "Size should be 8192");

    fsd_mmap_close(reader);
    cleanup_test_file();
    return 0;
}

static int test_data_access(void) {
    create_test_file(256, 0);

    fsd_mmap_reader_t *reader = NULL;
    fsd_mmap_open(&reader, test_file);

    const uint8_t *data = fsd_mmap_data(reader);
    TEST_ASSERT(data != NULL, "Data should not be NULL");

    /* Verify pattern */
    for (size_t i = 0; i < 256; i++) {
        TEST_ASSERT(data[i] == (uint8_t)i, "Data pattern should match");
    }

    fsd_mmap_close(reader);
    cleanup_test_file();
    return 0;
}

static int test_nonexistent_file(void) {
    fsd_mmap_reader_t *reader = NULL;
    fsd_error_t err = fsd_mmap_open(&reader, "/tmp/nonexistent_file_12345.bin");
    TEST_ASSERT(err == FSD_ERR_FILE_NOT_FOUND, "Should return FILE_NOT_FOUND");
    TEST_ASSERT(reader == NULL, "Reader should be NULL on error");
    return 0;
}

static int test_large_file(void) {
    /* Create a 1MB file */
    size_t size = 1024 * 1024;
    create_test_file(size, 0xAB);

    fsd_mmap_reader_t *reader = NULL;
    fsd_error_t err = fsd_mmap_open(&reader, test_file);
    TEST_ASSERT(err == FSD_SUCCESS, "Large file open should succeed");

    const uint8_t *data = fsd_mmap_data(reader);
    TEST_ASSERT(fsd_mmap_size(reader) == size, "Size should match");

    /* Spot check data */
    TEST_ASSERT(data[0] == (uint8_t)(0xAB + 0), "First byte check");
    TEST_ASSERT(data[size/2] == (uint8_t)(0xAB + (size/2)), "Middle byte check");

    fsd_mmap_close(reader);
    cleanup_test_file();
    return 0;
}

int main(void) {
    int failures = 0;

    printf("Running mmap reader tests...\n");

    failures += test_open_close();
    failures += test_size();
    failures += test_data_access();
    failures += test_nonexistent_file();
    failures += test_large_file();

    if (failures == 0) {
        printf("All mmap reader tests passed!\n");
        return 0;
    } else {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
}
