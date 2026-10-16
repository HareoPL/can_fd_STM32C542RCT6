/**
 * @file can_parser.h
 * @brief CAN message parser and filter route dispatcher module.
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-13
 */

#ifndef CAN_PARSER_H
#define CAN_PARSER_H

#include <stdint.h>
#include <stdbool.h>
#include "can_port.h"
#include "can_msg_types.h"
#include "can_manager.h"

/**
 * @brief Identifiers for available CAN parser profiles.
 */
typedef enum
{
    CAN_PARSER_ID_V_BOX = 0, /**< Parser Profile: V-Box telemetry & relay */
    CAN_PARSER_ID_EXAMPLE,   /**< Parser Profile: Example diagnostic & calibration */
    CAN_PARSER_ID_BOOT_OTA,   /**< Parser Profile: boot_ota handler */

    CAN_PARSER_COUNT
} can_parser_id_t;

/**
 * @brief Parser profile descriptor holding acceptance filters and rules count.
 */
typedef struct
{
    const can_filter_t *filters; /**< Pointer to profile filter table */
    uint8_t num_filters;         /**< Number of filter rules in the profile */
} can_parser_profile_t;

/**
 * @brief Dynamically selects and activates a CAN parser profile by its numerical ID.
 *        Configures hardware filters in M_CAN Message RAM and updates software dispatch callbacks.
 * @param[in,out] can_inst Pointer to target CAN instance (e.g. &can1_inst).
 * @param[in] id Parser profile identifier (CAN_PARSER_ID_V_BOX, CAN_PARSER_ID_EXAMPLE).
 * @return true on success, false on invalid parameter or hardware configuration error.
 */
bool can_parser_select(can_instance_t *can_inst, can_parser_id_t id);

/**
 * @brief Retrieves the currently active parser profile identifier.
 * @return Current can_parser_id_t, or CAN_PARSER_COUNT if none active.
 */
can_parser_id_t can_parser_get_current_id(void);

#endif /* CAN_PARSER_H */
