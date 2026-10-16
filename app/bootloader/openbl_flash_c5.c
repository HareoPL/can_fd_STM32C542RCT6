/**
 * @file openbl_flash_c5.c
 * @brief Flash programming and page erasure implementation for STM32C542.
 */

#include "openbl_flash_c5.h"
#include "boot_shared.h"
#include <string.h>

#define STM32C5_FLASH_PAGE_SIZE       2048U   /**< Flash page size: 2 KB */

static hal_flash_handle_t s_hflash;
static bool s_flash_initialized = false;

hal_status_t openbl_flash_init(void)
{
    if (s_flash_initialized)
    {
        return HAL_OK;
    }

    memset(&s_hflash, 0, sizeof(s_hflash));
    hal_status_t st = HAL_FLASH_Init(&s_hflash, HAL_FLASH);
    if (st == HAL_OK)
    {
        s_flash_initialized = true;
    }
    return st;
}

hal_status_t openbl_flash_erase(uint32_t start_addr, uint32_t size_bytes)
{
    if (!s_flash_initialized)
    {
        if (openbl_flash_init() != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    /* Safety checks: Never erase bootloader or write outside valid Flash */
    if ((start_addr < BOOT_APP_START_ADDR) || (size_bytes == 0U))
    {
        return HAL_INVALID_PARAM;
    }

    if ((start_addr + size_bytes) > BOOT_APP_END_ADDR)
    {
        return HAL_INVALID_PARAM;
    }

    /* Round up size_bytes to multiple of page size (2048 B) */
    uint32_t rem = size_bytes % STM32C5_FLASH_PAGE_SIZE;
    uint32_t aligned_size = (rem == 0U) ? size_bytes : (size_bytes + STM32C5_FLASH_PAGE_SIZE - rem);

    /* Unlock Flash registers */
    HAL_FLASH_ITF_Unlock(HAL_FLASH);

    /* Erase requested pages */
    hal_status_t st = HAL_FLASH_EraseByAddr(&s_hflash, start_addr, aligned_size, 10000U);

    /* Lock Flash registers */
    HAL_FLASH_ITF_Lock(HAL_FLASH);

    return st;
}

hal_status_t openbl_flash_write(uint32_t start_addr, const uint8_t *data, uint32_t length)
{
    if (!s_flash_initialized)
    {
        if (openbl_flash_init() != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    if ((data == NULL) || (length == 0U))
    {
        return HAL_INVALID_PARAM;
    }

    /* Safety check: destination must reside within application region */
    if ((start_addr < BOOT_APP_START_ADDR) || ((start_addr + length) > BOOT_APP_END_ADDR))
    {
        return HAL_INVALID_PARAM;
    }

    /* Unlock Flash registers */
    HAL_FLASH_ITF_Unlock(HAL_FLASH);

    /* Program Flash using adaptive width */
    hal_status_t st = HAL_FLASH_ProgramByAddrAdapt(&s_hflash, start_addr, (const uint32_t *)data, length, 2000U);

    /* Lock Flash registers */
    HAL_FLASH_ITF_Lock(HAL_FLASH);

    if (st != HAL_OK)
    {
        return st;
    }

    /* Post-write verification */
    if (memcmp((const void *)start_addr, data, length) != 0)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}
