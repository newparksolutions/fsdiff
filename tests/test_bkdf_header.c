/**
 * @file test_bkdf_header.c
 * @brief BKDF header read/write tests
 */

#include "encoding/bkdf_header.h"
#include "../src/platform.h"
#include <stdio.h>
#include <string.h>

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
    snprintf(test_file, sizeof(test_file), "%s/fsdiff_test_header.bin", temp_dir);
}

static int test_write_read(void) {
    /* Write header */
    FILE *f = fopen(test_file, "wb");
    TEST_ASSERT(f != NULL, "File open for write should succeed");

    fsd_error_t err = fsd_header_write(f, 1000, 12, 5000, 2000);
    TEST_ASSERT(err == FSD_SUCCESS, "Header write should succeed");
    fclose(f);

    /* Read header */
    f = fopen(test_file, "rb");
    TEST_ASSERT(f != NULL, "File open for read should succeed");

    fsd_header_t header;
    err = fsd_header_read(f, &header);
    TEST_ASSERT(err == FSD_SUCCESS, "Header read should succeed");
    fclose(f);

    /* Verify fields */
    TEST_ASSERT(header.magic[0] == 'B' && header.magic[1] == 'K' &&
                header.magic[2] == 'D' && header.magic[3] == 'F', "Magic should be BKDF");
    TEST_ASSERT(header.version == FSD_VERSION, "Version should match");
    TEST_ASSERT(header.block_size_log2 == 12, "Block size log2 should be 12");
    TEST_ASSERT(header.dest_blocks == 1000, "Dest blocks should be 1000");
    TEST_ASSERT(header.op_stream_len == 5000, "Op stream len should be 5000");
    TEST_ASSERT(header.diff_stream_len == 2000, "Diff stream len should be 2000");

    fsd_unlink(test_file);
    return 0;
}

static int test_memory_read(void) {
    /* Create header in memory */
    uint8_t buffer[64];
    memset(buffer, 0, sizeof(buffer));

    FILE *f = fopen(test_file, "wb");
    fsd_header_write(f, 500, 14, 1000, 500);
    fclose(f);

    f = fopen(test_file, "rb");
    size_t read_count = fread(buffer, 1, FSD_HEADER_SIZE, f);
    fclose(f);
    TEST_ASSERT(read_count == 32, "Should read 32 bytes");

    /* Read from memory */
    fsd_header_t header;
    fsd_error_t err = fsd_header_read_memory(buffer, FSD_HEADER_SIZE, &header);
    TEST_ASSERT(err == FSD_SUCCESS, "Memory read should succeed");

    TEST_ASSERT(header.dest_blocks == 500, "Dest blocks from memory");
    TEST_ASSERT(header.block_size_log2 == 14, "Block size from memory");

    fsd_unlink(test_file);
    return 0;
}

static int test_invalid_magic(void) {
    /* Create file with bad magic */
    FILE *f = fopen(test_file, "wb");
    uint32_t bad_magic = 0xDEADBEEF;
    fwrite(&bad_magic, 4, 1, f);
    uint8_t padding[28] = {0};
    fwrite(padding, 1, 28, f);
    fclose(f);

    f = fopen(test_file, "rb");
    fsd_header_t header;
    fsd_error_t err = fsd_header_read(f, &header);
    fclose(f);

    TEST_ASSERT(err == FSD_ERR_BAD_MAGIC, "Should detect bad magic");

    fsd_unlink(test_file);
    return 0;
}

static int test_truncated_header(void) {
    /* Create truncated file */
    FILE *f = fopen(test_file, "wb");
    uint8_t data[16] = {0};
    fwrite(data, 1, 16, f);
    fclose(f);

    f = fopen(test_file, "rb");
    fsd_header_t header;
    fsd_error_t err = fsd_header_read(f, &header);
    fclose(f);

    TEST_ASSERT(err == FSD_ERR_TRUNCATED, "Should detect truncated header");

    fsd_unlink(test_file);
    return 0;
}

static int test_header_size(void) {
    /* Verify header is exactly 32 bytes */
    FILE *f = fopen(test_file, "wb");
    fsd_header_write(f, 100, 12, 1000, 500);
    fclose(f);

    f = fopen(test_file, "rb");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);

    TEST_ASSERT(size == 32, "Header should be exactly 32 bytes");

    fsd_unlink(test_file);
    return 0;
}

int main(void) {
    int failures = 0;

    printf("Running BKDF header tests...\n");

    init_temp_path();

    failures += test_write_read();
    failures += test_memory_read();
    failures += test_invalid_magic();
    failures += test_truncated_header();
    failures += test_header_size();

    if (failures == 0) {
        printf("All BKDF header tests passed!\n");
        return 0;
    } else {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
}
