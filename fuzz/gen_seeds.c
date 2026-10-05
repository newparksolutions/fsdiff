/*
 * gen_seeds.c - write starting corpora for fuzz_patch and fuzz_roundtrip.
 *
 * Usage: gen_seeds <outdir>
 *   <outdir>/patch/      real patches produced by fsd_diff_files against
 *                        fuzz_patch's 64 KiB and 1 MiB sources, in both
 *                        input modes
 *   <outdir>/roundtrip/  edit scripts exercising every fuzz_roundtrip op
 *
 * Built against the full (diff-capable) library; not a fuzz target itself.
 */

#include "fuzz_common.h"

#include <errno.h>
#include <sys/stat.h>

#include <fsdiff/fsdiff.h>

#define SRC_LEN 65536          /* fuzz_patch source selector 3 */
#define BIG_SRC_LEN (1u << 20)  /* fuzz_patch source selector 5 */

static char outdir[512];
static int seed_count;

static void write_seed(const char *sub, const char *name,
                       const uint8_t *a, size_t alen,
                       const uint8_t *b, size_t blen) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s/%s", outdir, sub, name);
    FILE *f = fopen(path, "wb");
    if (!f) fz_die(path);
    if ((alen && fwrite(a, 1, alen, f) != alen) ||
        (blen && fwrite(b, 1, blen, f) != blen)) {
        fz_die(path);
    }
    fclose(f);
    seed_count++;
}

static void make_dir(const char *sub) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", outdir, sub);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) fz_die(path);
}

/* Diff src -> dest at the given block size and emit the patch as a raw-mode
 * and a structured-mode fuzz_patch seed. src must be fuzz_patch's source
 * `selector`, src_len bytes long. */
static void patch_seed(const char *name, uint8_t selector,
                       const uint8_t *src, size_t src_len,
                       const uint8_t *dest, size_t dest_len, uint8_t log2) {
    static fz_file_t s, d, p;
    static int init;
    if (!init) {
        fz_file_init(&s, "gen_src");
        fz_file_init(&d, "gen_dest");
        fz_file_init(&p, "gen_patch");
        init = 1;
    }
    fz_file_set(&s, src, src_len);
    fz_file_set(&d, dest, dest_len);
    fz_file_set(&p, NULL, 0);

    fsd_diff_options_t opts;
    fsd_diff_options_init(&opts);
    opts.block_size_log2 = log2;
    fsd_diff_ctx_t *ctx = NULL;
    if (fsd_diff_create(&ctx, &opts) != FSD_SUCCESS) fz_die("fsd_diff_create");
    fsd_error_t err = fsd_diff_files(ctx, s.path, d.path, p.path);
    fsd_diff_destroy(ctx);
    if (err != FSD_SUCCESS) {
        fprintf(stderr, "gen_seeds: diff %s failed: %s\n", name, fsd_strerror(err));
        exit(1);
    }

    size_t len;
    uint8_t *patch = fz_file_read(&p, &len);
    char fname[128];

    uint8_t raw_ctl = (uint8_t)(selector << 1);
    snprintf(fname, sizeof(fname), "raw_%s_bs%u", name, 1u << log2);
    write_seed("patch", fname, &raw_ctl, 1, patch, len);

    /* Structured mode: control, log2 byte, dest_blocks, op_len, diff_len. */
    uint64_t dest_blocks = fz_get_u64_le(&patch[8]);
    uint64_t op_len = fz_get_u64_le(&patch[16]);
    uint64_t diff_len = fz_get_u64_le(&patch[24]);
    if (dest_blocks <= 0xFFFF && op_len <= 0xFFFF && diff_len <= 0xFFFF) {
        uint8_t hdr[8] = {
            (uint8_t)(selector << 1 | 1), (uint8_t)(log2 - 5),
            (uint8_t)dest_blocks, (uint8_t)(dest_blocks >> 8),
            (uint8_t)op_len, (uint8_t)(op_len >> 8),
            (uint8_t)diff_len, (uint8_t)(diff_len >> 8),
        };
        snprintf(fname, sizeof(fname), "struct_%s_bs%u", name, 1u << log2);
        write_seed("patch", fname, hdr, sizeof(hdr),
                   patch + FSD_HEADER_SIZE, len - FSD_HEADER_SIZE);
    }
    free(patch);
}

static void gen_patch_seeds(void) {
    static uint8_t src[SRC_LEN], dest[SRC_LEN], noise[SRC_LEN];
    fz_fill_pattern(src, SRC_LEN, FZ_SOURCE_SEED);
    fz_fill_pattern(noise, SRC_LEN, 0xABCDEF);

    for (uint8_t log2 = 5; log2 <= 12; log2++) {
        size_t bs = (size_t)1 << log2;
        size_t nblocks = SRC_LEN / bs - 1;  /* slack for the shifted copies */
        if (nblocks > 512) nblocks = 512;   /* keep seeds small */
        size_t len = nblocks * bs;

        /* identical prefix of the source */
        memcpy(dest, src, len);
        patch_seed("identity", 3, src, SRC_LEN, dest, len, log2);

        /* blocks reversed: relocations with both offset signs */
        for (size_t i = 0; i < nblocks; i++) {
            memcpy(dest + i * bs, src + (nblocks - 1 - i) * bs, bs);
        }
        patch_seed("relocate", 3, src, SRC_LEN, dest, len, log2);

        /* byte-shifted with scattered edits: partial matches */
        memcpy(dest, src + 13, len);
        for (size_t i = 0; i < len; i += 97) dest[i] ^= 0x5A;
        patch_seed("partial", 3, src, SRC_LEN, dest, len, log2);

        /* dense edits: partial matches that favour the dense diff format */
        memcpy(dest, src + 3, len);
        for (size_t i = 0; i < len; i += 2) dest[i] += 1;
        patch_seed("dense", 3, src, SRC_LEN, dest, len, log2);

        /* mixture: zero, 0xFF, literal, identity, shifted */
        for (size_t i = 0; i < nblocks; i++) {
            uint8_t *d = dest + i * bs;
            switch (i % 5) {
            case 0: memset(d, 0x00, bs); break;
            case 1: memset(d, 0xFF, bs); break;
            case 2: memcpy(d, noise + i * bs, bs); break;
            case 3: memcpy(d, src + i * bs, bs); break;
            case 4: memcpy(d, src + i * bs + 5, bs); d[bs / 2] ^= 1; break;
            }
        }
        patch_seed("mixed", 3, src, SRC_LEN, dest, len, log2);
    }
}

/* Large blocks against the 1 MiB source. Only content that diffs to a small
 * patch (identity, relocation, sparse partial edits, zero/0xFF), so the seeds
 * stay well inside fuzz_patch's -max_len. */
static void gen_big_patch_seeds(void) {
    uint8_t *src = malloc(BIG_SRC_LEN);
    uint8_t *dest = malloc(BIG_SRC_LEN);
    if (!src || !dest) fz_die("malloc");
    fz_fill_pattern(src, BIG_SRC_LEN, FZ_SOURCE_SEED);

    for (uint8_t log2 = 13; log2 <= 19; log2 += 3) {   /* 8 KiB, 64 KiB, 512 KiB */
        size_t bs = (size_t)1 << log2;
        size_t nblocks = BIG_SRC_LEN / bs - 1;
        size_t len = nblocks * bs;

        memcpy(dest, src, len);
        patch_seed("identity", 5, src, BIG_SRC_LEN, dest, len, log2);

        for (size_t i = 0; i < nblocks; i++) {
            memcpy(dest + i * bs, src + (nblocks - 1 - i) * bs, bs);
        }
        patch_seed("relocate", 5, src, BIG_SRC_LEN, dest, len, log2);

        memcpy(dest, src + 13, len);
        for (size_t i = 0; i < len; i += 4093) dest[i] ^= 0x5A;
        patch_seed("partial", 5, src, BIG_SRC_LEN, dest, len, log2);

        for (size_t i = 0; i < nblocks; i++) {
            uint8_t *d = dest + i * bs;
            switch (i % 3) {
            case 0: memset(d, 0x00, bs); break;
            case 1: memset(d, 0xFF, bs); break;
            case 2: memcpy(d, src + i * bs, bs); break;
            }
        }
        patch_seed("zeroone", 5, src, BIG_SRC_LEN, dest, len, log2);
    }

    /* A single 1 MiB block: shifted copy with sparse edits. */
    memcpy(dest, src, BIG_SRC_LEN);
    for (size_t i = 0; i < BIG_SRC_LEN; i += 65521) dest[i] ^= 0xA5;
    patch_seed("oneblock", 5, src, BIG_SRC_LEN, dest, BIG_SRC_LEN, 20);

    free(src);
    free(dest);
}

static void gen_roundtrip_seeds(void) {
    for (uint8_t bs_sel = 0; bs_sel < 8; bs_sel++) {
        for (uint8_t src_mode = 0; src_mode < 2; src_mode++) {
            uint8_t buf[512];
            size_t n = 0;
            /* source length is in units of block_size/32 bytes: 256 input
             * bytes, or a pattern source of 8 KiB at 32-byte blocks up to
             * 1 MiB - 1 unit at 4 KiB blocks */
            size_t unit = (size_t)1 << bs_sel;
            uint16_t src_len = src_mode ? (uint16_t)(256 / unit)
                                        : (uint16_t)(bs_sel < 7 ? 8192 : 8191);
            buf[n++] = bs_sel;          /* block size */
            buf[n++] = 0;               /* all stages on */
            buf[n++] = 128;             /* threshold ~0.5 */
            buf[n++] = 0;               /* default radius */
            buf[n++] = (uint8_t)src_len;
            buf[n++] = (uint8_t)(src_len >> 8);
            buf[n++] = src_mode;
            buf[n++] = 42;              /* pattern seed */
            buf[n++] = 64;              /* dest blocks */
            if (src_mode) {
                for (int i = 0; i < 256; i++) buf[n++] = (uint8_t)(i * 7 + i / 13);
            }
            /* one of each edit op: op, len(2), op-specific bytes */
            static const uint8_t script[] = {
                0, 100, 0,
                1, 50, 0, 0x34, 0x12,
                2, 64, 0,
                3, 64, 0,
                4, 8, 0, 1, 2, 3, 4, 5, 6, 7, 8,
                5, 200, 0, 3, 2, 10, 0, 0xAA, 20, 0, 0xBB,
                6, 40, 0, 7, 0,
                7, 120, 0, 0x20, 0x00,
            };
            memcpy(buf + n, script, sizeof(script));
            n += sizeof(script);

            char fname[64];
            snprintf(fname, sizeof(fname), "rt_bs%u_src%u", 32u << bs_sel, src_mode);
            write_seed("roundtrip", fname, buf, n, NULL, 0);
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <outdir>\n", argv[0]);
        return 2;
    }
    snprintf(outdir, sizeof(outdir), "%s", argv[1]);
    if (mkdir(outdir, 0755) != 0 && errno != EEXIST) fz_die(outdir);
    make_dir("patch");
    make_dir("roundtrip");

    gen_patch_seeds();
    gen_big_patch_seeds();
    gen_roundtrip_seeds();

    printf("gen_seeds: wrote %d seeds to %s\n", seed_count, outdir);
    return 0;
}
