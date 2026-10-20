/**
 * @file boot_shared.c
 * @brief Implementation of shared RAM buffer management for STM32C5 Bootloader.
 */

#include "boot_shared.h"
#include "crc32.h"
#include <string.h>

/**
 * @brief Global shared structure instance placed at 0x2000FFC0 (.noinit.boot_shared).
 * @note This region is excluded from BSS/data initialization in startup scripts.
 */
#if defined(__GNUC__)
volatile boot_shared_ram_t g_boot_shared __attribute__((section(".noinit.boot_shared"), used));
#else
volatile boot_shared_ram_t g_boot_shared;
#endif

#define BOOT_SHARED_DATA_LEN    (sizeof(boot_shared_ram_t) - sizeof(uint32_t))

bool boot_shared_is_valid(void)
{
    uint32_t computed = crc32_calculate((const uint8_t *)(uintptr_t)&g_boot_shared, BOOT_SHARED_DATA_LEN);
    return (computed == g_boot_shared.crc32);
}

void boot_shared_update_crc(void)
{
    g_boot_shared.crc32 = crc32_calculate((const uint8_t *)(uintptr_t)&g_boot_shared, BOOT_SHARED_DATA_LEN);
}

void boot_shared_clear(void)
{
    memset((void *)(uintptr_t)&g_boot_shared, 0, sizeof(boot_shared_ram_t));
    boot_shared_update_crc();
}

void boot_shared_request_bootloader(bool session_selected, uint8_t active_can_bus)
{
    g_boot_shared.magic = BOOT_SHARED_MAGIC;
    g_boot_shared.session_selected = session_selected ? 1U : 0U;
    g_boot_shared.active_can_bus = (uint32_t)active_can_bus;
    boot_shared_update_crc();
}

void boot_shared_record_fault(boot_fault_reason_t reason, uint32_t pc, uint32_t cfsr)
{
    /* Preserve existing fail counter if structure was valid */
    uint32_t prev_cnt = 0U;
    if (boot_shared_is_valid())
    {
        prev_cnt = g_boot_shared.boot_fail_counter;
    }

    g_boot_shared.magic = 0U;
    g_boot_shared.session_selected = 0U;
    g_boot_shared.boot_fail_counter = prev_cnt + 1U;
    g_boot_shared.fault_reason = (uint32_t)reason;
    g_boot_shared.fault_pc = pc;
    g_boot_shared.fault_cfsr = cfsr;
    boot_shared_update_crc();
}
