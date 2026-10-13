/**
 * @file cp_example.c
 * @brief Implementation of CAN parser profile: Example diagnostic and calibration handler.
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-13
 */

#include "cp_example.h"
#include "safe_printf.h"
#include "can_msg_types.h"
#include <stddef.h>

/** @brief Logging tag for the example parser profile. */
#define TAG "CP_EXAMPLE"

/** @brief Standard 11-bit CAN ID for diagnostic requests (e.g. OBD2/UDS). */
#define CAN_ID_DIAG_REQ     0x7E0U

/** @brief Extended 29-bit CAN ID for calibration requests. */
#define CAN_ID_CALIB_REQ    0x18DAF110U

/* --- Private Callbacks -----------------------------------------------------*/

/**
 * @brief Handler callback for diagnostic requests (Standard ID: 0x7E0).
 * @param[in] msg Pointer to received CAN message.
 */
static void on_example_diag_req_rx(const CAN_msg_t *msg)
{
    if (msg == NULL)
    {
        return;
    }

    safe_printf(TAG, "[EXAMPLE] DIAG REQ (ID=0x%03lX, len=%u)\r\n",
                (unsigned long)CAN_msg_get_can_id(msg->ident),
                (unsigned int)msg->len);
}

/**
 * @brief Handler callback for calibration frames (Extended ID: 0x18DAF110).
 * @param[in] msg Pointer to received CAN message.
 */
static void on_example_calib_req_rx(const CAN_msg_t *msg)
{
    if (msg == NULL)
    {
        return;
    }

    safe_printf(TAG, "[EXAMPLE] CALIB REQ (ID=0x%08lX, len=%u)\r\n",
                (unsigned long)CAN_msg_get_can_id(msg->ident),
                (unsigned int)msg->len);
}

/* --- Private Profile Definition --------------------------------------------*/

/** @brief Internal filter table for Example profile. */
static const can_filter_t s_filters[] = {
    {
        .arb  = CAN_ID_DIAG_REQ,            /**< 11-bit Standard ID 0x7E0 */
        .mask = CAN_MASK_EXACT_STANDARD,    /**< Exact match */
        .cb   = on_example_diag_req_rx
    },
    {
        .arb  = CAN_ID_CALIB_REQ,           /**< 29-bit Extended ID 0x18DAF110 */
        .mask = CAN_MASK_EXACT_EXTENDED,    /**< Exact match */
        .cb   = on_example_calib_req_rx
    }
};

/** @brief Immutable profile descriptor for Example profile. */
static const can_parser_profile_t s_profile = {
    .filters     = s_filters,
    .num_filters = (uint8_t)(sizeof(s_filters) / sizeof(s_filters[0]))
};

/* --- Public API ------------------------------------------------------------*/

const can_parser_profile_t *cp_example_profile_get(void)
{
    return &s_profile;
}

