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
 * @param crc   Current CRC value (use 0 for initial)
 * @param data  Input data
 * @param len   Length in bytes
 * @return      Updated CRC32 value
 */
uint32_t fsd_crc32_update(uint32_t crc, const void *data, size_t len);

/**
 * Finalize a CRC32 checksum.
 *
 * @param crc  Running CRC value
 * @return     Final CRC32 checksum
 */
uint32_t fsd_crc32_final(uint32_t crc);

#ifdef __cplusplus
}
#endif

#endif /* FSDIFF_CRC32_H */
