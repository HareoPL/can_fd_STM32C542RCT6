/**
 * @file openbl_flash_c5.h
 * @brief Flash memory operations wrapper for STM32C542 Bootloader.
 */

#ifndef OPENBL_FLASH_C5_H
#define OPENBL_FLASH_C5_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32_hal.h"
#include "stm32c5xx_hal_flash.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the STM32C5 Flash HAL controller.
 * @return HAL_OK on success, error code otherwise.
 */
hal_status_t openbl_flash_init(void);

/**
 * @brief Erases Flash pages covering the requested application area.
 * @note start_addr must be >= BOOT_APP_START_ADDR (0x08008000) to protect the bootloader.
 * @param[in] start_addr Flash address to begin erasure.
 * @param[in] size_bytes Number of bytes to erase.
 * @return HAL_OK on success, error code otherwise.
 */
hal_status_t openbl_flash_erase(uint32_t start_addr, uint32_t size_bytes);

/**
 * @brief Programs a block of data into Flash memory and verifies it.
 * @note start_addr must be >= BOOT_APP_START_ADDR (0x08008000).
 * @param[in] start_addr Destination Flash address.
 * @param[in] data Pointer to raw data bytes.
 * @param[in] length Number of bytes to write.
 * @return HAL_OK on success and successful verify, error code otherwise.
 */
hal_status_t openbl_flash_write(uint32_t start_addr, const uint8_t *data, uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* OPENBL_FLASH_C5_H */
