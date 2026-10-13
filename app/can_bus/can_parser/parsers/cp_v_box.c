/**
 * @file cp_v_box.c
 * @brief Implementation of CAN parser profile: V-Box telemetry and relay handler.
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-13
 */

#include "cp_v_box.h"
#include "safe_printf.h"
#include "can_msg_types.h"
#include <stddef.h>
#include <stdint.h>
#include "bsp_io.h"

/** @brief Logging tag for the v-box parser profile. */
#define TAG "CP_V_BOX"

/** @brief Standard 11-bit CAN ID for external relay commands. */
#define CAN_ID_RELAY_CMD    0x01F2F3F4
// #define CAN_ID_RELAY_CMD    0x7FFU


/* --- Private Callbacks -----------------------------------------------------*/

/**
 * @brief Handler callback for incoming relay commands (Standard ID: 0x7FF).
 * @param[in] msg Pointer to received CAN message.
 */
static void on_v_box_relay_cmd_rx(const CAN_msg_t *msg)
{
    if (msg == NULL)
    {
        return;
    }
    static uint8_t relay_prevState = 0xFFU;
    static uint8_t relay_state = 0xFFU;

    if (msg->len > 0U)
    {
        relay_state = ((msg->data[2] & (1 << 0)) != 0);

        if(relay_state != relay_prevState)
        {
            relay_prevState = relay_state;
            bsp_io_output_setPin(relay_state);
            
            safe_printf(TAG, "RELAY CMD OK\r\n");
        }

        safe_printf(TAG, "RELAY CMD (ID=0x%03lX, len=%u): state=%u\r\n",
                (unsigned long)CAN_msg_get_can_id(msg->ident),
                (unsigned int)msg->len,
                (unsigned int)relay_state);
    }
   
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

/** @brief Immutable profile descriptor for V-Box profile. */
static const can_parser_profile_t s_profile = {
    .filters     = s_filters,
    .num_filters = (uint8_t)(sizeof(s_filters) / sizeof(s_filters[0]))
};

/* --- Public API ------------------------------------------------------------*/

const can_parser_profile_t *cp_v_box_profile_get(void)
{
    return &s_profile;
}

