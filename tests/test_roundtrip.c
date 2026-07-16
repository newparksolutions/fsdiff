/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file test_roundtrip.c
 * @brief End-to-end roundtrip tests
 */

#include <fsdiff/fsdiff.h>
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

/* Platform-portable temp file paths */
static char temp_dir[256];
static char src_file[512];
static char dest_file[512];
static char patch_file[512];
static char output_file[512];

static void init_temp_paths(void) {
    if (fsd_get_temp_dir(temp_dir) < 0) {
        strcpy(temp_dir, ".");
    }
    snprintf(src_file, sizeof(src_file), "%s/fsdiff_test_src.bin", temp_dir);
    snprintf(dest_file, sizeof(dest_file), "%s/fsdiff_test_dest.bin", temp_dir);
    snprintf(patch_file, sizeof(patch_file), "%s/fsdiff_test.patch", temp_dir);
    snprintf(output_file, sizeof(output_file), "%s/fsdiff_test_out.bin", temp_dir);
}

static void cleanup(void) {
    fsd_unlink(src_file);
    fsd_unlink(dest_file);
    fsd_unlink(patch_file);
    fsd_unlink(output_file);
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

static int test_one_blocks(void) {
    printf("  Testing one blocks...\n");

    /* Source: random pattern, Dest: mix of 0xFF blocks and pattern */
    size_t size = 4096 * 5;
    uint8_t *src_data = malloc(size);
    uint8_t *dest_data = malloc(size);

    for (size_t i = 0; i < size; i++) {
        src_data[i] = (uint8_t)(i & 0xFF);
    }

    /* Mix of 0xFF blocks and pattern */
    memset(dest_data, 0xFF, 4096 * 2);  /* First 2 blocks all 0xFF */
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
    TEST_ASSERT(stats.one_blocks >= 2, "Should detect one blocks");

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

/* Diff+apply helper for the fsmap test: returns patch size, or -1 on any
 * failure (asserts are in the caller for clearer messages). */
static long diff_apply_size(const char *src, const char *dest,
                            const char *patch, const char *out,
                            bool enable_fsmap) {
    fsd_diff_ctx_t *diff_ctx = NULL;
    fsd_diff_options_t opts;
    fsd_diff_options_init(&opts);
    opts.enable_fsmap = enable_fsmap;
    if (fsd_diff_create(&diff_ctx, &opts) != FSD_SUCCESS) return -1;
    fsd_error_t err = fsd_diff_files(diff_ctx, src, dest, patch);
    fsd_diff_destroy(diff_ctx);
    if (err != FSD_SUCCESS) return -1;

    fsd_patch_ctx_t *patch_ctx = NULL;
    if (fsd_patch_create(&patch_ctx, NULL) != FSD_SUCCESS) return -1;
    err = fsd_patch_apply(patch_ctx, src, patch, out);
    fsd_patch_destroy(patch_ctx);
    if (err != FSD_SUCCESS) return -1;

    FILE *f = fopen(patch, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    return size;
}

/* Filesystem-aware matching on a real ext4 pair built with mkfs.ext4:
 * both patches must reconstruct the destination byte-identically and the
 * fsmap-enabled patch must not be larger. Skipped without mkfs.ext4. */
static int test_fsmap_ext4_pair(void) {
    printf("  Testing fsmap on an ext4 pair...\n");
#ifdef _WIN32
    printf("    SKIP: requires mkfs.ext4\n");
    return 0;
#else
    if (system("command -v mkfs.ext4 > /dev/null 2>&1") != 0) {
        printf("    SKIP: mkfs.ext4 not available\n");
        return 0;
    }

    char tree[600], cmd[2048];
    snprintf(tree, sizeof(tree), "%s/fsdiff_test_tree", temp_dir);

    /* Source tree: two files of pseudo-random data */
    snprintf(cmd, sizeof(cmd),
             "rm -rf %s '%s' '%s' && mkdir -p %s/dir && "
             "head -c 262144 /dev/urandom > %s/dir/app.bin && "
             "head -c  65536 /dev/urandom > %s/config.dat && "
             "mkfs.ext4 -q -F -b 4096 -d %s '%s' 8M > /dev/null 2>&1",
             tree, src_file, dest_file, tree, tree, tree, tree, src_file);
    if (system(cmd) != 0) {
        printf("    SKIP: fixture build failed\n");
        return 0;
    }
    /* Dest tree: app.bin modified in the middle (partial matches at the
     * same path), config.dat unchanged, one new file (literals) */
    snprintf(cmd, sizeof(cmd),
             "dd if=/dev/urandom of=%s/dir/app.bin bs=1 seek=100000 "
             "count=9000 conv=notrunc status=none && "
             "head -c 30000 /dev/urandom > %s/new.bin && "
             "mkfs.ext4 -q -F -b 4096 -d %s '%s' 8M > /dev/null 2>&1",
             tree, tree, tree, dest_file);
    if (system(cmd) != 0) {
        printf("    SKIP: dest fixture build failed\n");
        return 0;
    }

    char out2[600], patch2[600];
    snprintf(out2, sizeof(out2), "%s/fsdiff_test_out2.bin", temp_dir);
    snprintf(patch2, sizeof(patch2), "%s/fsdiff_test2.patch", temp_dir);

    long with_fsmap = diff_apply_size(src_file, dest_file,
                                      patch_file, output_file, true);
    TEST_ASSERT(with_fsmap > 0, "fsmap diff+apply should succeed");
    TEST_ASSERT(compare_files(dest_file, output_file) == 0,
                "fsmap patch output should match destination");

    long without = diff_apply_size(src_file, dest_file, patch2, out2, false);
    TEST_ASSERT(without > 0, "no-fsmap diff+apply should succeed");
    TEST_ASSERT(compare_files(dest_file, out2) == 0,
                "no-fsmap patch output should match destination");

    TEST_ASSERT(with_fsmap <= without,
                "fsmap patch should not be larger");

    char cleanup_cmd[2048];
    snprintf(cleanup_cmd, sizeof(cleanup_cmd), "rm -rf %s '%s' '%s'",
             tree, out2, patch2);
    if (system(cleanup_cmd) != 0) { /* best-effort cleanup */ }
    cleanup();
    return 0;
#endif
}

int main(void) {
    int failures = 0;

    printf("Running roundtrip tests...\n");

    /* Initialize temp file paths */
    init_temp_paths();

    /* Initialize library */
    fsd_error_t err = fsd_init();
    if (err != FSD_SUCCESS) {
        fprintf(stderr, "Failed to initialize library\n");
        return 1;
    }

    failures += test_identical_files();
    failures += test_different_files();
    failures += test_zero_blocks();
    failures += test_one_blocks();
    failures += test_relocated_blocks();
    failures += test_fsmap_ext4_pair();

    fsd_cleanup();

    if (failures == 0) {
        printf("All roundtrip tests passed!\n");
        return 0;
    } else {
        printf("%d test(s) failed\n", failures);
        return 1;
    }
}
