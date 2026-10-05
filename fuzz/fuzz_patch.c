/*
 * fuzz_patch.c - libFuzzer harness for fsd_patch_apply().
 *
 * The patch is untrusted input, so this is the primary target. Patch,
 * source and output are all memfds reached via /proc/self/fd/N; the
 * library runs exactly as shipped (open + mmap + fopen).
 *
 * Input layout:
 *   byte 0      control: bit 0 = mode (0 raw, 1 structured)
 *                        bits 1-3 = which source image to use
 *   raw:        bytes 1..  are the patch file verbatim
 *   structured: byte 1     block_size_log2 = 5 + (b % 20)  (32 B..16 MiB)
 *               bytes 2-3  dest_blocks (LE, reduced to fit the output cap)
 *               bytes 4-5  op_stream_len   (LE, mod remaining + 1)
 *               bytes 6-7  diff_stream_len (LE, mod remaining + 1)
 *               bytes 8..  op, diff and literal streams
 *   Structured mode writes a header whose stream lengths are consistent
 *   with the data, so mutations reach the op decoder instead of being
 *   rejected by the header length checks.
 *
 * Oracle: sanitizers, plus on FSD_SUCCESS the output must be exactly
 * dest_blocks << block_size_log2 bytes.
 */

#include "fuzz_common.h"

#include <fsdiff/fsdiff.h>

/* Output-size cap: the patcher can legitimately expand a tiny patch into an
 * enormous output, and memfds live in RAM. Inputs declaring more are skipped. */
#define FZ_MAX_OUTPUT (16u << 20)

/* Every source is a prefix of the same pattern. Selectors 0-3 predate the
 * large sources; keep their sizes so existing corpora keep their meaning. */
#define FZ_NUM_SOURCES 8
static const size_t source_sizes[FZ_NUM_SOURCES] = {
    0, 31, 4096 + 7, 65536,
    (256u << 10) + 1, 1u << 20, (1u << 20) + 4097, (16u << 20) + 1,
};

static fz_file_t sources[FZ_NUM_SOURCES];
static fz_file_t patch_file;
static fz_file_t out_file;

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    size_t max = source_sizes[FZ_NUM_SOURCES - 1];
    uint8_t *buf = malloc(max);
    if (!buf) fz_die("malloc");
    fz_fill_pattern(buf, max, FZ_SOURCE_SEED);
    for (int i = 0; i < FZ_NUM_SOURCES; i++) {
        fz_file_init(&sources[i], "fz_source");
        fz_file_set(&sources[i], buf, source_sizes[i]);
    }
    free(buf);
    fz_file_init(&patch_file, "fz_patch");
    fz_file_init(&out_file, "fz_output");
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return -1;

    uint8_t control = data[0];
    int structured = control & 1;
    const fz_file_t *src = &sources[(control >> 1) & 7];
    data++; size--;

    uint8_t *built = NULL;
    const uint8_t *patch = data;
    size_t patch_len = size;
    uint8_t log2;
    uint64_t dest_blocks;

    if (structured) {
        if (size < 7) return -1;
        log2 = 5 + (data[0] % 20);
        dest_blocks = (uint64_t)(data[1] | data[2] << 8);
        dest_blocks %= (FZ_MAX_OUTPUT >> log2) + 1;
        size_t rest = size - 7;
        size_t op_len = (size_t)(data[3] | data[4] << 8) % (rest + 1);
        size_t diff_len = (size_t)(data[5] | data[6] << 8) % (rest - op_len + 1);

        patch_len = FSD_HEADER_SIZE + rest;
        built = malloc(patch_len);
        if (!built) fz_die("malloc");
        fz_put_header(built, log2, dest_blocks, op_len, diff_len);
        memcpy(built + FSD_HEADER_SIZE, data + 7, rest);
        patch = built;
    } else {
        /* Skip headers that would make the patcher write past the cap. Inputs
         * the header parser rejects outright are still run. */
        if (size >= FSD_HEADER_SIZE && memcmp(data, "BKDF", 4) == 0 &&
            data[5] >= 5 && data[5] <= 24) {
            log2 = data[5];
            dest_blocks = fz_get_u64_le(&data[8]);
            if (dest_blocks > (FZ_MAX_OUTPUT >> log2)) return -1;
        } else {
            log2 = 0;
            dest_blocks = 0;
        }
    }

    fz_file_set(&patch_file, patch, patch_len);
    fz_file_set(&out_file, NULL, 0);

    fsd_patch_options_t opts;
    fsd_patch_options_init(&opts);
    fsd_patch_ctx_t *ctx = NULL;
    if (fsd_patch_create(&ctx, &opts) != FSD_SUCCESS) fz_die("fsd_patch_create");

    fsd_error_t err = fsd_patch_apply(ctx, src->path, patch_file.path, out_file.path);
    fsd_patch_destroy(ctx);

    if (err == FSD_SUCCESS) {
        size_t expect = (size_t)dest_blocks << log2;
        size_t got = fz_file_size(&out_file);
        if (got != expect) {
            fprintf(stderr, "fuzz_patch: success but output is %zu bytes, "
                    "header says %zu\n", got, expect);
            abort();
        }
    }

    free(built);
    return 0;
}
