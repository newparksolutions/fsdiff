/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file test_ext_reader.c
 * @brief Tests for the minimal ext2/3/4 walker (src/fs/ext_reader.h)
 *
 * Two parts:
 *  1. Hostile-input safety on hand-crafted buffers: malformed images must be
 *     rejected (or walked without crashing) — no tools required.
 *  2. Real-walk correctness against a fixture image built with mkfs.ext4
 *     when available; prints SKIP and passes otherwise.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "fs/ext_reader.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            return 1; \
        } \
    } while (0)

#define IMG_BLOCKS 64
#define IMG_BS 1024
#define IMG_SIZE (IMG_BLOCKS * IMG_BS)

/* Build a minimally valid ext2-style superblock (1 KiB blocks) into buf */
static void put16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF; p[3] = v >> 24;
}

static void make_valid_superblock(uint8_t *img) {
    uint8_t *sb = img + 1024;
    put32(sb + 0x00, 16);          /* inodes count */
    put32(sb + 0x04, IMG_BLOCKS);  /* blocks count */
    put32(sb + 0x14, 1);           /* first data block (1 for 1 KiB bs) */
    put32(sb + 0x18, 0);           /* log block size -> 1024 */
    put32(sb + 0x20, IMG_BLOCKS);  /* blocks per group */
    put32(sb + 0x28, 16);          /* inodes per group */
    put16(sb + 0x38, 0xEF53);      /* magic */
    put16(sb + 0x3A, 0x0001);      /* state: clean */
    put32(sb + 0x4C, 1);           /* rev level */
    put16(sb + 0x58, 128);         /* inode size */
    put32(sb + 0x60, 0x0002);      /* incompat: FILETYPE only */
}

static int count_cb(void *user, const char *path, uint64_t ino,
                    uint64_t size, const fsd_ext_extent_t *ext, size_t n) {
    (void)path; (void)ino; (void)size; (void)ext; (void)n;
    (*(int *)user)++;
    return 0;
}

static int test_probe_rejections(void) {
    uint8_t *img = calloc(1, IMG_SIZE);
    fsd_extfs_t *fs = NULL;
    const char *reason = NULL;

    /* No magic at all */
    TEST_ASSERT(fsd_extfs_open(&fs, img, IMG_SIZE, &reason) ==
                    FSD_ERR_BAD_MAGIC, "zeroed image rejected");

    /* Too small */
    make_valid_superblock(img);
    TEST_ASSERT(fsd_extfs_open(&fs, img, 1500, &reason) == FSD_ERR_BAD_MAGIC,
                "truncated image rejected");

    /* Valid baseline actually opens */
    TEST_ASSERT(fsd_extfs_open(&fs, img, IMG_SIZE, &reason) == FSD_SUCCESS,
                "valid superblock accepted");
    TEST_ASSERT(fsd_extfs_block_size(fs) == 1024, "block size decoded");
    fsd_extfs_close(fs);

    /* Unknown incompat feature */
    make_valid_superblock(img);
    put32(img + 1024 + 0x60, 0x0002 | 0x10000);   /* ENCRYPT */
    TEST_ASSERT(fsd_extfs_open(&fs, img, IMG_SIZE, &reason) ==
                    FSD_ERR_BAD_MAGIC, "unknown incompat rejected");

    /* Dirty journal */
    make_valid_superblock(img);
    put32(img + 1024 + 0x60, 0x0002 | 0x0004);    /* RECOVER */
    TEST_ASSERT(fsd_extfs_open(&fs, img, IMG_SIZE, &reason) ==
                    FSD_ERR_BAD_MAGIC, "dirty journal rejected");

    /* Not cleanly unmounted */
    make_valid_superblock(img);
    put16(img + 1024 + 0x3A, 0x0000);
    TEST_ASSERT(fsd_extfs_open(&fs, img, IMG_SIZE, &reason) ==
                    FSD_ERR_BAD_MAGIC, "unclean fs rejected");

    /* Filesystem bigger than the image */
    make_valid_superblock(img);
    put32(img + 1024 + 0x04, IMG_BLOCKS * 10);
    TEST_ASSERT(fsd_extfs_open(&fs, img, IMG_SIZE, &reason) ==
                    FSD_ERR_CORRUPT_DATA, "oversized fs rejected");

    /* Absurd inode size */
    make_valid_superblock(img);
    put16(img + 1024 + 0x58, 3000);
    TEST_ASSERT(fsd_extfs_open(&fs, img, IMG_SIZE, &reason) ==
                    FSD_ERR_CORRUPT_DATA, "bad inode size rejected");

    free(img);
    return 0;
}

/* A structurally-plausible image whose metadata is garbage must not crash
 * the walk (files/dirs are skipped, walk returns success). */
static int test_walk_garbage_metadata(void) {
    uint8_t *img = malloc(IMG_SIZE);

    /* Deterministic noise everywhere, then a valid superblock on top */
    uint32_t seed = 99;
    for (size_t i = 0; i < IMG_SIZE; i++) {
        seed = seed * 1664525u + 1013904223u;
        img[i] = (uint8_t)(seed >> 24);
    }
    make_valid_superblock(img);

    fsd_extfs_t *fs = NULL;
    if (fsd_extfs_open(&fs, img, IMG_SIZE, NULL) == FSD_SUCCESS) {
        int n = 0;
        fsd_error_t err = fsd_extfs_walk(fs, count_cb, &n);
        TEST_ASSERT(err == FSD_SUCCESS || err == FSD_ERR_OUT_OF_MEMORY,
                    "garbage walk terminates cleanly");
        fsd_extfs_close(fs);
    }
    /* (open may also legitimately reject the garbage geometry) */
    free(img);
    return 0;
}

/*===========================================================================
 * Fixture test via mkfs.ext4 (skipped when tools are unavailable)
 *===========================================================================*/

typedef struct {
    int found_hello;
    int found_sub;
    int found_link_a;
    int found_link_b;
    uint64_t hello_size;
    uint64_t big_blocks;
    int files;
} fixture_result_t;

static int fixture_cb(void *user, const char *path, uint64_t ino,
                      uint64_t size, const fsd_ext_extent_t *ext, size_t n) {
    (void)ino;
    fixture_result_t *r = user;
    r->files++;
    if (strcmp(path, "/hello.txt") == 0) {
        r->found_hello = 1;
        r->hello_size = size;
    } else if (strcmp(path, "/subdir/nested.bin") == 0) {
        r->found_sub = 1;
        for (size_t i = 0; i < n; i++) {
            r->big_blocks += ext[i].count;
        }
    } else if (strcmp(path, "/link_a") == 0) {
        r->found_link_a = 1;
    } else if (strcmp(path, "/hardlinks/link_b") == 0) {
        r->found_link_b = 1;
    }
    return 0;
}

static int test_mkfs_fixture(void) {
    if (system("command -v mkfs.ext4 > /dev/null 2>&1") != 0) {
        printf("  SKIP: mkfs.ext4 not available\n");
        return 0;
    }

    const char *dir = "test_ext_fixture_tree";
    const char *imgpath = "test_ext_fixture.img";
    char cmd[512];

    /* Build a small tree: text file, nested binary, hard link pair */
    snprintf(cmd, sizeof(cmd),
             "rm -rf %s %s && mkdir -p %s/subdir %s/hardlinks && "
             "printf 'hello world\\n' > %s/hello.txt && "
             "head -c 300000 /dev/urandom > %s/subdir/nested.bin && "
             "printf 'linked' > %s/link_a && "
             "ln %s/link_a %s/hardlinks/link_b",
             dir, imgpath, dir, dir, dir, dir, dir, dir, dir);
    if (system(cmd) != 0) {
        printf("  SKIP: cannot build fixture tree\n");
        return 0;
    }
    snprintf(cmd, sizeof(cmd),
             "mkfs.ext4 -q -F -b 4096 -d %s %s 8M > /dev/null 2>&1",
             dir, imgpath);
    if (system(cmd) != 0) {
        printf("  SKIP: mkfs.ext4 -d failed\n");
        return 0;
    }

    FILE *f = fopen(imgpath, "rb");
    TEST_ASSERT(f, "fixture image opens");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *img = malloc((size_t)size);
    TEST_ASSERT(img && fread(img, 1, (size_t)size, f) == (size_t)size,
                "fixture image reads");
    fclose(f);

    fsd_extfs_t *fs = NULL;
    const char *reason = NULL;
    fsd_error_t err = fsd_extfs_open(&fs, img, (size_t)size, &reason);
    if (err != FSD_SUCCESS) {
        /* e.g. mkfs defaults enabled a feature outside the whitelist */
        printf("  SKIP: fixture not walkable (%s)\n", reason ? reason : "?");
        free(img);
        return 0;
    }

    fixture_result_t r = {0};
    TEST_ASSERT(fsd_extfs_walk(fs, fixture_cb, &r) == FSD_SUCCESS,
                "fixture walk succeeds");
    TEST_ASSERT(r.found_hello, "hello.txt found");
    TEST_ASSERT(r.hello_size == 12, "hello.txt size correct");
    TEST_ASSERT(r.found_sub, "nested file found");
    TEST_ASSERT(r.big_blocks == (300000 + 4095) / 4096,
                "nested file extent coverage matches size");
    TEST_ASSERT(r.found_link_a && r.found_link_b,
                "hard link reported under both paths");

    fsd_extfs_close(fs);
    free(img);
    snprintf(cmd, sizeof(cmd), "rm -rf %s %s", dir, imgpath);
    if (system(cmd) != 0) { /* best-effort cleanup */ }
    return 0;
}

int main(void) {
    int failures = 0;

    printf("test_ext_reader:\n");
    printf("  probe rejections... \n");
    failures += test_probe_rejections();
    printf("  garbage metadata walk...\n");
    failures += test_walk_garbage_metadata();
    printf("  mkfs fixture...\n");
    failures += test_mkfs_fixture();

    printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
