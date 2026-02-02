/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file test_buffered_writer.c
 * @brief Buffered writer tests
 */

#include "io/buffered_writer.h"
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

static char test_file[512];

static void init_temp_path(void) {
    char temp_dir[256];
    if (fsd_get_temp_dir(temp_dir) < 0) {
        strcpy(temp_dir, ".");
    }
    snprintf(test_file, sizeof(test_file), "%s/fsdiff_test_writer.bin", temp_dir);
}

static int test_create_close(void) {
    FILE *f = fopen(test_file, "wb");
    TEST_ASSERT(f != NULL, "File open should succeed");

    fsd_buffered_writer_t *writer = NULL;
    fsd_error_t err = fsd_writer_create_from_file(&writer, f, 0);
    TEST_ASSERT(err == FSD_SUCCESS, "Writer creation should succeed");
    TEST_ASSERT(writer != NULL, "Writer should not be NULL");

    fsd_writer_close(writer);
    fclose(f);  /* Close the FILE* since writer doesn't own it */
    fsd_unlink(test_file);
    return 0;
}

static int test_write_byte(void) {
    FILE *f = fopen(test_file, "wb");
    fsd_buffered_writer_t *writer = NULL;
    fsd_writer_create_from_file(&writer, f, 0);

    for (int i = 0; i < 256; i++) {
        fsd_error_t err = fsd_writer_write_byte(writer, (uint8_t)i);
        TEST_ASSERT(err == FSD_SUCCESS, "Write byte should succeed");
    }

    fsd_writer_close(writer);
    fclose(f);  /* Close the FILE* since writer doesn't own it */

    /* Verify written data */
    f = fopen(test_file, "rb");
    for (int i = 0; i < 256; i++) {
        int c = fgetc(f);
        TEST_ASSERT(c == i, "Read byte should match written");
    }
    fclose(f);

    fsd_unlink(test_file);
    return 0;
}

static int test_write_u16(void) {
    FILE *f = fopen(test_file, "wb");
    fsd_buffered_writer_t *writer = NULL;
    fsd_writer_create_from_file(&writer, f, 0);

    fsd_writer_write_u16_le(writer, 0x1234);
    fsd_writer_write_u16_le(writer, 0xABCD);

    fsd_writer_close(writer);
    fclose(f);  /* Close the FILE* since writer doesn't own it */

    /* Verify */
    f = fopen(test_file, "rb");
    uint8_t buf[4];
    size_t n = fread(buf, 1, 4, f);
    fclose(f);

    TEST_ASSERT(n == 4, "Should read 4 bytes");
    TEST_ASSERT(buf[0] == 0x34 && buf[1] == 0x12, "First u16 LE");
    TEST_ASSERT(buf[2] == 0xCD && buf[3] == 0xAB, "Second u16 LE");

    fsd_unlink(test_file);
    return 0;
}

static int test_write_u32(void) {
    FILE *f = fopen(test_file, "wb");
    fsd_buffered_writer_t *writer = NULL;
    fsd_writer_create_from_file(&writer, f, 0);

    fsd_writer_write_u32_le(writer, 0x12345678);

    fsd_writer_close(writer);
    fclose(f);  /* Close the FILE* since writer doesn't own it */

    /* Verify */
    f = fopen(test_file, "rb");
    uint8_t buf[4];
    size_t n = fread(buf, 1, 4, f);
    fclose(f);

    TEST_ASSERT(n == 4, "Should read 4 bytes");
    TEST_ASSERT(buf[0] == 0x78 && buf[1] == 0x56 &&
                buf[2] == 0x34 && buf[3] == 0x12, "u32 LE");

    fsd_unlink(test_file);
    return 0;
}

static int test_write_buffer(void) {
    FILE *f = fopen(test_file, "wb");
    fsd_buffered_writer_t *writer = NULL;
    fsd_writer_create_from_file(&writer, f, 0);

    uint8_t data[1024];
    for (int i = 0; i < 1024; i++) {
        data[i] = (uint8_t)(i & 0xFF);
    }

    fsd_error_t err = fsd_writer_write(writer, data, sizeof(data));
    TEST_ASSERT(err == FSD_SUCCESS, "Buffer write should succeed");

    size_t bytes = fsd_writer_bytes_written(writer);
    TEST_ASSERT(bytes == 1024, "Bytes written should be 1024");

    fsd_writer_close(writer);
    fclose(f);  /* Close the FILE* since writer doesn't own it */

    /* Verify */
    f = fopen(test_file, "rb");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);

    TEST_ASSERT(size == 1024, "File size should be 1024");

    fsd_unlink(test_file);
    return 0;
}

static int test_large_write(void) {
    FILE *f = fopen(test_file, "wb");
    fsd_buffered_writer_t *writer = NULL;
    fsd_writer_create_from_file(&writer, f, 0);

    /* Write more than buffer size */
    size_t total = 1024 * 1024;  /* 1MB */
    uint8_t *data = malloc(total);
    for (size_t i = 0; i < total; i++) {
        data[i] = (uint8_t)(i & 0xFF);
    }

    fsd_error_t err = fsd_writer_write(writer, data, total);
    TEST_ASSERT(err == FSD_SUCCESS, "Large write should succeed");

    size_t bytes = fsd_writer_bytes_written(writer);
    TEST_ASSERT(bytes == total, "Bytes written should match");

    fsd_writer_close(writer);
    fclose(f);  /* Close the FILE* since writer doesn't own it */
    free(data);

    /* Verify size */
    f = fopen(test_file, "rb");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);

    TEST_ASSERT((size_t)size == total, "File size should be 1MB");

    fsd_unlink(test_file);
    return 0;
}

int main(void) {
    int failures = 0;

    printf("Running buffered writer tests...\n");

    init_temp_path();

    failures += test_create_close();
    failures += test_write_byte();
    failures += test_write_u16();
    failures += test_write_u32();
    failures += test_write_buffer();
    failures += test_large_write();

    if (failures == 0) {
        printf("All buffered writer tests passed!\n");
        return 0;
    } else {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
}
