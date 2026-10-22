/**
 * @file cp_v_box.c
 * @brief Implementation of CAN parser profile: V-Box telemetry and relay handler.
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-13
 */

#include "cp_v_box.h"
#include "safe_printf.h"
#include "can_msg_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "bsp_io.h"
#include "io_manager.h"

/** @brief Logging tag for the v-box parser profile. */
#define TAG "CP_V_BOX"

/** @brief Extended 29-bit CAN ID for external relay commands. */
#define CAN_ID_RELAY_CMD    0x01F2F3F0


/* --- Private Callbacks -----------------------------------------------------*/

/**
 * @brief Handler callback for incoming relay commands (Extended ID: 0x01F2F3F0).
 * @param[in] msg Pointer to received CAN message.
 */
static void on_v_box_relay_cmd_rx(const CAN_msg_t *msg)
{
    if (msg == NULL || msg->len < 3U)
    {
        return;
    }

    uint8_t target_state = ((msg->data[2] & (1U << 0U)) != 0U) ? 1U : 0U;

    bool ok = io_manager_setRelayStateAsync(target_state);

    safe_printf(TAG, "RELAY CMD (ID=0x%08lX, len=%u): target=%u, %s\r\n",
                (unsigned long)(msg->ident & CAN_EFF_MASK),
                (unsigned int)msg->len,
                (unsigned int)target_state,
                ok ? "QUEUED" : "QUEUE FULL");
}

static void on_v_box_test(const CAN_msg_t *msg)
{
    if (msg == NULL)
    {
        return;
    }
    

        safe_printf(TAG, "testowe id \r\n");
       
}

/* --- Private Profile Definition --------------------------------------------*/

/** @brief Internal filter table for V-Box profile. */
static const can_filter_t s_filters[] = {
    {
        .arb  = CAN_ID_RELAY_CMD,           /**< 11-bit Standard ID 0x7FF */
        .mask = CAN_MASK_EXACT_EXTENDED, // CAN_MASK_EXACT_STANDARD,    /**< Exact match */
        .cb   = on_v_box_relay_cmd_rx
    },
    {
        .arb  = 0x01F2F3F0,           /**< 11-bit Standard ID 0x7FF */
        .mask = CAN_MASK_EXACT_EXTENDED,    /**< Exact match */
        .cb   = on_v_box_test
    }
};

/**
 * @brief Bus configuration for V-Box profile.
 *        CAN FD with BRS example: .mode = CAN_MODE_FD_BRS, .baudrate_FD = CAN_BAUDRATE_2000K
 *        CAN FD without BRS:      .mode = CAN_MODE_FD_NO_BRS (baudrate_FD ignored)
 */
static const can_config_t s_bus_config = {
    .mode            = CAN_MODE_CLASSIC,
    .baudrate_normal = CAN_BAUDRATE_500K,
    .baudrate_FD     = CAN_BAUDRATE_CUSTOM
};

/** @brief Immutable profile descriptor for V-Box profile. */
static const can_parser_profile_t s_profile = {
    .bus_config  = &s_bus_config,
    .filters     = s_filters,
    .num_filters = (uint8_t)(sizeof(s_filters) / sizeof(s_filters[0]))
};

/* --- Public API ------------------------------------------------------------*/

const can_parser_profile_t *cp_v_box_profile_get(void)
{
    return &s_profile;
}

