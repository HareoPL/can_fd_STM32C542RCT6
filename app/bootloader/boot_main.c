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

#define TAG                 "BOOT"
#define NORMAL_WINDOW_CYCLES 43200000UL /**< ~300 ms @ 144 MHz (300 ms * 144,000 cycles/ms) */

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
    safe_printf(TAG, "  STM32C542 CAN Bootloader (v_box_boot v1.0)  \r\n");
    safe_printf(TAG, "==============================================\r\n");

    /* Inspect Shared RAM structure */
    boot_mode_t mode = BOOT_MODE_NORMAL;
    bool session_selected = false;

    if (boot_shared_is_valid())
    {
        if (g_boot_shared.magic == BOOT_SHARED_MAGIC)
        {
            mode = BOOT_MODE_STAY_IN_BOOT;
            session_selected = (g_boot_shared.session_selected == 1U);
            safe_printf(TAG, "[INIT] App requested Bootloader mode! Staying in bootloader.\r\n");

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

    /* Initialize Bootloader CAN core and Flash subsystem */
    openbl_core_init(session_selected);

    /* -------------------------------------------------------------------------
     * Phase A: Normal Boot - Listen on CAN2 for 300 ms
     * ------------------------------------------------------------------------- */
    if (mode == BOOT_MODE_NORMAL)
    {
        safe_printf(TAG, "[NORMAL] Listening for CAN commands on 0x7DF / 0x7E0 (300 ms window)...\r\n");

        uint32_t loops = 0U;
        const uint32_t max_loops = 3000U; /* 3000 iterations with 100 us delay each ~ 300 ms */

        while (loops < max_loops)
        {
            openbl_core_poll();

            /* If PC connected and selected target, switch to Stay-In-Boot mode */
            if (openbl_core_is_session_active())
            {
                mode = BOOT_MODE_STAY_IN_BOOT;
                safe_printf(TAG, "[NORMAL] Host connection established! Cancelling boot timeout.\r\n");
                break;
            }

            delay_cycles(14400U); /* ~100 us @ 144 MHz */
            loops++;
        }

        /* If no connection established, verify application integrity and jump */
        if (mode == BOOT_MODE_NORMAL)
        {
            if (boot_is_app_valid(BOOT_APP_START_ADDR))
            {
                safe_printf(TAG, "[JUMP] Valid Application detected at 0x%08lX! Booting...\r\n", BOOT_APP_START_ADDR);

                /* Increment boot fail counter to detect crash loops before healthy confirmation */
                g_boot_shared.boot_fail_counter++;
                boot_shared_update_crc();

                /* Delay briefly to flush UART transmitter */
                delay_cycles(1440000U); /* ~10 ms */

                boot_jump_to_application(BOOT_APP_START_ADDR);
            }
            else
            {
                safe_printf(TAG, "[ERROR] Application at 0x%08lX is EMPTY or INVALID! Entering Recovery Mode.\r\n", BOOT_APP_START_ADDR);
                mode = BOOT_MODE_RECOVERY;
            }
        }
    }

    /* -------------------------------------------------------------------------
     * Phase B: Stay in Bootloader or Recovery Mode
     * ------------------------------------------------------------------------- */
    uint32_t led_ticker = 0U;
    uint32_t beacon_ticker = 0U;

    while (1)
    {
        openbl_core_poll();

        led_ticker++;
        beacon_ticker++;

        /* Fast alert LED blink (Recovery = 5 Hz, Update = 2 Hz) */
        uint32_t led_period = (mode == BOOT_MODE_RECOVERY) ? 500U : 1500U;
        if (led_ticker >= led_period)
        {
            bsp_io_output_setPin(!bsp_io_output_get_state());
            led_ticker = 0U;
        }

        /* Emit recovery crash beacon every 1000 ms if in recovery mode */
        if (mode == BOOT_MODE_RECOVERY)
        {
            if (beacon_ticker >= 10000U) /* ~1000 ms */
            {
                openbl_core_send_recovery_beacon((uint8_t)g_boot_shared.fault_reason,
                                                (uint8_t)g_boot_shared.boot_fail_counter,
                                                g_boot_shared.fault_pc);
                beacon_ticker = 0U;
            }
        }

        delay_cycles(1440U); /* ~10 us */
    }

    return 0;
}
