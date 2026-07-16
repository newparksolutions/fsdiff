/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file ext_reader.c
 * @brief Minimal read-only ext2/3/4 walker (see ext_reader.h)
 *
 * Decoding style: every on-disk field is read through le16/le32/le64 helpers
 * at an explicit byte offset (constants below, cross-checked against the
 * public ext4 layout documentation and e2fsprogs' ext2_fs.h). This avoids
 * struct packing/alignment concerns entirely and keeps the reader portable.
 *
 * Hardening rules (a diff tool gets handed arbitrary images):
 *  - every block number is bounds-checked against the image before use;
 *  - directory recursion uses an explicit stack with a depth cap and a
 *    visited-inode bitmap, so cycles and deep trees terminate;
 *  - extent trees are depth-capped and every interior node re-validated;
 *  - per-file and global caps bound all allocations;
 *  - any structural inconsistency skips the file/directory, never crashes.
 */

#include "ext_reader.h"
#include <stdlib.h>
#include <string.h>

/*===========================================================================
 * Little-endian loads (alignment-safe, endian-independent)
 *===========================================================================*/

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
/*===========================================================================
 * On-disk layout constants
 *===========================================================================*/

#define EXT_SB_OFFSET          1024
#define EXT_SB_MIN_SIZE        264      /* through s_desc_size + padding */

/* Superblock field offsets (relative to superblock start) */
#define SB_INODES_COUNT        0x00
#define SB_BLOCKS_COUNT_LO     0x04
#define SB_FIRST_DATA_BLOCK    0x14
#define SB_LOG_BLOCK_SIZE      0x18
#define SB_BLOCKS_PER_GROUP    0x20
#define SB_INODES_PER_GROUP    0x28
#define SB_MAGIC               0x38
#define SB_STATE               0x3A
#define SB_REV_LEVEL           0x4C
#define SB_INODE_SIZE          0x58
#define SB_FEATURE_COMPAT      0x5C
#define SB_FEATURE_INCOMPAT    0x60
#define SB_FEATURE_RO_COMPAT   0x64
#define SB_DESC_SIZE           0xFE
#define SB_BLOCKS_COUNT_HI     0x150

#define EXT_SUPER_MAGIC        0xEF53
#define EXT_STATE_CLEAN        0x0001

/* INCOMPAT feature flags */
#define INCOMPAT_FILETYPE      0x00002
#define INCOMPAT_RECOVER       0x00004
#define INCOMPAT_EXTENTS       0x00040
#define INCOMPAT_64BIT         0x00080
#define INCOMPAT_FLEX_BG       0x00200
#define INCOMPAT_CSUM_SEED     0x02000
#define INCOMPAT_INLINE_DATA   0x08000
#define INCOMPAT_ALLOWED (INCOMPAT_FILETYPE | INCOMPAT_EXTENTS | \
                          INCOMPAT_64BIT | INCOMPAT_FLEX_BG | \
                          INCOMPAT_CSUM_SEED | INCOMPAT_INLINE_DATA)

/* Group descriptor field offsets */
#define GD_INODE_TABLE_LO      0x08
#define GD_INODE_TABLE_HI      0x28
#define GD_SIZE_32             32

/* Inode field offsets */
#define INO_MODE               0x00
#define INO_SIZE_LO            0x04
#define INO_FLAGS              0x20
#define INO_BLOCK              0x28    /* 60-byte i_block area */
#define INO_SIZE_HIGH          0x6C
#define INO_BLOCK_LEN          60
#define INO_MIN_SIZE           128

#define MODE_FMT_MASK          0xF000
#define MODE_REGULAR           0x8000
#define MODE_DIRECTORY         0x4000

#define IFLAG_EXTENTS          0x00080000u
#define IFLAG_INLINE_DATA      0x10000000u

/* Extent tree */
#define EH_MAGIC_OFF           0
#define EH_ENTRIES_OFF         2
#define EH_MAX_OFF             4
#define EH_DEPTH_OFF           6
#define EH_SIZE                12
#define EXT_ENTRY_SIZE         12
#define EXTENT_HEADER_MAGIC    0xF30A
#define EXTENT_MAX_DEPTH       5
#define EXT_INIT_MAX_LEN       32768   /* ee_len above this means unwritten */

/* Leaf extent entry offsets */
#define EE_BLOCK_OFF           0
#define EE_LEN_OFF             4
#define EE_START_HI_OFF        6
#define EE_START_LO_OFF        8
/* Index entry offsets */
#define EI_LEAF_LO_OFF         4
#define EI_LEAF_HI_OFF         8

/* Directory entries */
#define DE_INODE_OFF           0
#define DE_REC_LEN_OFF         4
#define DE_NAME_LEN_OFF        6
#define DE_FILE_TYPE_OFF       7
#define DE_NAME_OFF            8
#define DE_FT_REGULAR          1
#define DE_FT_DIRECTORY        2

#define EXT_ROOT_INODE         2

/* Hardening caps */
#define MAX_PATH_LEN           4096
#define MAX_DIR_DEPTH          128
#define MAX_EXTENTS_PER_FILE   65536
#define MAX_FILE_BLOCKS        (1ull << 32)   /* block-map iteration cap */

/*===========================================================================
 * Handle
 *===========================================================================*/

struct fsd_extfs {
    const uint8_t *image;
    size_t image_size;

    uint32_t block_size;
    uint64_t block_count;
    uint32_t inodes_count;
    uint32_t inodes_per_group;
    uint32_t blocks_per_group;
    uint32_t inode_size;
    uint32_t desc_size;
    uint32_t first_data_block;
    uint64_t gdt_start;         /* byte offset of group descriptor table */
    uint32_t group_count;
};

/* Pointer to filesystem block b, or NULL if it lies outside the image. */
static const uint8_t *fs_block(const fsd_extfs_t *fs, uint64_t b) {
    if (b == 0 || b >= fs->block_count) {
        return NULL;
    }
    uint64_t off = b * fs->block_size;
    if (off + fs->block_size > fs->image_size) {
        return NULL;
    }
    return fs->image + off;
}

/*===========================================================================
 * Probe / open
 *===========================================================================*/

fsd_error_t fsd_extfs_open(fsd_extfs_t **fs_out,
                           const uint8_t *image, size_t image_size,
                           const char **reason_out) {
    const char *reason = "";
    if (reason_out) *reason_out = "";
    if (!fs_out || !image) {
        return FSD_ERR_INVALID_ARG;
    }
    if (image_size < EXT_SB_OFFSET + 1024) {
        if (reason_out) *reason_out = "image too small for a superblock";
        return FSD_ERR_BAD_MAGIC;
    }

    const uint8_t *sb = image + EXT_SB_OFFSET;
    if (rd16(sb + SB_MAGIC) != EXT_SUPER_MAGIC) {
        if (reason_out) *reason_out = "no ext superblock magic";
        return FSD_ERR_BAD_MAGIC;
    }

    uint32_t log_bs = rd32(sb + SB_LOG_BLOCK_SIZE);
    uint32_t incompat = rd32(sb + SB_FEATURE_INCOMPAT);
    uint32_t rev = rd32(sb + SB_REV_LEVEL);
    fsd_error_t err = FSD_ERR_BAD_MAGIC;

    if (log_bs > 2) {
        reason = "unsupported block size";
        goto fail;
    }
    if (incompat & ~(uint32_t)INCOMPAT_ALLOWED) {
        reason = "unsupported incompat features";
        goto fail;
    }
    if (incompat & INCOMPAT_RECOVER) {   /* covered above, kept for clarity */
        reason = "journal needs recovery";
        goto fail;
    }
    if (!(rd16(sb + SB_STATE) & EXT_STATE_CLEAN)) {
        reason = "filesystem not cleanly unmounted";
        goto fail;
    }

    fsd_extfs_t *fs = calloc(1, sizeof(*fs));
    if (!fs) {
        return FSD_ERR_OUT_OF_MEMORY;
    }
    fs->image = image;
    fs->image_size = image_size;
    fs->block_size = 1024u << log_bs;
    fs->block_count = rd32(sb + SB_BLOCKS_COUNT_LO);
    if (incompat & INCOMPAT_64BIT) {
        fs->block_count |= (uint64_t)rd32(sb + SB_BLOCKS_COUNT_HI) << 32;
    }
    fs->inodes_count = rd32(sb + SB_INODES_COUNT);
    fs->inodes_per_group = rd32(sb + SB_INODES_PER_GROUP);
    fs->blocks_per_group = rd32(sb + SB_BLOCKS_PER_GROUP);
    fs->first_data_block = rd32(sb + SB_FIRST_DATA_BLOCK);
    fs->inode_size = (rev >= 1) ? rd16(sb + SB_INODE_SIZE) : INO_MIN_SIZE;
    fs->desc_size = (incompat & INCOMPAT_64BIT) ? rd16(sb + SB_DESC_SIZE)
                                                : GD_SIZE_32;

    err = FSD_ERR_CORRUPT_DATA;
    if (fs->inodes_per_group == 0 || fs->blocks_per_group == 0 ||
        fs->inodes_count == 0 || fs->block_count == 0) {
        reason = "zero geometry field";
        goto fail_free;
    }
    if (fs->inode_size < INO_MIN_SIZE || fs->inode_size > fs->block_size ||
        (fs->inode_size & (fs->inode_size - 1)) != 0) {
        reason = "bad inode size";
        goto fail_free;
    }
    if (fs->desc_size < GD_SIZE_32 || fs->desc_size > fs->block_size) {
        reason = "bad descriptor size";
        goto fail_free;
    }
    if (fs->first_data_block != (fs->block_size == 1024 ? 1u : 0u)) {
        reason = "bad first data block";
        goto fail_free;
    }
    /* The filesystem must fit inside the image (a truncated image would
     * defeat the per-block bounds checks for metadata near the end). */
    if (fs->block_count > fs->image_size / fs->block_size) {
        reason = "filesystem larger than image";
        goto fail_free;
    }
    fs->group_count = (uint32_t)((fs->block_count - fs->first_data_block +
                                  fs->blocks_per_group - 1) /
                                 fs->blocks_per_group);
    if ((uint64_t)fs->group_count * fs->inodes_per_group < fs->inodes_count) {
        reason = "inode count exceeds groups";
        goto fail_free;
    }
    fs->gdt_start = (uint64_t)(fs->first_data_block + 1) * fs->block_size;
    if (fs->gdt_start + (uint64_t)fs->group_count * fs->desc_size >
        fs->image_size) {
        reason = "descriptor table out of range";
        goto fail_free;
    }

    *fs_out = fs;
    return FSD_SUCCESS;

fail_free:
    free(fs);
fail:
    if (reason_out) *reason_out = reason;
    return err;
}

void fsd_extfs_close(fsd_extfs_t *fs) {
    free(fs);
}

uint32_t fsd_extfs_block_size(const fsd_extfs_t *fs) {
    return fs ? fs->block_size : 0;
}

uint64_t fsd_extfs_block_count(const fsd_extfs_t *fs) {
    return fs ? fs->block_count : 0;
}

/*===========================================================================
 * Inode access
 *===========================================================================*/

/* Raw inode bytes for inode number `ino` (1-based), or NULL. */
static const uint8_t *inode_ptr(const fsd_extfs_t *fs, uint64_t ino) {
    if (ino == 0 || ino > fs->inodes_count) {
        return NULL;
    }
    uint64_t index = ino - 1;
    uint64_t group = index / fs->inodes_per_group;
    uint64_t within = index % fs->inodes_per_group;

    const uint8_t *gd = fs->image + fs->gdt_start + group * fs->desc_size;
    uint64_t table = rd32(gd + GD_INODE_TABLE_LO);
    if (fs->desc_size >= GD_INODE_TABLE_HI + 4) {
        table |= (uint64_t)rd32(gd + GD_INODE_TABLE_HI) << 32;
    }

    uint64_t byte = table * fs->block_size + within * fs->inode_size;
    if (table == 0 || table >= fs->block_count ||
        byte + fs->inode_size > fs->image_size) {
        return NULL;
    }
    return fs->image + byte;
}

static uint64_t inode_file_size(const uint8_t *ino, uint16_t mode) {
    uint64_t size = rd32(ino + INO_SIZE_LO);
    if ((mode & MODE_FMT_MASK) == MODE_REGULAR) {
        size |= (uint64_t)rd32(ino + INO_SIZE_HIGH) << 32;
    }
    return size;
}

/*===========================================================================
 * Extent collection (extent trees and legacy block maps)
 *
 * Both paths append to a caller-supplied growable array, coalescing
 * adjacent mappings, and skip holes and unwritten extents.
 *===========================================================================*/

typedef struct {
    fsd_ext_extent_t *v;
    size_t n;
    size_t cap;
} extent_list_t;

static int extent_append(extent_list_t *list, uint64_t logical,
                         uint64_t physical, uint64_t count) {
    if (count == 0) {
        return 0;
    }
    if (list->n > 0) {
        fsd_ext_extent_t *last = &list->v[list->n - 1];
        if (last->logical + last->count == logical &&
            last->physical + last->count == physical) {
            last->count += count;
            return 0;
        }
    }
    if (list->n >= MAX_EXTENTS_PER_FILE) {
        return -1;
    }
    if (list->n == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 16;
        fsd_ext_extent_t *v = realloc(list->v, cap * sizeof(*v));
        if (!v) {
            return -1;
        }
        list->v = v;
        list->cap = cap;
    }
    list->v[list->n].logical = logical;
    list->v[list->n].physical = physical;
    list->v[list->n].count = count;
    list->n++;
    return 0;
}

/* Walk one extent-tree node (12-byte header + entries). `space` is the
 * number of bytes available for the node (60 for the in-inode root, block
 * size for interior blocks). Returns 0 on success, -1 to skip the file. */
static int extents_from_node(const fsd_extfs_t *fs, const uint8_t *node,
                             size_t space, int expect_depth,
                             extent_list_t *out) {
    if (space < EH_SIZE || rd16(node + EH_MAGIC_OFF) != EXTENT_HEADER_MAGIC) {
        return -1;
    }
    uint16_t entries = rd16(node + EH_ENTRIES_OFF);
    uint16_t depth = rd16(node + EH_DEPTH_OFF);
    if (depth > EXTENT_MAX_DEPTH ||
        (expect_depth >= 0 && depth != (uint16_t)expect_depth)) {
        return -1;
    }
    if ((size_t)EH_SIZE + (size_t)entries * EXT_ENTRY_SIZE > space) {
        return -1;
    }

    const uint8_t *entry = node + EH_SIZE;
    for (uint16_t i = 0; i < entries; i++, entry += EXT_ENTRY_SIZE) {
        if (depth == 0) {
            uint32_t len = rd16(entry + EE_LEN_OFF);
            if (len == 0) {
                continue;
            }
            if (len > EXT_INIT_MAX_LEN) {
                continue;    /* unwritten (fallocated): content undefined */
            }
            uint64_t phys = rd32(entry + EE_START_LO_OFF) |
                            ((uint64_t)rd16(entry + EE_START_HI_OFF) << 32);
            if (phys == 0 || phys + len > fs->block_count) {
                return -1;
            }
            if (extent_append(out, rd32(entry + EE_BLOCK_OFF), phys, len) != 0) {
                return -1;
            }
        } else {
            uint64_t child = rd32(entry + EI_LEAF_LO_OFF) |
                             ((uint64_t)rd16(entry + EI_LEAF_HI_OFF) << 32);
            const uint8_t *cb = fs_block(fs, child);
            if (!cb) {
                return -1;
            }
            if (extents_from_node(fs, cb, fs->block_size, depth - 1, out) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

/* Legacy (ext2/3) block map: 12 direct pointers, then 1/2/3-level indirect
 * blocks of 32-bit block numbers. `*logical` advances as entries are
 * consumed; `remaining` bounds the number of file blocks still expected. */
static int blockmap_indirect(const fsd_extfs_t *fs, uint64_t map_block,
                             int level, uint64_t *logical, uint64_t remaining,
                             extent_list_t *out) {
    if (remaining == 0) {
        return 0;
    }
    if (map_block == 0) {           /* hole covering this whole subtree */
        uint64_t span = fs->block_size / 4;
        for (int l = 1; l < level; l++) {
            span *= fs->block_size / 4;
        }
        *logical += level == 0 ? 1 : span;
        return 0;
    }
    if (level == 0) {
        if (map_block >= fs->block_count) {
            return -1;
        }
        int r = extent_append(out, *logical, map_block, 1);
        (*logical)++;
        return r;
    }
    const uint8_t *blk = fs_block(fs, map_block);
    if (!blk) {
        return -1;
    }
    uint32_t per = fs->block_size / 4;
    for (uint32_t i = 0; i < per && remaining > *logical; i++) {
        if (blockmap_indirect(fs, rd32(blk + i * 4), level - 1,
                              logical, remaining, out) != 0) {
            return -1;
        }
    }
    return 0;
}

static int blockmap_collect(const fsd_extfs_t *fs, const uint8_t *iblock,
                            uint64_t file_blocks, extent_list_t *out) {
    if (file_blocks > MAX_FILE_BLOCKS) {
        return -1;
    }
    uint64_t logical = 0;
    for (int i = 0; i < 12 && logical < file_blocks; i++) {
        if (blockmap_indirect(fs, rd32(iblock + i * 4), 0,
                              &logical, file_blocks, out) != 0) {
            return -1;
        }
    }
    for (int level = 1; level <= 3 && logical < file_blocks; level++) {
        if (blockmap_indirect(fs, rd32(iblock + (11 + level) * 4), level,
                              &logical, file_blocks, out) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Collect all data mappings for an inode. Returns 0 and fills `out`
 * (possibly empty: inline-data or fully sparse), or -1 to skip the file. */
static int inode_extents(const fsd_extfs_t *fs, const uint8_t *ino,
                         extent_list_t *out) {
    uint32_t flags = rd32(ino + INO_FLAGS);
    out->n = 0;
    if (flags & IFLAG_INLINE_DATA) {
        return 0;                    /* data lives in the inode; no blocks */
    }
    if (flags & IFLAG_EXTENTS) {
        return extents_from_node(fs, ino + INO_BLOCK, INO_BLOCK_LEN, -1, out);
    }
    uint16_t mode = rd16(ino + INO_MODE);
    uint64_t size = inode_file_size(ino, mode);
    uint64_t file_blocks = (size + fs->block_size - 1) / fs->block_size;
    return blockmap_collect(fs, ino + INO_BLOCK, file_blocks, out);
}

/*===========================================================================
 * Directory walk
 *===========================================================================*/

typedef struct {
    uint64_t inode;
    char *path;                      /* malloc'd; "" for the root */
} dir_entry_t;

typedef struct {
    dir_entry_t *v;
    size_t n, cap;
} dir_stack_t;

static int dir_push(dir_stack_t *st, uint64_t ino, const char *path) {
    if (st->n == st->cap) {
        size_t cap = st->cap ? st->cap * 2 : 64;
        dir_entry_t *v = realloc(st->v, cap * sizeof(*v));
        if (!v) {
            return -1;
        }
        st->v = v;
        st->cap = cap;
    }
    char *dup = malloc(strlen(path) + 1);
    if (!dup) {
        return -1;
    }
    strcpy(dup, path);
    st->v[st->n].inode = ino;
    st->v[st->n].path = dup;
    st->n++;
    return 0;
}

/* Depth = number of '/' in the stored path; cheap proxy good enough for the
 * cap (paths are bounded anyway). */
static int path_depth(const char *p) {
    int d = 0;
    for (; *p; p++) {
        d += (*p == '/');
    }
    return d;
}

fsd_error_t fsd_extfs_walk(fsd_extfs_t *fs, fsd_extfs_file_cb cb, void *user) {
    if (!fs || !cb) {
        return FSD_ERR_INVALID_ARG;
    }

    fsd_error_t err = FSD_ERR_OUT_OF_MEMORY;
    uint8_t *visited = calloc((size_t)fs->inodes_count / 8 + 1, 1);
    char *path_buf = malloc(MAX_PATH_LEN);
    dir_stack_t stack = {0};
    extent_list_t dir_ext = {0};
    extent_list_t file_ext = {0};
    if (!visited || !path_buf) {
        goto out;
    }
    if (dir_push(&stack, EXT_ROOT_INODE, "") != 0) {
        goto out;
    }
    visited[EXT_ROOT_INODE / 8] |= (uint8_t)(1u << (EXT_ROOT_INODE % 8));

    int stopped = 0;
    while (stack.n > 0 && !stopped) {
        dir_entry_t dir = stack.v[--stack.n];

        const uint8_t *dino = inode_ptr(fs, dir.inode);
        if (!dino || (rd16(dino + INO_MODE) & MODE_FMT_MASK) != MODE_DIRECTORY ||
            inode_extents(fs, dino, &dir_ext) != 0) {
            free(dir.path);
            continue;
        }

        for (size_t e = 0; e < dir_ext.n && !stopped; e++) {
            /* Snapshot: inode_extents reuses dir_ext, so a subdirectory's
             * collection during this loop must not be able to alias it —
             * dirs are pushed, not descended, so dir_ext stays ours. */
            fsd_ext_extent_t ext = dir_ext.v[e];
            for (uint64_t b = 0; b < ext.count && !stopped; b++) {
                const uint8_t *blk = fs_block(fs, ext.physical + b);
                if (!blk) {
                    continue;
                }
                uint32_t pos = 0;
                while (pos + DE_NAME_OFF <= fs->block_size) {
                    const uint8_t *de = blk + pos;
                    uint32_t rec_len = rd16(de + DE_REC_LEN_OFF);
                    if (rec_len < DE_NAME_OFF || (rec_len & 3) ||
                        pos + rec_len > fs->block_size) {
                        break;      /* corrupt or htree interior block */
                    }
                    uint32_t ino = rd32(de + DE_INODE_OFF);
                    uint8_t name_len = de[DE_NAME_LEN_OFF];
                    uint8_t ftype = de[DE_FILE_TYPE_OFF];
                    if (ino != 0 && name_len > 0 &&
                        DE_NAME_OFF + (uint32_t)name_len <= rec_len) {
                        const char *name = (const char *)(de + DE_NAME_OFF);
                        int is_dot = (name_len == 1 && name[0] == '.') ||
                                     (name_len == 2 && name[0] == '.' &&
                                      name[1] == '.');
                        size_t plen = strlen(dir.path);
                        if (!is_dot && !memchr(name, '/', name_len) &&
                            !memchr(name, '\0', name_len) &&
                            plen + 1 + name_len < MAX_PATH_LEN) {
                            memcpy(path_buf, dir.path, plen);
                            path_buf[plen] = '/';
                            memcpy(path_buf + plen + 1, name, name_len);
                            path_buf[plen + 1 + name_len] = '\0';

                            if (ftype == DE_FT_DIRECTORY) {
                                if (ino <= fs->inodes_count &&
                                    !(visited[ino / 8] & (1u << (ino % 8))) &&
                                    path_depth(path_buf) <= MAX_DIR_DEPTH) {
                                    visited[ino / 8] |=
                                        (uint8_t)(1u << (ino % 8));
                                    if (dir_push(&stack, ino, path_buf) != 0) {
                                        goto out;
                                    }
                                }
                            } else if (ftype == DE_FT_REGULAR) {
                                const uint8_t *fi = inode_ptr(fs, ino);
                                if (fi &&
                                    (rd16(fi + INO_MODE) & MODE_FMT_MASK) ==
                                        MODE_REGULAR &&
                                    inode_extents(fs, fi, &file_ext) == 0) {
                                    uint64_t fsize = inode_file_size(
                                        fi, rd16(fi + INO_MODE));
                                    if (cb(user, path_buf, ino, fsize,
                                           file_ext.v, file_ext.n) != 0) {
                                        stopped = 1;
                                    }
                                }
                            }
                            /* other types (symlink, device, ...) skipped */
                        }
                    }
                    pos += rec_len;
                }
            }
        }
        free(dir.path);
    }
    err = FSD_SUCCESS;

out:
    while (stack.n > 0) {
        free(stack.v[--stack.n].path);
    }
    free(stack.v);
    free(dir_ext.v);
    free(file_ext.v);
    free(visited);
    free(path_buf);
    return err;
}
