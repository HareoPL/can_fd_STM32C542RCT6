/**
 * @file boot_shared.h
 * @brief Shared RAM structure between Main Application and Bootloader for STM32C5.
 * @details Located in the dedicated .noinit.boot_shared section at 0x2000FFC0 (64 bytes).
 *          Preserves boot flags, crash diagnostics (HardFault), and session state across resets.
 */

#ifndef BOOT_SHARED_H
#define BOOT_SHARED_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Magic value indicating a valid request to enter Bootloader mode */
#define BOOT_SHARED_MAGIC               0xB00710ADUL

/** @brief Maximum consecutive boot failures before triggering Recovery Mode */
#define BOOT_MAX_FAIL_COUNTER           3U

/** @brief Application Flash base address */
#define BOOT_APP_START_ADDR             0x08008000UL

/** @brief Application Flash end address (224 KB region) */
#define BOOT_APP_END_ADDR               0x08040000UL

/**
 * @brief Bootloader fault reasons recorded during crashes.
 */
typedef enum
{
    BOOT_FAULT_NONE         = 0x00U, /**< Normal system startup or graceful restart */
    BOOT_FAULT_HARDFAULT    = 0x01U, /**< Hardware exception (HardFault / BusFault / MemManage) */
    BOOT_FAULT_RTOS_ASSERT  = 0x02U, /**< FreeRTOS configASSERT failed */
    BOOT_FAULT_WATCHDOG     = 0x03U  /**< Independent Watchdog (IWDG) timeout reset */
} boot_fault_reason_t;

/**
 * @brief Bootloader operation mode.
 */
typedef enum
{
    BOOT_MODE_NORMAL        = 0x00U, /**< Standard boot: listen 300 ms then jump to app */
    BOOT_MODE_STAY_IN_BOOT  = 0x01U, /**< Requested by App via CAN: stay in bootloader */
    BOOT_MODE_RECOVERY      = 0x02U  /**< Anti-brick recovery mode: 3 consecutive crashes */
} boot_mode_t;

/**
 * @brief Shared RAM structure across resets.
 * @note Size must be exactly 64 bytes (16 words) to fit in SHARED_RAM at 0x2000FFC0.
 */
typedef struct
{
    uint32_t magic;                 /**< Magic value (BOOT_SHARED_MAGIC) */
    uint32_t session_selected;      /**< 1 if node was already selected by UID in App */
    uint32_t boot_fail_counter;     /**< Consecutive crash counter before healthy confirmation */
    uint32_t fault_reason;          /**< Fault reason code (boot_fault_reason_t) */
    uint32_t fault_pc;              /**< Program Counter at crash point */
    uint32_t fault_cfsr;            /**< Configurable Fault Status Register (SCB->CFSR) */
    uint32_t reserved[9];           /**< Reserved for future expansion / 64-byte padding */
    uint32_t crc32;                 /**< IEEE 802.3 CRC32 of preceding 15 words */
} boot_shared_ram_t;

/** @brief Shared structure instance placed in .noinit.boot_shared section */
extern volatile boot_shared_ram_t g_boot_shared;

/**
 * @brief Validates the integrity of the shared RAM structure using CRC32.
 * @return true if CRC32 matches, false otherwise.
 */
bool boot_shared_is_valid(void);

/**
 * @brief Updates the CRC32 field of the shared structure.
 */
void boot_shared_update_crc(void);

/**
 * @brief Resets shared structure to clean default values and updates CRC32.
 */
void boot_shared_clear(void);

/**
 * @brief Sets the enter-bootloader request flag and resets the MCU.
 * @param[in] session_selected Whether the CAN session is already selected.
 */
void boot_shared_request_bootloader(bool session_selected);

/**
 * @brief Records a system fault and updates CRC32 before reset.
 * @param[in] reason Fault reason code.
 * @param[in] pc Program Counter at fault.
 * @param[in] cfsr SCB->CFSR status register.
 */
void boot_shared_record_fault(boot_fault_reason_t reason, uint32_t pc, uint32_t cfsr);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_SHARED_H */
