/**
 * @file crc32.h
 * @brief Fast IEEE 802.3 CRC32 implementation compatible with Python zlib.crc32.
 */

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the CRC32 running state.
 * @return Initial CRC value (0xFFFFFFFF).
 */
uint32_t crc32_init(void);

/**
 * @brief Updates a running CRC32 checksum with a chunk of data.
 * @param[in] current_crc Current running CRC value.
 * @param[in] data Pointer to data buffer.
 * @param[in] length Number of bytes to process.
 * @return Updated CRC value.
 */
uint32_t crc32_update(uint32_t current_crc, const uint8_t *data, size_t length);

/**
 * @brief Finalizes the CRC32 checksum (performs post-conditioning XOR).
 * @param[in] current_crc Running CRC value.
 * @return Final 32-bit checksum value.
 */
uint32_t crc32_finalize(uint32_t current_crc);

/**
 * @brief Computes CRC32 checksum over a complete data buffer in one step.
 * @param[in] data Pointer to data buffer.
 * @param[in] length Number of bytes to process.
 * @return Final 32-bit CRC32 checksum.
 */
uint32_t crc32_calculate(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* CRC32_H */

