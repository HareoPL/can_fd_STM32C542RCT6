/**
 * @file bootloader_updater.h
 * @brief Bootloader update engine operating from Main Application via Flash Staging Slot.
 * @details Receives and stages new Bootloader binary images in dedicated top 32 KB Flash
 *          (0x08038000 - 0x0803FFFF), verifies full image CRC32 and Cortex-M33 vector table
 *          integrity, and atomically flashes the new Bootloader into 0x08000000 - 0x08007FFF.
 */

#ifndef BOOTLOADER_UPDATER_H
#define BOOTLOADER_UPDATER_H

#include <stdint.h>
#include <stdbool.h>
#include "ota_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the Flash controller for bootloader update operations.
 * @return true on success, false otherwise.
 */
bool bootloader_updater_init(void);

/**
 * @brief Erases pages in the dedicated Bootloader Staging Slot (0x08038000).
 * @param[in] size_bytes Number of bytes to erase (rounded up to 2 KB page boundary).
 * @return true on success, false if parameters invalid or Flash erase failed.
 */
bool bootloader_updater_erase_staging(uint32_t size_bytes);

/**
 * @brief Writes a data block into the dedicated Bootloader Staging Slot.
 * @param[in] address Target flash address within staging area (0x08038000 - 0x0803FFFF).
 * @param[in] data Pointer to source data buffer.
 * @param[in] length Number of bytes to write.
 * @return true on success, false otherwise.
 */
bool bootloader_updater_write_staging(uint32_t address, const uint8_t *data, uint16_t length);

/**
 * @brief Validates the Cortex-M33 vector table integrity of a staged bootloader image.
 * @param[in] vector_base Pointer to vector table (first word MSP, second word Reset_Handler).
 * @return true if MSP and Reset_Handler addresses are valid for STM32C5 Bootloader, false otherwise.
 */
bool bootloader_updater_verify_vector_table(const uint32_t *vector_base);

/**
 * @brief Atomically commits and installs the staged bootloader into 0x08000000.
 * @details Performs 3-step safety validation:
 *          1. Validates total size is within 512 B to 32 KB.
 *          2. Verifies CRC32 of entire staged image against expected_crc32.
 *          3. Validates Cortex-M33 initial MSP and Reset_Handler in vector table.
 *          If all checks pass, erases 0x08000000 and copies staged binary to 0x08000000,
 *          then verifies written bytes with memcmp.
 * @param[in] expected_size Size of the new bootloader image in bytes.
 * @param[in] expected_crc32 Expected IEEE 802.3 CRC32 checksum.
 * @return true if successfully verified, flashed, and verified; false on any error.
 */
bool bootloader_updater_install(uint32_t expected_size, uint32_t expected_crc32);

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_UPDATER_H */
