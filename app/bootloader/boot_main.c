/**
 * @file boot_main.c
 * @brief Main entry point for STM32C542 CAN Bootloader (v_box_boot).
 * @details Runs lightweight bare-metal firmware (no RTOS).
 *          Validates shared RAM, checks crash recovery counter, listens on FDCAN2,
 *          and either stays in Bootloader mode or branches to Main Application.
 */

#include "stm32_hal.h"
#include "mx_system.h"
#include "mx_usart2.h"
#include "bsp_io.h"
#include "safe_printf.h"
#include "boot_shared.h"
#include "boot_jump.h"
#include "openbl_core.h"
#include "boot_version_config.h"

#define TAG                 "BOOT"

/**
 * @brief Simple delay based on ARM cycle execution.
 */
static void delay_cycles(uint32_t cycles)
{
    for (volatile uint32_t i = 0; i < cycles; i++)
    {
        __NOP();
    }
}

int main(void)
{
    /* Initialize system clocks (144 MHz), ICACHE, GPIO, and Power */
    (void)mx_system_init();

    /* Initialize user LED (LD1 on PB7) */
    (void)bsp_io_init();

    /* Initialize USART2 for bootloader diagnostic output */
    (void)mx_usart2_uart_init();

    safe_printf(TAG, "\r\n==============================================\r\n");
    safe_printf(TAG, "  STM32C542 CAN Bootloader (v_box_boot %s)-OTA  \r\n", BOOT_VERSION_STRING);
    safe_printf(TAG, "  Git: %s | Built: %s\r\n", BOOT_GIT_DESCRIBE, BOOT_BUILD_TIMESTAMP);
    safe_printf(TAG, "==============================================\r\n");

    /* Inspect Shared RAM structure */
    boot_mode_t mode = BOOT_MODE_NORMAL;
    bool session_selected = false;
    uint8_t active_can_bus = BOOT_CAN_BUS_AUTO;

    if (boot_shared_is_valid())
    {
        if (g_boot_shared.magic == BOOT_SHARED_MAGIC)
        {
            mode = BOOT_MODE_STAY_IN_BOOT;
            session_selected = (g_boot_shared.session_selected == 1U);
            active_can_bus = (uint8_t)g_boot_shared.active_can_bus;
            safe_printf(TAG, "[INIT] App requested Bootloader mode (bus=%u)! Staying in bootloader.\r\n",
                        (unsigned int)active_can_bus);

            /* Clear magic so subsequent reset is normal unless requested again */
            g_boot_shared.magic = 0U;
            boot_shared_update_crc();
        }
        else if (g_boot_shared.boot_fail_counter >= BOOT_MAX_FAIL_COUNTER)
        {
            mode = BOOT_MODE_RECOVERY;
            safe_printf(TAG, "[INIT] RECOVERY MODE ACTIVATED! (App crashed %lu times, reason=%lu, PC=0x%08lX)\r\n",
                        g_boot_shared.boot_fail_counter, g_boot_shared.fault_reason, g_boot_shared.fault_pc);
        }
        else
        {
            safe_printf(TAG, "[INIT] Normal start (fail_cnt=%lu)\r\n", g_boot_shared.boot_fail_counter);
        }
    }
    else
    {
        /* Cold Power-On Reset or corrupted RAM */
        safe_printf(TAG, "[INIT] Cold start / Shared RAM uninitialized. Initializing.\r\n");
        boot_shared_clear();
    }

    /* -------------------------------------------------------------------------
     * Phase A: Normal Boot - Instant Jump to Application if Valid
     * ------------------------------------------------------------------------- */
    if (mode == BOOT_MODE_NORMAL)
    {
        if (boot_is_app_valid(BOOT_APP_START_ADDR))
        {
            safe_printf(TAG, "[FAST BOOT] Valid Application at 0x%08lX. Booting immediately...\r\n", BOOT_APP_START_ADDR);

            /* Increment boot fail counter to detect crash loops before healthy confirmation */
            g_boot_shared.boot_fail_counter++;
            boot_shared_update_crc();

            boot_jump_to_application(BOOT_APP_START_ADDR);
        }
        else
        {
            safe_printf(TAG, "[RECOVERY] No valid application at 0x%08lX (Flash blank or corrupt)!\r\n", BOOT_APP_START_ADDR);
            safe_printf(TAG, "[RECOVERY] Entering silent Recovery Mode. Listening on CAN1 & CAN2 (0x01F2F3DF)...\r\n");
            mode = BOOT_MODE_RECOVERY;
            g_boot_shared.fault_reason = BOOT_FAULT_NO_APP;
            g_boot_shared.fault_pc = BOOT_APP_START_ADDR;
            g_boot_shared.boot_fail_counter = BOOT_MAX_FAIL_COUNTER;
            boot_shared_update_crc();
        }
    }

    /* -------------------------------------------------------------------------
     * Phase B: Stay in Bootloader or Recovery Mode (CAN1 + CAN2 listener active)
     * ------------------------------------------------------------------------- */
    /* Initialize Bootloader CAN core and Flash subsystem (zero unsolicited traffic) */
    openbl_core_init(session_selected, mode, active_can_bus);

    uint32_t led_ticker = 0U;

    while (1)
    {
        openbl_core_poll();

        led_ticker++;

        /* Fast alert LED blink (Recovery = 5 Hz, Update = 2 Hz) */
        uint32_t led_period = (mode == BOOT_MODE_RECOVERY) ? 500U : 1500U;
        if (led_ticker >= led_period)
        {
            bsp_io_output_setPin(!bsp_io_output_get_state());
            led_ticker = 0U;
        }

        delay_cycles(1440U); /* ~10 us */
    }

    return 0;
}
