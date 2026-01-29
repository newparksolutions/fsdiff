/**
 * @file test_roundtrip.c
 * @brief End-to-end roundtrip tests
 */

#define _GNU_SOURCE
#include <fsdiff/fsdiff.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        return 1; \
    } \
} while(0)

static const char *src_file = "/tmp/fsdiff_test_src.bin";
static const char *dest_file = "/tmp/fsdiff_test_dest.bin";
static const char *patch_file = "/tmp/fsdiff_test.patch";
static const char *output_file = "/tmp/fsdiff_test_out.bin";

static void cleanup(void) {
    unlink(src_file);
    unlink(dest_file);
    unlink(patch_file);
    unlink(output_file);
}

static int create_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    return (written == size) ? 0 : -1;
}

static int compare_files(const char *path1, const char *path2) {
    FILE *f1 = fopen(path1, "rb");
    FILE *f2 = fopen(path2, "rb");
    if (!f1 || !f2) {
        if (f1) fclose(f1);
        if (f2) fclose(f2);
        return -1;
    }

    int result = 0;
    while (1) {
        int c1 = fgetc(f1);
        int c2 = fgetc(f2);
        if (c1 != c2) {
            result = 1;
            break;
        }
        if (c1 == EOF) break;
    }

    fclose(f1);
    fclose(f2);
    return result;
}

static int test_identical_files(void) {
    printf("  Testing identical files...\n");

    /* Create identical source and destination */
    size_t size = 4096 * 10;  /* 10 blocks */
    uint8_t *data = malloc(size);
    for (size_t i = 0; i < size; i++) {
        data[i] = (uint8_t)(i & 0xFF);
    }

    create_file(src_file, data, size);
    create_file(dest_file, data, size);
    free(data);

    /* Create diff */
    fsd_diff_ctx_t *diff_ctx = NULL;
    fsd_diff_options_t opts;
    fsd_diff_options_init(&opts);

    fsd_error_t err = fsd_diff_create(&diff_ctx, &opts);
    TEST_ASSERT(err == FSD_SUCCESS, "Diff create should succeed");

    err = fsd_diff_files(diff_ctx, src_file, dest_file, patch_file);
    TEST_ASSERT(err == FSD_SUCCESS, "Diff files should succeed");

    fsd_diff_stats_t stats;
    fsd_diff_get_stats(diff_ctx, &stats);
    TEST_ASSERT(stats.identity_matches == stats.total_blocks,
                "All blocks should be identity matches");

    fsd_diff_destroy(diff_ctx);

    /* Apply patch */
    fsd_patch_ctx_t *patch_ctx = NULL;
    err = fsd_patch_create(&patch_ctx, NULL);
    TEST_ASSERT(err == FSD_SUCCESS, "Patch create should succeed");

    err = fsd_patch_apply(patch_ctx, src_file, patch_file, output_file);
    TEST_ASSERT(err == FSD_SUCCESS, "Patch apply should succeed");

    fsd_patch_destroy(patch_ctx);

    /* Compare output to destination */
    int cmp = compare_files(dest_file, output_file);
    TEST_ASSERT(cmp == 0, "Output should match destination");

    cleanup();
    return 0;
}

static int test_different_files(void) {
    printf("  Testing different files...\n");

    /* Create different source and destination */
    size_t size = 4096 * 10;
    uint8_t *src_data = malloc(size);
    uint8_t *dest_data = malloc(size);

    for (size_t i = 0; i < size; i++) {
        src_data[i] = (uint8_t)(i & 0xFF);
        dest_data[i] = (uint8_t)((i + 1) & 0xFF);  /* All different */
    }

    create_file(src_file, src_data, size);
    create_file(dest_file, dest_data, size);
    free(src_data);
    free(dest_data);

    /* Create diff */
    fsd_diff_ctx_t *diff_ctx = NULL;
    fsd_diff_create(&diff_ctx, NULL);

    fsd_error_t err = fsd_diff_files(diff_ctx, src_file, dest_file, patch_file);
    TEST_ASSERT(err == FSD_SUCCESS, "Diff should succeed");

    fsd_diff_destroy(diff_ctx);

    /* Apply patch */
    fsd_patch_ctx_t *patch_ctx = NULL;
    fsd_patch_create(&patch_ctx, NULL);

    err = fsd_patch_apply(patch_ctx, src_file, patch_file, output_file);
    TEST_ASSERT(err == FSD_SUCCESS, "Patch apply should succeed");

    fsd_patch_destroy(patch_ctx);

    /* Compare */
    int cmp = compare_files(dest_file, output_file);
    TEST_ASSERT(cmp == 0, "Output should match destination");

    cleanup();
    return 0;
}

static int test_zero_blocks(void) {
    printf("  Testing zero blocks...\n");

    /* Source: all zeros, Dest: pattern */
    size_t size = 4096 * 5;
    uint8_t *src_data = calloc(size, 1);  /* All zeros */
    uint8_t *dest_data = malloc(size);

    /* Mix of zeros and pattern */
    memset(dest_data, 0, 4096 * 2);  /* First 2 blocks zero */
    for (size_t i = 4096 * 2; i < size; i++) {
        dest_data[i] = (uint8_t)(i & 0xFF);
    }

    create_file(src_file, src_data, size);
    create_file(dest_file, dest_data, size);
    free(src_data);
    free(dest_data);

    /* Diff and patch */
    fsd_diff_ctx_t *diff_ctx = NULL;
    fsd_diff_create(&diff_ctx, NULL);
    fsd_diff_files(diff_ctx, src_file, dest_file, patch_file);

    fsd_diff_stats_t stats;
    fsd_diff_get_stats(diff_ctx, &stats);
    TEST_ASSERT(stats.zero_blocks >= 2, "Should detect zero blocks");

    fsd_diff_destroy(diff_ctx);

    fsd_patch_ctx_t *patch_ctx = NULL;
    fsd_patch_create(&patch_ctx, NULL);
    fsd_patch_apply(patch_ctx, src_file, patch_file, output_file);
    fsd_patch_destroy(patch_ctx);

    int cmp = compare_files(dest_file, output_file);
    TEST_ASSERT(cmp == 0, "Output should match destination");

    cleanup();
    return 0;
}

static int test_relocated_blocks(void) {
    printf("  Testing relocated blocks...\n");

    /* Source has blocks A, B, C, D, E
     * Dest has blocks E, D, C, B, A (reversed) */
    size_t block_size = 4096;
    size_t num_blocks = 5;
    size_t size = block_size * num_blocks;

    uint8_t *src_data = malloc(size);
    uint8_t *dest_data = malloc(size);

    /* Create unique blocks */
    for (size_t b = 0; b < num_blocks; b++) {
        uint8_t pattern = (uint8_t)(b * 50);
        for (size_t i = 0; i < block_size; i++) {
            src_data[b * block_size + i] = pattern + (uint8_t)(i & 0x0F);
        }
    }

    /* Reverse block order in dest */
    for (size_t b = 0; b < num_blocks; b++) {
        memcpy(dest_data + b * block_size,
               src_data + (num_blocks - 1 - b) * block_size,
               block_size);
    }

    create_file(src_file, src_data, size);
    create_file(dest_file, dest_data, size);
    free(src_data);
    free(dest_data);

    /* Diff */
    fsd_diff_ctx_t *diff_ctx = NULL;
    fsd_diff_create(&diff_ctx, NULL);
    fsd_diff_files(diff_ctx, src_file, dest_file, patch_file);

    fsd_diff_stats_t stats;
    fsd_diff_get_stats(diff_ctx, &stats);
    /* Should detect relocated blocks (except middle one which is identity) */
    TEST_ASSERT(stats.identity_matches + stats.relocate_matches >= num_blocks - 1,
                "Should detect relocated/identity blocks");

    fsd_diff_destroy(diff_ctx);

    /* Patch */
    fsd_patch_ctx_t *patch_ctx = NULL;
    fsd_patch_create(&patch_ctx, NULL);
    fsd_error_t err = fsd_patch_apply(patch_ctx, src_file, patch_file, output_file);
    TEST_ASSERT(err == FSD_SUCCESS, "Patch should succeed");
    fsd_patch_destroy(patch_ctx);

    /* Verify */
    int cmp = compare_files(dest_file, output_file);
    TEST_ASSERT(cmp == 0, "Output should match destination");

    cleanup();
    return 0;
}

int main(void) {
    int failures = 0;

    printf("Running roundtrip tests...\n");

    /* Initialize library */
    fsd_error_t err = fsd_init();
    if (err != FSD_SUCCESS) {
        fprintf(stderr, "Failed to initialize library\n");
        return 1;
    }

    failures += test_identical_files();
    failures += test_different_files();
    failures += test_zero_blocks();
    failures += test_relocated_blocks();

    fsd_cleanup();

    if (failures == 0) {
        printf("All roundtrip tests passed!\n");
        return 0;
    } else {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
}
