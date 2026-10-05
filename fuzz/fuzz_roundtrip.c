/*
 * fuzz_roundtrip.c - libFuzzer harness: diff then patch must reproduce dest.
 *
 * The fuzz input describes a source image and an edit script that derives
 * the destination from it. The harness diffs the pair with fuzzed options,
 * applies the resulting patch, and aborts unless the output equals dest.
 * This catches any disagreement between the encoder and the decoder.
 *
 * Input layout:
 *   byte 0      block_size_log2 = 5 + (b % 8)                  (32..4096)
 *   byte 1      option flags: set bits disable bit0 identity, bit1 relocation,
 *               bit2 partial, bit3 fsmap; bit4 sets force_scalar
 *   byte 2      partial_threshold = b / 255
 *   byte 3      search_radius = (int8_t)b      (<= 0 selects the default),
 *               (with partial on, dest_blocks is reduced to fit FZ_PARTIAL_BUDGET)
 *   bytes 4-5   source length in units of block_size/32 bytes (LE, mod
 *               FZ_MAX_SRC; 0 means one block), so 32-byte-block lengths are
 *               byte-exact and lengths may or may not be block-aligned
 *   byte 6      bit0: the first FZ_MAX_INPUT_SRC source bytes come from the
 *               input; the rest (or all, if clear) is a pattern seeded by
 *               byte 7
 *   byte 7      pattern seed
 *   byte 8      dest block count (1 + b % FZ_MAX_DEST_BLOCKS)
 *   ...         [source bytes,] then edit-script ops until dest is full;
 *               any remainder is copied from source at the same offset.
 *
 * Inputs are mapped onto what the differ accepts, so any diff failure is a
 * finding: images are never empty (the differ rejects empty files), and
 * relocation is only enabled together with identity (relocation without
 * identity is rejected as FSD_ERR_INVALID_ARG by design).
 */

#include "fuzz_common.h"

#include <fsdiff/fsdiff.h>

#define FZ_MAX_SRC          (1u << 20)
#define FZ_MAX_INPUT_SRC    4096
#define FZ_MAX_DEST_BLOCKS  128

/* The differ's partial search is a brute-force sweep: worst case about
 * dest_blocks x (2 x radius + 1) x block_size^2 byte comparisons. A fuzzed
 * radius of 126 at 2 KiB blocks took 35-55 s per input without
 * instrumentation, and even the default radius at 4 KiB blocks took 9 s for
 * 126 blocks. When the partial stage is on, dest_blocks is reduced so the
 * worst case stays within this budget (~0.3 s uninstrumented). */
#define FZ_PARTIAL_BUDGET   (1ull << 30)
#define FZ_DEFAULT_RADIUS   8   /* stage_partial.c DEFAULT_SEARCH_RADIUS_BLOCKS */

static fz_file_t src_file, dest_file, patch_file, out_file;

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    fz_file_init(&src_file, "fz_source");
    fz_file_init(&dest_file, "fz_dest");
    fz_file_init(&patch_file, "fz_patch");
    fz_file_init(&out_file, "fz_output");
    return 0;
}

/* Copy len bytes of src starting at a (possibly out-of-range) offset; bytes
 * outside the source read as zero. */
static void copy_src(uint8_t *dst, const uint8_t *src, size_t src_len,
                     int64_t off, size_t len) {
    for (size_t i = 0; i < len; i++) {
        int64_t p = off + (int64_t)i;
        dst[i] = (p >= 0 && (uint64_t)p < src_len) ? src[p] : 0;
    }
}

static void build_dest(fz_reader_t *r, uint8_t *dest, size_t dest_len,
                       const uint8_t *src, size_t src_len, size_t block_size) {
    /* Offsets are 16-bit input values scaled like the source length, so
     * they can reach anywhere in images up to FZ_MAX_SRC. */
    size_t unit = block_size / 32;
    size_t pos = 0;
    while (pos < dest_len && r->left > 0) {
        uint8_t op = fz_u8(r);
        size_t len = (size_t)(fz_u16(r) % (block_size * 4)) + 1;
        if (len > dest_len - pos) len = dest_len - pos;
        uint8_t *d = dest + pos;

        switch (op % 8) {
        case 0: /* same-offset copy */
            copy_src(d, src, src_len, (int64_t)pos, len);
            break;
        case 1: /* copy from an arbitrary source byte offset */
            copy_src(d, src, src_len, fz_u16(r) * unit % src_len, len);
            break;
        case 2:
            memset(d, 0x00, len);
            break;
        case 3:
            memset(d, 0xFF, len);
            break;
        case 4: /* literal bytes from the input */
            for (size_t i = 0; i < len; i++) d[i] = fz_u8(r);
            break;
        case 5: { /* nearby copy with a handful of byte edits */
            copy_src(d, src, src_len, (int64_t)pos + (int8_t)fz_u8(r), len);
            int edits = fz_u8(r) % 8;
            for (int i = 0; i < edits; i++) {
                size_t at = fz_u16(r) % len;
                d[at] = fz_u8(r);
            }
            break;
        }
        case 6: /* repeat earlier dest content */
            if (pos > 0) {
                size_t from = fz_u16(r) * unit % pos;
                for (size_t i = 0; i < len; i++) d[i] = dest[from + i];
            } else {
                memset(d, 0, len);
            }
            break;
        case 7: /* copy shifted by a block-scale displacement */
            copy_src(d, src, src_len,
                     (int64_t)pos + (int16_t)fz_u16(r) * (int64_t)block_size / 16,
                     len);
            break;
        }
        pos += len;
    }
    if (pos < dest_len) {
        copy_src(dest + pos, src, src_len, (int64_t)pos, dest_len - pos);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 9) return -1;
    fz_reader_t r = { data, size };

    uint8_t log2 = 5 + (fz_u8(&r) % 8);
    size_t block_size = (size_t)1 << log2;

    fsd_diff_options_t dopts;
    fsd_diff_options_init(&dopts);
    uint8_t flags = fz_u8(&r);
    dopts.block_size_log2   = log2;
    dopts.enable_identity   = !(flags & 0x01);
    dopts.enable_relocation = !(flags & 0x02) && dopts.enable_identity;
    dopts.enable_partial    = !(flags & 0x04);
    dopts.enable_fsmap      = !(flags & 0x08);
    dopts.force_scalar      = (flags & 0x10) != 0;
    dopts.partial_threshold = fz_u8(&r) / 255.0f;
    dopts.search_radius     = (int8_t)fz_u8(&r);

    size_t src_len = fz_u16(&r) * (block_size / 32) % FZ_MAX_SRC;
    if (src_len == 0) src_len = block_size;
    uint8_t src_mode = fz_u8(&r);
    uint8_t seed = fz_u8(&r);
    size_t dest_blocks = 1 + fz_u8(&r) % FZ_MAX_DEST_BLOCKS;
    if (dopts.enable_partial) {
        uint64_t radius = dopts.search_radius > 0 ? (uint64_t)dopts.search_radius
                                                  : FZ_DEFAULT_RADIUS;
        uint64_t per_block = (2 * radius + 1) * block_size * block_size;
        uint64_t max_blocks = FZ_PARTIAL_BUDGET / per_block;
        if (max_blocks < 1) max_blocks = 1;
        if (dest_blocks > max_blocks) dest_blocks = (size_t)max_blocks;
    }
    size_t dest_len = dest_blocks * block_size;

    uint8_t *src = malloc(src_len);
    uint8_t *dest = malloc(dest_len ? dest_len : 1);
    if (!src || !dest) fz_die("malloc");

    fz_fill_pattern(src, src_len, seed);
    if (src_mode & 1) {
        size_t n = src_len < FZ_MAX_INPUT_SRC ? src_len : FZ_MAX_INPUT_SRC;
        for (size_t i = 0; i < n; i++) src[i] = fz_u8(&r);
    }
    build_dest(&r, dest, dest_len, src, src_len, block_size);

    fz_file_set(&src_file, src, src_len);
    fz_file_set(&dest_file, dest, dest_len);
    fz_file_set(&patch_file, NULL, 0);
    fz_file_set(&out_file, NULL, 0);

    fsd_diff_ctx_t *dctx = NULL;
    if (fsd_diff_create(&dctx, &dopts) != FSD_SUCCESS) fz_die("fsd_diff_create");
    fsd_error_t err = fsd_diff_files(dctx, src_file.path, dest_file.path, patch_file.path);
    fsd_diff_destroy(dctx);
    if (err != FSD_SUCCESS) {
        fprintf(stderr, "fuzz_roundtrip: diff failed: %s (src %zu bytes, "
                "dest %zu bytes, block %zu)\n",
                fsd_strerror(err), src_len, dest_len, block_size);
        abort();
    }

    fsd_patch_options_t popts;
    fsd_patch_options_init(&popts);
    fsd_patch_ctx_t *pctx = NULL;
    if (fsd_patch_create(&pctx, &popts) != FSD_SUCCESS) fz_die("fsd_patch_create");
    err = fsd_patch_apply(pctx, src_file.path, patch_file.path, out_file.path);
    fsd_patch_destroy(pctx);
    if (err != FSD_SUCCESS) {
        fprintf(stderr, "fuzz_roundtrip: patch of a fresh diff failed: %s\n",
                fsd_strerror(err));
        abort();
    }

    size_t out_len;
    uint8_t *out = fz_file_read(&out_file, &out_len);
    if (out_len != dest_len || memcmp(out, dest, dest_len) != 0) {
        size_t i = 0;
        while (i < out_len && i < dest_len && out[i] == dest[i]) i++;
        fprintf(stderr, "fuzz_roundtrip: output mismatch: %zu bytes vs dest %zu, "
                "first difference at byte %zu (block %zu)\n",
                out_len, dest_len, i, i / block_size);
        abort();
    }

    free(out);
    free(src);
    free(dest);
    return 0;
}
