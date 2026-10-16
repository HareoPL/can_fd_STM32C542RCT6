/**
 * @file boot_jump.c
 * @brief Application jump implementation for ARM Cortex-M33 (STM32C542).
 */

#include "boot_jump.h"
#include "boot_shared.h"
#include "stm32_hal.h"
#include "mx_fdcan1.h"
#include "mx_fdcan2.h"
#include "mx_usart2.h"
#include "mx_tim6.h"
#include "mx_rcc.h"
#include "mx_icache.h"

bool boot_is_app_valid(uint32_t app_address)
{
    if (app_address != BOOT_APP_START_ADDR)
    {
        return false;
    }

    uint32_t app_msp = *(volatile uint32_t *)app_address;
    uint32_t app_entry = *(volatile uint32_t *)(app_address + 4U);

    /* Verify initial MSP is within SRAM range (0x20000000 to 0x20010000) */
    if ((app_msp < 0x20000000UL) || (app_msp > 0x20010000UL))
    {
        return false;
    }

    /* Verify Reset_Handler points inside application Flash region (0x08008001 to 0x0803FFFF) */
    if ((app_entry < (BOOT_APP_START_ADDR + 1U)) || (app_entry >= BOOT_APP_END_ADDR))
    {
        return false;
    }

    /* In ARM Cortex-M, entry point addresses must have the Thumb bit set (bit 0 = 1) */
    if ((app_entry & 1U) == 0U)
    {
        return false;
    }

    return true;
}

void boot_jump_to_application(uint32_t app_address)
{
    /* De-initialize peripherals used by bootloader */
    mx_fdcan2_deinit();
    mx_fdcan1_deinit();
    mx_usart2_uart_deinit();

    /* Stop and reset ICACHE */
    HAL_ICACHE_DeInit(mx_icache_gethandle());

    /* Reset peripheral buses and restore default system clock (HSIDIV3, PSI/PLL disabled) */
    (void)HAL_DeInit();
    mx_rcc_deinit();
    mx_tim6_deinit();

    __disable_irq();

    /* Disable and clear SysTick timer */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL  = 0U;

    /* Disable all NVIC interrupts and clear all pending interrupt requests */
    for (uint32_t i = 0U; i < 8U; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    /* Relocate Vector Table to Application Base Address */
    SCB->VTOR = app_address;
    __DSB();
    __ISB();

    /* Crucial on ARMv8-M / Cortex-M33: clear stack limit registers before modifying MSP */
    __set_MSPLIM(0U);
    __set_PSPLIM(0U);

    /* Read application MSP and Reset_Handler */
    uint32_t app_msp = *(volatile uint32_t *)app_address;
    uint32_t app_entry = *(volatile uint32_t *)(app_address + 4U);

    /* Ensure privileged mode with MSP stack */
    __set_CONTROL(0U);
    __ISB();

    /* Execute atomic inline assembly jump to Application Reset_Handler */
    __asm volatile (
        "cpsie i        \n"  /* Enable interrupts (standard reset state) */
        "msr   msp, %0  \n"  /* Set Application Main Stack Pointer */
        "bx    %1       \n"  /* Branch to Application Reset_Handler */
        :
        : "r" (app_msp), "r" (app_entry)
        : "memory"
    );

    /* Unreachable code: trap if application handler unexpectedly returns */
    while (1)
    {
        __NOP();
    }
}
