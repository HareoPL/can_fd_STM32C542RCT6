/**
 * @file boot_jump.h
 * @brief Application integrity verification and hardware jump procedure for Cortex-M33.
 */

#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Validates if a valid application image resides at the target Flash address.
 * @details Checks that:
 *          1. Initial MSP address is located within SRAM boundaries (0x20000000 - 0x20010000).
 *          2. Reset_Handler address points inside valid application Flash and has Thumb bit set.
 * @param[in] app_address Start address of the application (e.g. 0x08008000).
 * @return true if valid application signature is detected, false otherwise.
 */
bool boot_is_app_valid(uint32_t app_address);

/**
 * @brief Performs a clean hardware shutdown and executes a jump to the application.
 * @details De-initializes active peripherals (FDCAN2, USART2), disables all NVIC interrupts,
 *          resets SysTick, clears MSPLIM/PSPLIM (ARM Cortex-M33 specific), sets SCB->VTOR,
 *          loads the new MSP, and branches to the Application Reset_Handler.
 * @param[in] app_address Base address of the application vector table.
 * @note This function never returns.
 */
void boot_jump_to_application(uint32_t app_address);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_JUMP_H */
