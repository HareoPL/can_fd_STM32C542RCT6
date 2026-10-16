/**
 * @file ota_manager.h
 * @brief OTA update manager and dummy verification engine for FDCAN2.
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include "can_manager.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the OTA update manager and spawns its asynchronous worker task.
 * @param[in] can2_inst Pointer to initialized CAN2 instance.
 * @return true on success, false on initialization error.
 */
bool ota_manager_init(can_instance_t *can2_inst);

/**
 * @brief Feeds an incoming CAN frame from FDCAN2 into the non-blocking OTA worker queue.
 *        Called directly from cp_boot_ota callbacks in can_rx_task context (< 1 microsecond).
 * @param[in] msg Pointer to received CAN message structure.
 */
void ota_manager_feed_can_frame(const CAN_msg_t *msg);

/**
 * @brief Checks if this node is currently selected by UID in an active OTA session.
 * @return true if target is selected, false otherwise.
 */
bool ota_manager_is_target_selected(void);

/**
 * @brief Retrieves the 96-bit (12-byte) factory hardware Unique Device ID (UID).
 * @return Pointer to 12-byte UID buffer.
 */
const uint8_t *ota_manager_get_uid(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_MANAGER_H */
