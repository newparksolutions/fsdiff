/*
 * Copyright (c) 2026 JL Finance Limited
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file crc32.h
 * @brief CRC32 computation for block checksums
 */

#ifndef FSDIFF_CRC32_H
#define FSDIFF_CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Compute CRC32 checksum of data.
 *
 * Uses the standard CRC32 polynomial (0xEDB88320).
 *
 * @param data  Input data
 * @param len   Length in bytes
 * @return      CRC32 checksum
 */
uint32_t fsd_crc32(const void *data, size_t len);

/**
 * Update a running CRC32 checksum.
 *
 * For incremental use:
 *   uint32_t crc = ~0U;  // or use fsd_crc32_update(~0U, first_chunk, len)
 *   crc = fsd_crc32_update(crc, chunk1, len1);
 *   crc = fsd_crc32_update(crc, chunk2, len2);
 *   uint32_t result = fsd_crc32_final(crc);
 *
 * @param crc   Current CRC value (use ~0U for initial)
 * @param data  Input data
 * @param len   Length in bytes
 * @return      Updated (unfinalized) CRC32 value
 */
uint32_t fsd_crc32_update(uint32_t crc, const void *data, size_t len);

/**
 * Finalize a CRC32 checksum.
 *
 * Must be called once after the last fsd_crc32_update() call.
 *
 * @param crc  Running CRC value from fsd_crc32_update
 * @return     Final CRC32 checksum
 */
uint32_t fsd_crc32_final(uint32_t crc);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_CRC32_H */
