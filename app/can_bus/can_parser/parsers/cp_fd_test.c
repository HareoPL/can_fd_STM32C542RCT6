/**
 * @file cp_fd_test.c
 * @brief Implementation of CAN parser profile: CAN FD bus test.
 *        Unlike production profiles the bus configuration is mutable, so the test module
 *        can switch mode and bit rates through the regular can_parser_select() path.
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-22
 */

#include "cp_fd_test.h"
#include "can_fd_test.h"
#include "can_msg_types.h"
#include <stddef.h>

/* --- Private Callbacks -----------------------------------------------------*/

/**
 * @brief Callback for test command frames (0x01F2F400).
 * @param[in] msg Pointer to received CAN message structure.
 */
static void on_fd_test_cmd_rx(const CAN_msg_t *msg)
{
    can_fd_test_on_command(msg);
}

/**
 * @brief Callback for echo request frames (0x01F2F410 extended, 0x410 standard).
 * @param[in] msg Pointer to received CAN message structure.
 */
static void on_fd_test_echo_rx(const CAN_msg_t *msg)
{
    can_fd_test_on_echo(msg);
}

/**
 * @brief Callback for CAN1 -> CAN2 loop requests (0x01F2F420).
 * @param[in] msg Pointer to received CAN message structure.
 */
static void on_fd_test_loop_req_rx(const CAN_msg_t *msg)
{
    can_fd_test_on_loop_request(msg);
}

/**
 * @brief Callback for CAN2 -> CAN1 loop responses (0x01F2F421).
 * @param[in] msg Pointer to received CAN message structure.
 */
static void on_fd_test_loop_resp_rx(const CAN_msg_t *msg)
{
    can_fd_test_on_loop_response(msg);
}

/* --- Private Profile Definition --------------------------------------------*/

static const can_filter_t s_filters[] = {
    {
        .arb  = CAN_FD_TEST_ID_CMD,
        .mask = CAN_MASK_EXACT_EXTENDED,
        .cb   = on_fd_test_cmd_rx
    },
    {
        .arb  = CAN_FD_TEST_ID_ECHO_EXT,
        .mask = CAN_MASK_EXACT_EXTENDED,
        .cb   = on_fd_test_echo_rx
    },
    {
        .arb  = CAN_FD_TEST_ID_ECHO_STD,
        .mask = CAN_MASK_EXACT_STANDARD,
        .cb   = on_fd_test_echo_rx
    },
    {
        .arb  = CAN_FD_TEST_ID_LOOP_REQ,
        .mask = CAN_MASK_EXACT_EXTENDED,
        .cb   = on_fd_test_loop_req_rx
    },
    {
        .arb  = CAN_FD_TEST_ID_LOOP_RESP,
        .mask = CAN_MASK_EXACT_EXTENDED,
        .cb   = on_fd_test_loop_resp_rx
    }
};

/** @brief Mutable bus configuration, changed by the test module before each select. */
static can_config_t s_bus_config = {
    .mode            = CAN_MODE_CLASSIC,
    .baudrate_normal = CAN_BAUDRATE_500K,
    .baudrate_FD     = CAN_BAUDRATE_CUSTOM
};

/** @brief Profile descriptor (filters immutable, bus configuration points to mutable storage). */
static const can_parser_profile_t s_profile = {
    .bus_config  = &s_bus_config,
    .filters     = s_filters,
    .num_filters = (uint8_t)(sizeof(s_filters) / sizeof(s_filters[0]))
};

/* --- Public API ------------------------------------------------------------*/

const can_parser_profile_t *cp_fd_test_profile_get(void)
{
    return &s_profile;
}

void cp_fd_test_bus_config_set(const can_config_t *config)
{
    if (config != NULL)
    {
        s_bus_config = *config;
    }
}
