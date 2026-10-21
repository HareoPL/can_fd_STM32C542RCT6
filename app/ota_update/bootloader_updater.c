/**
 * @file bootloader_updater.c
 * @brief Bootloader update engine operating from Main Application via Flash Staging Slot.
 */

#include "bootloader_updater.h"
#include "crc32.h"
#include "safe_printf.h"
#include "stm32_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define TAG                             "BOOT_UPDATER"

static hal_flash_handle_t s_hflash;
static bool s_flash_inited = false;

/**
 * @brief Static buffer holding one OTA block in BSS to guarantee 32-bit alignment for HAL flash writes.
 */
static uint32_t s_aligned_buf[OTA_MAX_BLOCK_SIZE / 4U];

bool bootloader_updater_init(void)
{
    if (s_flash_inited)
    {
        return true;
    }

    memset(&s_hflash, 0, sizeof(s_hflash));
    hal_status_t st = HAL_FLASH_Init(&s_hflash, HAL_FLASH);
    if (st == HAL_OK)
    {
        s_flash_inited = true;
        return true;
    }

    safe_printf(TAG, "[INIT] HAL_FLASH_Init failed (code=%d)!\r\n", (int)st);
    return false;
}

bool bootloader_updater_erase_staging(uint32_t size_bytes)
{
    if (!bootloader_updater_init())
    {
        return false;
    }

    if ((size_bytes == 0U) || (size_bytes > BOOTLOADER_STAGING_SIZE_BYTES))
    {
        safe_printf(TAG, "[ERASE] Invalid staging size: %lu B\r\n", (unsigned long)size_bytes);
        return false;
    }

    /* Round up to 2 KB page boundary */
    uint32_t rem = size_bytes % BOOTLOADER_PAGE_SIZE;
    uint32_t aligned_size = (rem == 0U) ? size_bytes : (size_bytes + BOOTLOADER_PAGE_SIZE - rem);

    safe_printf(TAG, "[ERASE] Erasing Staging Slot at 0x%08lX (%lu bytes, %lu pages)...\r\n",
                BOOTLOADER_STAGING_BASE, (unsigned long)aligned_size, (unsigned long)(aligned_size / BOOTLOADER_PAGE_SIZE));

    HAL_FLASH_ITF_Unlock(HAL_FLASH);
    hal_status_t st = HAL_FLASH_EraseByAddr(&s_hflash, BOOTLOADER_STAGING_BASE, aligned_size, 10000U);
    HAL_FLASH_ITF_Lock(HAL_FLASH);

    if (st != HAL_OK)
    {
        safe_printf(TAG, "[ERASE] Failed to erase Staging Slot (code=%d)!\r\n", (int)st);
        return false;
    }

    safe_printf(TAG, "[ERASE] Staging Slot erased successfully!\r\n");
    return true;
}

bool bootloader_updater_write_staging(uint32_t address, const uint8_t *data, uint16_t length)
{
    if (!bootloader_updater_init())
    {
        return false;
    }

    if ((data == NULL) || (length == 0U) || (length > sizeof(s_aligned_buf)))
    {
        return false;
    }

    /* Boundary safety check: must reside entirely inside staging slot */
    if ((address < BOOTLOADER_STAGING_BASE) ||
        ((address + length) > (BOOTLOADER_STAGING_BASE + BOOTLOADER_STAGING_SIZE_BYTES)))
    {
        safe_printf(TAG, "[WRITE] Address out of bounds: 0x%08lX (len=%u)\r\n", address, (unsigned int)length);
        return false;
    }

    /* Copy into 32-bit aligned buffer */
    memcpy(s_aligned_buf, data, length);

    HAL_FLASH_ITF_Unlock(HAL_FLASH);
    hal_status_t st = HAL_FLASH_ProgramByAddrAdapt(&s_hflash, address, s_aligned_buf, length, 5000U);
    HAL_FLASH_ITF_Lock(HAL_FLASH);

    if (st != HAL_OK)
    {
        safe_printf(TAG, "[WRITE] Flash write failed at 0x%08lX (code=%d)\r\n", address, (int)st);
        return false;
    }

    /* Immediate post-write verification */
    if (memcmp((const void *)address, data, length) != 0)
    {
        safe_printf(TAG, "[WRITE] Verification failed at 0x%08lX\r\n", address);
        return false;
    }

    return true;
}

bool bootloader_updater_verify_vector_table(const uint32_t *vector_base)
{
    if (vector_base == NULL)
    {
        return false;
    }

    uint32_t msp = vector_base[0];
    uint32_t reset_handler = vector_base[1];

    /* Verify MSP is in SRAM region (0x20000000 to 0x20010000) */
    if ((msp < 0x20000000UL) || (msp > 0x20010000UL))
    {
        safe_printf(TAG, "[VERIFY] Invalid MSP: 0x%08lX (not in SRAM)\r\n", (unsigned long)msp);
        return false;
    }

    /* Verify Reset_Handler points inside Bootloader Flash (0x08000001 to 0x08008000) */
    if ((reset_handler < (BOOTLOADER_FLASH_BASE + 1U)) ||
        (reset_handler >= (BOOTLOADER_FLASH_BASE + BOOTLOADER_MAX_SIZE_BYTES)))
    {
        safe_printf(TAG, "[VERIFY] Invalid Reset_Handler: 0x%08lX (out of bootloader Flash)\r\n",
                    (unsigned long)reset_handler);
        return false;
    }

    /* In ARM Cortex-M, reset handler address must have Thumb bit set (bit 0 = 1) */
    if ((reset_handler & 1U) == 0U)
    {
        safe_printf(TAG, "[VERIFY] Invalid Reset_Handler: 0x%08lX (Thumb bit not set)\r\n",
                    (unsigned long)reset_handler);
        return false;
    }

    return true;
}

bool bootloader_updater_install(uint32_t expected_size, uint32_t expected_crc32)
{
    if (!bootloader_updater_init())
    {
        return false;
    }

    if ((expected_size < 512U) || (expected_size > BOOTLOADER_MAX_SIZE_BYTES))
    {
        safe_printf(TAG, "[INSTALL] Invalid image size: %lu B\r\n", (unsigned long)expected_size);
        return false;
    }

    /* Step 1: Verify CRC32 of entire staged image */
    uint32_t calc_crc = crc32_calculate((const uint8_t *)BOOTLOADER_STAGING_BASE, expected_size);
    if (calc_crc != expected_crc32)
    {
        safe_printf(TAG, "[INSTALL] Staging CRC mismatch! Calc=0x%08lX, Exp=0x%08lX\r\n",
                    (unsigned long)calc_crc, (unsigned long)expected_crc32);
        return false;
    }

    /* Step 2: Validate Cortex-M33 vector table in staging slot */
    if (!bootloader_updater_verify_vector_table((const uint32_t *)BOOTLOADER_STAGING_BASE))
    {
        safe_printf(TAG, "[INSTALL] Staged vector table validation FAILED!\r\n");
        return false;
    }

    safe_printf(TAG, "[INSTALL] Integrity validated (CRC=0x%08lX, Size=%lu B). Installing to 0x08000000...\r\n",
                (unsigned long)calc_crc, (unsigned long)expected_size);

    uint32_t rem = expected_size % BOOTLOADER_PAGE_SIZE;
    uint32_t aligned_size = (rem == 0U) ? expected_size : (expected_size + BOOTLOADER_PAGE_SIZE - rem);

    /* Step 3: Atomic install (Erase 0x08000000 -> Copy Staging -> Verify) */
    taskENTER_CRITICAL();
    HAL_FLASH_ITF_Unlock(HAL_FLASH);

    /* Erase Bootloader flash region */
    hal_status_t st = HAL_FLASH_EraseByAddr(&s_hflash, BOOTLOADER_FLASH_BASE, aligned_size, 10000U);
    if (st != HAL_OK)
    {
        HAL_FLASH_ITF_Lock(HAL_FLASH);
        taskEXIT_CRITICAL();
        safe_printf(TAG, "[INSTALL] Flash erase failed at 0x%08lX (code=%d)!\r\n",
                    BOOTLOADER_FLASH_BASE, (int)st);
        return false;
    }

    /* Program new Bootloader from Staging Slot into 0x08000000 */
    uint32_t offset = 0U;
    while (offset < expected_size)
    {
        uint32_t chunk_len = expected_size - offset;
        if (chunk_len > sizeof(s_aligned_buf))
        {
            chunk_len = sizeof(s_aligned_buf);
        }

        memcpy(s_aligned_buf, (const void *)(BOOTLOADER_STAGING_BASE + offset), chunk_len);

        st = HAL_FLASH_ProgramByAddrAdapt(&s_hflash, BOOTLOADER_FLASH_BASE + offset, s_aligned_buf, chunk_len, 5000U);
        if (st != HAL_OK)
        {
            break;
        }

        offset += chunk_len;
    }

    HAL_FLASH_ITF_Lock(HAL_FLASH);
    taskEXIT_CRITICAL();

    if (st != HAL_OK)
    {
        safe_printf(TAG, "[INSTALL] Flash programming failed at offset %lu (code=%d)!\r\n",
                    (unsigned long)offset, (int)st);
        return false;
    }

    /* Step 4: Byte-by-byte post-write verification */
    if (memcmp((const void *)BOOTLOADER_FLASH_BASE, (const void *)BOOTLOADER_STAGING_BASE, expected_size) != 0)
    {
        safe_printf(TAG, "[INSTALL] Post-write verification mismatch!\r\n");
        return false;
    }

    /* Step 5: Clean up staging area */
    (void)bootloader_updater_erase_staging(aligned_size);

    safe_printf(TAG, "[INSTALL] SUCCESS! Bootloader updated and verified at 0x08000000 (%lu bytes).\r\n",
                (unsigned long)expected_size);

    return true;
}
