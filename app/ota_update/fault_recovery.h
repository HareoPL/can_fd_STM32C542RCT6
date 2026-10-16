/**
 * @file fault_recovery.h
 * @brief Anti-brick fault detection and crash loop recovery for STM32C5 Main Application.
 */

#ifndef FAULT_RECOVERY_H
#define FAULT_RECOVERY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Confirms that the application has booted successfully and ran stably for >= 3 seconds.
 * @details Resets boot_fail_counter to 0 in shared RAM, proving that the firmware is healthy.
 */
void fault_recovery_confirm_app_healthy(void);

/**
 * @brief Explicitly records an RTOS assertion failure and resets the system into Bootloader.
 * @param[in] file Source file where assertion occurred.
 * @param[in] line Line number of failed assertion.
 */
void fault_recovery_on_assert(const char *file, uint32_t line);

#ifdef __cplusplus
}
#endif

#endif /* FAULT_RECOVERY_H */
