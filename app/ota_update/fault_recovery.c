/**
 * @file fault_recovery.c
 * @brief Exception interception, PC capture, and crash recovery for STM32C5.
 */

#include "fault_recovery.h"
#include "boot_shared.h"
#include "stm32_hal.h"

/**
 * @brief C-level fault diagnostic logger called from assembly exception stubs.
 * @param[in] stacked_regs Pointer to exception stack frame.
 */
void prv_hardfault_c_handler(uint32_t *stacked_regs)
{
    uint32_t stacked_pc = stacked_regs[6];
    uint32_t cfsr = SCB->CFSR;

    boot_shared_record_fault(BOOT_FAULT_HARDFAULT, stacked_pc, cfsr);

    NVIC_SystemReset();
    while (1)
    {
        __NOP();
    }
}

/**
 * @brief Naked HardFault_Handler extracting MSP or PSP and forwarding to C handler.
 */
void HardFault_Handler(void) __attribute__((naked));
void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4            \n"
        "ite eq                \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "b prv_hardfault_c_handler \n"
    );
}

void MemManage_Handler(void) __attribute__((naked));
void MemManage_Handler(void)
{
    __asm volatile(
        "tst lr, #4            \n"
        "ite eq                \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "b prv_hardfault_c_handler \n"
    );
}

void BusFault_Handler(void) __attribute__((naked));
void BusFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4            \n"
        "ite eq                \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "b prv_hardfault_c_handler \n"
    );
}

void UsageFault_Handler(void) __attribute__((naked));
void UsageFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4            \n"
        "ite eq                \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "b prv_hardfault_c_handler \n"
    );
}

void fault_recovery_confirm_app_healthy(void)
{
    if (boot_shared_is_valid())
    {
        g_boot_shared.boot_fail_counter = 0U;
        g_boot_shared.fault_reason = (uint32_t)BOOT_FAULT_NONE;
        boot_shared_update_crc();
    }
}

void fault_recovery_on_assert(const char *file, uint32_t line)
{
    (void)file;
    boot_shared_record_fault(BOOT_FAULT_RTOS_ASSERT, line, 0U);
    NVIC_SystemReset();
}
