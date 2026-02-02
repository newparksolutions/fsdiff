/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file fsdiff.c
 * @brief fsdiff command-line tool
 *
 * Usage:
 *   fsdiff delta <source> <destination> <patch>
 *   fsdiff patch <source> <patch> <output>
 *   fsdiff info <patch>
 */

#include <fsdiff/fsdiff.h>
#include "../src/getopt_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog) {
    fprintf(stderr, "fsdiff - Binary block-level diff/patch tool\n\n");
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s delta [options] <source> <destination> <patch>\n", prog);
    fprintf(stderr, "  %s patch [options] <source> <patch> <output>\n", prog);
    fprintf(stderr, "  %s info <patch>\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Commands:\n");
    fprintf(stderr, "  delta    Generate a patch file from source to destination\n");
    fprintf(stderr, "  patch     Apply a patch file to source, creating output\n");
    fprintf(stderr, "  info      Display information about a patch file\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Delta options:\n");
    fprintf(stderr, "  -b, --block-size <log2>   Block size as power of 2 (default: 12 = 4096)\n");
    fprintf(stderr, "  -r, --search-radius <n>   Search radius for relocation (default: 8)\n");
    fprintf(stderr, "  -p, --projections <n>     Number of FFT projections (default: 1)\n");
    fprintf(stderr, "  -t, --threshold <f>       Partial match threshold 0.0-1.0 (default: 0.5)\n");
    fprintf(stderr, "  --no-identity             Disable identity matching\n");
    fprintf(stderr, "  --no-relocation           Disable relocation matching\n");
    fprintf(stderr, "  --no-partial              Disable partial (FFT) matching\n");
    fprintf(stderr, "  --scalar                  Force scalar (non-SIMD) code path\n");
    fprintf(stderr, "  -v, --verbose             Show progress and statistics\n");
    fprintf(stderr, "  -V, --very-verbose        Detailed output from all matching stages\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Patch options:\n");
    fprintf(stderr, "  --verify                  Verify output checksum if available\n");
    fprintf(stderr, "  -v, --verbose             Show progress\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s delta fs_v1.img fs_v2.img update.patch\n", prog);
    fprintf(stderr, "  %s patch fs_v1.img update.patch fs_v2_rebuilt.img\n", prog);
    fprintf(stderr, "  %s info update.patch\n", prog);
}

static void progress_callback(void *user_data, uint64_t current, uint64_t total) {
    int *verbose = (int *)user_data;
    if (*verbose) {
        int percent = (int)((current * 100) / total);
        fprintf(stderr, "\rProgress: %d%% (%lu / %lu blocks)",
                percent, (unsigned long)current, (unsigned long)total);
        if (current == total) {
            fprintf(stderr, "\n");
        }
    }
}

static int cmd_create(int argc, char **argv) {
    static struct option long_options[] = {
        {"block-size",      required_argument, 0, 'b'},
        {"search-radius",   required_argument, 0, 'r'},
        {"projections",     required_argument, 0, 'p'},
        {"threshold",       required_argument, 0, 't'},
        {"no-identity",     no_argument,       0, 'I'},
        {"no-relocation",   no_argument,       0, 'R'},
        {"no-partial",      no_argument,       0, 'P'},
        {"scalar",          no_argument,       0, 'S'},
        {"verbose",         no_argument,       0, 'v'},
        {"very-verbose",    no_argument,       0, 'V'},
        {"help",            no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    fsd_diff_options_t opts;
    fsd_diff_options_init(&opts);
    int verbose = 0;

    int opt;
    while ((opt = getopt_long(argc, argv, "b:r:p:t:vVh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'b':
            opts.block_size_log2 = atoi(optarg);
            if (opts.block_size_log2 < 9 || opts.block_size_log2 > 20) {
                fprintf(stderr, "Error: block-size must be between 9 and 20\n");
                return 1;
            }
            break;
        case 'r':
            opts.search_radius = atoi(optarg);
            break;
        case 'p':
            opts.num_projections = atoi(optarg);
            break;
        case 't':
            opts.partial_threshold = (float)atof(optarg);
            break;
        case 'I':
            opts.enable_identity = false;
            break;
        case 'R':
            opts.enable_relocation = false;
            break;
        case 'P':
            opts.enable_partial = false;
            break;
        case 'S':
            opts.force_scalar = true;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'V':
            verbose = 1;
            opts.verbose = true;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            return 1;
        }
    }

    if (optind + 3 != argc) {
        fprintf(stderr, "Error: delta requires <source> <destination> <patch>\n");
        return 1;
    }

    const char *src_path = argv[optind];
    const char *dest_path = argv[optind + 1];
    const char *patch_path = argv[optind + 2];

    if (verbose) {
        fprintf(stderr, "Creating patch: %s -> %s => %s\n",
                src_path, dest_path, patch_path);
        fprintf(stderr, "Block size: %zu bytes\n", (size_t)1 << opts.block_size_log2);
    }

    fsd_diff_ctx_t *ctx = NULL;
    fsd_error_t err = fsd_diff_create(&ctx, &opts);
    if (err != FSD_SUCCESS) {
        fprintf(stderr, "Error creating diff context: %s\n", fsd_strerror(err));
        return 1;
    }

    if (verbose) {
        fsd_diff_set_progress(ctx, progress_callback, &verbose);
    }

    err = fsd_diff_files(ctx, src_path, dest_path, patch_path);
    if (err != FSD_SUCCESS) {
        fprintf(stderr, "Error creating patch: %s\n", fsd_strerror(err));
        fsd_diff_destroy(ctx);
        return 1;
    }

    if (verbose) {
        fsd_diff_stats_t stats;
        fsd_diff_get_stats(ctx, &stats);

        fprintf(stderr, "\nStatistics:\n");
        fprintf(stderr, "  Total blocks:      %lu\n", (unsigned long)stats.total_blocks);
        fprintf(stderr, "  Identity matches:  %lu (%.1f%%)\n",
                (unsigned long)stats.identity_matches,
                100.0 * stats.identity_matches / stats.total_blocks);
        fprintf(stderr, "  Relocate matches:  %lu (%.1f%%)\n",
                (unsigned long)stats.relocate_matches,
                100.0 * stats.relocate_matches / stats.total_blocks);
        fprintf(stderr, "  Partial matches:   %lu (%.1f%%)\n",
                (unsigned long)stats.partial_matches,
                100.0 * stats.partial_matches / stats.total_blocks);
        fprintf(stderr, "  Zero blocks:       %lu (%.1f%%)\n",
                (unsigned long)stats.zero_blocks,
                100.0 * stats.zero_blocks / stats.total_blocks);
        fprintf(stderr, "  One blocks:        %lu (%.1f%%)\n",
                (unsigned long)stats.one_blocks,
                100.0 * stats.one_blocks / stats.total_blocks);
        fprintf(stderr, "  Literal blocks:    %lu (%.1f%%)\n",
                (unsigned long)stats.literal_blocks,
                100.0 * stats.literal_blocks / stats.total_blocks);
        fprintf(stderr, "  Patch size:        %lu bytes\n", (unsigned long)stats.patch_size);
        fprintf(stderr, "  Time:              %.2f seconds\n", stats.elapsed_seconds);
    }

    fsd_diff_destroy(ctx);
    return 0;
}

static int cmd_apply(int argc, char **argv) {
    static struct option long_options[] = {
        {"verify",  no_argument, 0, 'V'},
        {"verbose", no_argument, 0, 'v'},
        {"help",    no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    fsd_patch_options_t opts;
    fsd_patch_options_init(&opts);
    int verbose = 0;

    int opt;
    while ((opt = getopt_long(argc, argv, "vh", long_options, NULL)) != -1) {
        switch (opt) {
        case 'V':
            opts.verify_output = true;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            return 1;
        }
    }

    if (optind + 3 != argc) {
        fprintf(stderr, "Error: apply requires <source> <patch> <output>\n");
        return 1;
    }

    const char *src_path = argv[optind];
    const char *patch_path = argv[optind + 1];
    const char *output_path = argv[optind + 2];

    if (verbose) {
        fprintf(stderr, "Applying patch: %s + %s => %s\n",
                src_path, patch_path, output_path);
    }

    fsd_patch_ctx_t *ctx = NULL;
    fsd_error_t err = fsd_patch_create(&ctx, &opts);
    if (err != FSD_SUCCESS) {
        fprintf(stderr, "Error creating patch context: %s\n", fsd_strerror(err));
        return 1;
    }

    if (verbose) {
        fsd_patch_set_progress(ctx, progress_callback, &verbose);
    }

    err = fsd_patch_apply(ctx, src_path, patch_path, output_path);
    if (err != FSD_SUCCESS) {
        fprintf(stderr, "Error applying patch: %s\n", fsd_strerror(err));
        fsd_patch_destroy(ctx);
        return 1;
    }

    if (verbose) {
        fprintf(stderr, "Patch applied successfully.\n");
    }

    fsd_patch_destroy(ctx);
    return 0;
}

static int cmd_info(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Error: info requires <patch>\n");
        return 1;
    }

    const char *patch_path = argv[0];

    fsd_header_t header;
    fsd_error_t err = fsd_patch_read_header(patch_path, &header);
    if (err != FSD_SUCCESS) {
        fprintf(stderr, "Error reading patch header: %s\n", fsd_strerror(err));
        return 1;
    }

    size_t block_size = (size_t)1 << header.block_size_log2;
    size_t output_size = header.dest_blocks * block_size;

    printf("Patch file: %s\n", patch_path);
    printf("\n");
    printf("Format:           BKDF (Binary Block Diff Format)\n");
    printf("Version:          %u\n", header.version);
    printf("Block size:       %zu bytes (2^%u)\n", block_size, header.block_size_log2);
    printf("Output blocks:    %lu\n", (unsigned long)header.dest_blocks);
    printf("Output size:      %lu bytes (%.2f MiB)\n",
           (unsigned long)output_size, output_size / (1024.0 * 1024.0));
    printf("\n");
    printf("Stream sizes:\n");
    printf("  Operations:     %lu bytes\n", (unsigned long)header.op_stream_len);
    printf("  Diff data:      %lu bytes\n", (unsigned long)header.diff_stream_len);

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* Initialize library */
    fsd_error_t err = fsd_init();
    if (err != FSD_SUCCESS) {
        fprintf(stderr, "Error initializing library: %s\n", fsd_strerror(err));
        return 1;
    }

    const char *cmd = argv[1];
    int result;

    if (strcmp(cmd, "delta") == 0) {
        result = cmd_create(argc - 1, argv + 1);
    } else if (strcmp(cmd, "patch") == 0) {
        result = cmd_apply(argc - 1, argv + 1);
    } else if (strcmp(cmd, "info") == 0) {
        result = cmd_info(argc - 2, argv + 2);
    } else if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(argv[0]);
        result = 0;
    } else if (strcmp(cmd, "--version") == 0) {
        printf("fsdiff %s\n", fsd_version());
        printf("Build: %s\n", fsd_build_info());
        result = 0;
    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        print_usage(argv[0]);
        result = 1;
    }

    fsd_cleanup();
    return result;
}
