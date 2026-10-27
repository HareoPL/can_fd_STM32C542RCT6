/**
 * @file sim_none.c
 * @brief Car simulator BRAK: marker frame for a device parser number without a car simulator (D4).
 *
 * The frames of the previous car would make the tested device parser look broken, so v-box stops
 * them and sends only the marker frame. The extended ID belongs to the v-box family and never
 * collides with the 11-bit IDs of a car.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#include "sim_none.h"
#include "io_sim.h"
#include <stddef.h>

/** @brief Extended 29-bit CAN ID of the marker frame. */
#define SIM_NONE_MARKER_ID 0x01F2F3FFU

/** @brief Marker frame CA FE 00 BA BE 00. */
static const sim_frame_t s_tx[] = {
    {
        .id        = SIM_NONE_MARKER_ID,
        .ext       = true,
        .dlc       = 6U,
        .period_ms = 1000U,
        .init      = { 0xCAU, 0xFEU, 0x00U, 0xBAU, 0xBEU, 0x00U },
    },
};

/** @brief Bus configuration: CAN1 Classic 500 kbps. */
static const can_config_t s_bus_config = {
    .mode            = CAN_MODE_CLASSIC,
    .baudrate_normal = CAN_BAUDRATE_500K,
    .baudrate_FD     = CAN_BAUDRATE_CUSTOM
};

/** @brief Immutable BRAK profile (RPM limits as V_BOX). */
static const car_sim_profile_t s_profile = {
    .key             = "BRAK",
    .name            = "brak symulatora",
    .dev_parsers     = NULL,
    .num_dev_parsers = 0U,
    .bus_config      = &s_bus_config,
    .tx              = s_tx,
    .num_tx          = (uint8_t)(sizeof(s_tx) / sizeof(s_tx[0])),
    .rx              = NULL,
    .num_rx          = 0U,
    .rpm_max         = IO_RPM_MAX,
    .rpm_step        = IO_RPM_PER_BIT,
    .idle_rpm        = IO_RPM_ENGINE_ON,
    .fob             = NULL,
    .on_select       = NULL
};

const car_sim_profile_t *sim_none_profile_get(void)
{
    return &s_profile;
}
