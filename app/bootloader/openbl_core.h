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
 * @brief Initializes the Bootloader CAN subsystem (FDCAN1 + FDCAN2), ISO-TP engine, and Flash interface.
 * @param[in] session_selected_init Initial state of target selection (e.g. from shared RAM).
 * @param[in] boot_mode Bootloader operation mode (BOOT_MODE_STAY_IN_BOOT or BOOT_MODE_RECOVERY).
 * @param[in] active_can_bus Initial active CAN bus (BOOT_CAN_BUS_AUTO, BOOT_CAN_BUS_FDCAN1, or BOOT_CAN_BUS_FDCAN2).
 */
void openbl_core_init(bool session_selected_init, boot_mode_t boot_mode, uint8_t active_can_bus);

/**
 * @brief Polls FDCAN1 and FDCAN2 RX FIFOs, processes received CAN frames, and updates ISO-TP state.
 * @note Must be called periodically in the Bootloader main loop.
 */
void openbl_core_poll(void);

/**
 * @brief Checks if a PC updater session has actively selected this node.
 * @return true if selected, false otherwise.
 */
bool openbl_core_is_session_active(void);

/**
 * @brief Transmits an on-demand recovery crash beacon on Extended ID 0x01F2F3E9.
 * @param[in] reason Recorded fault reason code.
 * @param[in] fail_cnt Number of consecutive boot crashes.
 * @param[in] pc Program counter at crash point.
 */
void openbl_core_send_recovery_beacon(uint8_t reason, uint8_t fail_cnt, uint32_t pc);

#ifdef __cplusplus
}
#endif

#endif /* OPENBL_CORE_H */
