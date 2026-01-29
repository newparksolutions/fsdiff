/**
 * @file bkdf_header.c
 * @brief BKDF header implementation
 */

#include "bkdf_header.h"
#include <string.h>

/* Read little-endian uint64 */
static uint64_t read_u64_le(const uint8_t *p) {
    return (uint64_t)p[0] |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

/* Write little-endian uint64 */
static void write_u64_le(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
    p[4] = (uint8_t)((v >> 32) & 0xFF);
    p[5] = (uint8_t)((v >> 40) & 0xFF);
    p[6] = (uint8_t)((v >> 48) & 0xFF);
    p[7] = (uint8_t)((v >> 56) & 0xFF);
}

fsd_error_t fsd_header_write(FILE *file,
                             uint64_t dest_blocks,
                             uint8_t block_size_log2,
                             uint64_t op_stream_len,
                             uint64_t diff_stream_len) {
    if (!file) {
        return FSD_ERR_INVALID_ARG;
    }

    uint8_t header[FSD_HEADER_SIZE];
    memset(header, 0, sizeof(header));

    /* Magic: "BKDF" */
    header[0] = 'B';
    header[1] = 'K';
    header[2] = 'D';
    header[3] = 'F';

    /* Version */
    header[4] = FSD_VERSION;

    /* Block size log2 */
    header[5] = block_size_log2;

    /* Reserved (bytes 6-7) = 0 */

    /* Destination blocks (bytes 8-15) */
    write_u64_le(&header[8], dest_blocks);

    /* Operation stream length (bytes 16-23) */
    write_u64_le(&header[16], op_stream_len);

    /* Diff stream length (bytes 24-31) */
    write_u64_le(&header[24], diff_stream_len);

    if (fwrite(header, 1, FSD_HEADER_SIZE, file) != FSD_HEADER_SIZE) {
        return FSD_ERR_IO;
    }

    return FSD_SUCCESS;
}

fsd_error_t fsd_header_read(FILE *file, fsd_header_t *header_out) {
    if (!file || !header_out) {
        return FSD_ERR_INVALID_ARG;
    }

    uint8_t buf[FSD_HEADER_SIZE];
    if (fread(buf, 1, FSD_HEADER_SIZE, file) != FSD_HEADER_SIZE) {
        return FSD_ERR_TRUNCATED;
    }

    return fsd_header_read_memory(buf, FSD_HEADER_SIZE, header_out);
}

fsd_error_t fsd_header_read_memory(const void *data,
                                   size_t len,
                                   fsd_header_t *header_out) {
    if (!data || len < FSD_HEADER_SIZE || !header_out) {
        return FSD_ERR_INVALID_ARG;
    }

    const uint8_t *buf = (const uint8_t *)data;

    /* Check magic */
    if (buf[0] != 'B' || buf[1] != 'K' || buf[2] != 'D' || buf[3] != 'F') {
        return FSD_ERR_BAD_MAGIC;
    }

    /* Check version */
    if (buf[4] != FSD_VERSION) {
        return FSD_ERR_BAD_VERSION;
    }

    /* Check block size */
    uint8_t block_size_log2 = buf[5];
    if (block_size_log2 < 9 || block_size_log2 > 20) {
        return FSD_ERR_BAD_BLOCK_SIZE;
    }

    /* Check reserved bytes */
    if (buf[6] != 0 || buf[7] != 0) {
        return FSD_ERR_CORRUPT_DATA;
    }

    /* Fill output structure */
    memcpy(header_out->magic, buf, 4);
    header_out->version = buf[4];
    header_out->block_size_log2 = buf[5];
    header_out->reserved = 0;
    header_out->dest_blocks = read_u64_le(&buf[8]);
    header_out->op_stream_len = read_u64_le(&buf[16]);
    header_out->diff_stream_len = read_u64_le(&buf[24]);

    return FSD_SUCCESS;
}
