/**
 * @file sim_v_box.c
 * @brief Car simulator V_BOX: the v-box telemetry frame 0x01F2F3F4 and the relay command
 *        0x01F2F3F0, byte for byte as before the car simulators (device parser 192, D1).
 *
 * Telemetry frame 0x01F2F3F4: data[0] = SW0..SW7, data[1] = fuel 0..100 %, data[2] = RPM / 3
 * (raw inputs, without the D13 rule), data[3] = 0, data[4..7] = frame counter, big endian.
 * Relay command 0x01F2F3F0: data[2] & 0x01 = 1 closes, 0 opens the lock relay; frames shorter
 * than 3 bytes are ignored.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#include "sim_v_box.h"
#include "io_sim.h"
#include <stddef.h>

/** @brief Extended 29-bit CAN ID of the telemetry frame. */
#define SIM_V_BOX_TELEMETRY_ID 0x01F2F3F4U
/** @brief Extended 29-bit CAN ID of the relay command. */
#define SIM_V_BOX_RELAY_CMD_ID 0x01F2F3F0U

/** @brief Telemetry frame counter (data[4..7]), reset when the simulator is selected. */
static uint32_t s_counter = 0U;

/**
 * @brief Encodes the telemetry frame (logic moved 1:1 from io_manager_canCreateFrame()).
 * @param[in] vs Vehicle state.
 * @param[out] data Frame payload.
 */
static void encode_telemetry(const vehicle_state_t *vs, uint8_t data[CAR_SIM_DLC_MAX])
{
    data[0] = (uint8_t)(vs->btn & 0xFFU);
    data[1] = vs->fuel_pct;
    data[2] = io_sim_rpmToByte(vs->rpm_in);
    data[3] = 0U;
    data[4] = (uint8_t)(s_counter >> 24);
    data[5] = (uint8_t)(s_counter >> 16);
    data[6] = (uint8_t)(s_counter >> 8);
    data[7] = (uint8_t)(s_counter & 0xFFU);
    s_counter++;
}

/** @brief Resets the telemetry frame counter. */
static void on_select(void)
{
    s_counter = 0U;
}

/** @brief Telemetry frame. */
static const sim_frame_t s_tx[] = {
    {
        .id        = SIM_V_BOX_TELEMETRY_ID,
        .ext       = true,
        .dlc       = 8U,
        .period_ms = 100U,
        .encode    = encode_telemetry,
    },
};

/** @brief Relay command: data[2] & 0x01 selects the target state, no PIN. */
static const sim_cmd_t s_rx[] = {
    {
        .id         = SIM_V_BOX_RELAY_CMD_ID,
        .ext        = true,
        .match      = { 0x00U, 0x00U, 0x01U },
        .match_mask = { 0x00U, 0x00U, 0x01U },
        .pin_byte   = -1,
        .action     = SIM_ACT_LOCK,
    },
    {
        .id         = SIM_V_BOX_RELAY_CMD_ID,
        .ext        = true,
        .match      = { 0x00U, 0x00U, 0x00U },
        .match_mask = { 0x00U, 0x00U, 0x01U },
        .pin_byte   = -1,
        .action     = SIM_ACT_UNLOCK,
    },
};

/** @brief Device parser numbers (D1: 192 is the default device parser). */
static const uint16_t s_dev_parsers[] = { 192U };

/** @brief Bus configuration: CAN1 Classic 500 kbps. */
static const can_config_t s_bus_config = {
    .mode            = CAN_MODE_CLASSIC,
    .baudrate_normal = CAN_BAUDRATE_500K,
    .baudrate_FD     = CAN_BAUDRATE_CUSTOM
};

/** @brief Immutable V_BOX profile. */
static const car_sim_profile_t s_profile = {
    .key             = "V_BOX",
    .name            = "telemetria v-box",
    .dev_parsers     = s_dev_parsers,
    .num_dev_parsers = (uint8_t)(sizeof(s_dev_parsers) / sizeof(s_dev_parsers[0])),
    .bus_config      = &s_bus_config,
    .tx              = s_tx,
    .num_tx          = (uint8_t)(sizeof(s_tx) / sizeof(s_tx[0])),
    .rx              = s_rx,
    .num_rx          = (uint8_t)(sizeof(s_rx) / sizeof(s_rx[0])),
    .rpm_max         = IO_RPM_MAX,
    .rpm_step        = IO_RPM_PER_BIT,
    .idle_rpm        = IO_RPM_ENGINE_ON,
    .fob             = NULL,
    .on_select       = on_select
};

const car_sim_profile_t *sim_v_box_profile_get(void)
{
    return &s_profile;
}
