/**
 * @file openbl_core.h
 * @brief Core command processor for STM32C5 CAN Bootloader.
 */

#ifndef OPENBL_CORE_H
#define OPENBL_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "boot_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the Bootloader CAN subsystem, ISO-TP engine, and Flash interface.
 * @param[in] session_selected_init Initial state of target selection (e.g. from shared RAM).
 */
void openbl_core_init(bool session_selected_init);

/**
 * @brief Polls FDCAN2 RX FIFO, processes received CAN frames, and updates ISO-TP state.
 * @note Must be called periodically in the Bootloader main loop.
 */
void openbl_core_poll(void);

/**
 * @brief Checks if a PC updater session has actively selected this node.
 * @return true if selected, false otherwise.
 */
bool openbl_core_is_session_active(void);

/**
 * @brief Transmits a recovery crash beacon on ID 0x7E9.
 * @param[in] reason Recorded fault reason code.
 * @param[in] fail_cnt Number of consecutive boot crashes.
 * @param[in] pc Program counter at crash point.
 */
void openbl_core_send_recovery_beacon(uint8_t reason, uint8_t fail_cnt, uint32_t pc);

#ifdef __cplusplus
}
#endif

#endif /* OPENBL_CORE_H */
