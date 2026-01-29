/**
 * @file stream_writer.c
 * @brief Diff stream writer implementation
 */

#include "stream_writer.h"
#include <stdlib.h>

fsd_error_t fsd_stream_write_dense(fsd_buffered_writer_t *writer,
                                   const uint8_t *source,
                                   const uint8_t *dest,
                                   size_t len) {
    if (!writer || !source || !dest) {
        return FSD_ERR_INVALID_ARG;
    }

    /* Compute and write diff bytes */
    for (size_t i = 0; i < len; i++) {
        uint8_t diff = dest[i] - source[i];  /* mod 256 automatic */
        fsd_error_t err = fsd_writer_write_byte(writer, diff);
        if (err != FSD_SUCCESS) return err;
    }

    return FSD_SUCCESS;
}

fsd_error_t fsd_stream_write_sparse(fsd_buffered_writer_t *writer,
                                    const uint8_t *source,
                                    const uint8_t *dest,
                                    size_t len,
                                    size_t *bytes_out) {
    if (!writer || !source || !dest || !bytes_out) {
        return FSD_ERR_INVALID_ARG;
    }

    size_t written = 0;
    size_t pos = 0;
    int in_copy_add = 1;  /* Start in copy-add mode */

    while (pos < len) {
        if (in_copy_add) {
            /* Copy-add mode: find run of differing bytes */
            size_t run_start = pos;
            while (pos < len && pos - run_start < 255 &&
                   source[pos] != dest[pos]) {
                pos++;
            }
            size_t run_len = pos - run_start;

            /* Write length */
            fsd_error_t err = fsd_writer_write_byte(writer, (uint8_t)run_len);
            if (err != FSD_SUCCESS) return err;
            written++;

            /* Write diff values */
            for (size_t i = run_start; i < pos; i++) {
                uint8_t diff = dest[i] - source[i];
                err = fsd_writer_write_byte(writer, diff);
                if (err != FSD_SUCCESS) return err;
                written++;
            }

            in_copy_add = 0;
        } else {
            /* Copy mode: find run of matching bytes */
            size_t run_start = pos;
            while (pos < len && pos - run_start < 255 &&
                   source[pos] == dest[pos]) {
                pos++;
            }
            size_t run_len = pos - run_start;

            /* Write length */
            fsd_error_t err = fsd_writer_write_byte(writer, (uint8_t)run_len);
            if (err != FSD_SUCCESS) return err;
            written++;

            in_copy_add = 1;
        }
    }

    /* If we ended in copy mode, write final copy-add with length 0 */
    if (!in_copy_add) {
        fsd_error_t err = fsd_writer_write_byte(writer, 0);
        if (err != FSD_SUCCESS) return err;
        written++;
    }

    *bytes_out = written;
    return FSD_SUCCESS;
}

fsd_diff_format_t fsd_stream_choose_format(const uint8_t *source,
                                           const uint8_t *dest,
                                           size_t len) {
    if (!source || !dest || len == 0) {
        return FSD_DIFF_DENSE;
    }

    /* Count differing bytes */
    size_t diff_count = 0;
    for (size_t i = 0; i < len; i++) {
        if (source[i] != dest[i]) {
            diff_count++;
        }
    }

    /*
     * Estimate sparse encoding size:
     * - Overhead: ~2 bytes per run transition
     * - Data: 1 byte per differing byte
     *
     * Sparse is better when changes are localized.
     * Rough heuristic: sparse if <25% bytes differ
     */
    if (diff_count < len / 4) {
        return FSD_DIFF_SPARSE;
    }

    return FSD_DIFF_DENSE;
}
