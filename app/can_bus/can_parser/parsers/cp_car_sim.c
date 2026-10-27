/**
 * @file cp_car_sim.c
 * @brief CAN parser profile of the active car simulator: bus configuration of the car and one
 *        exact filter per unique command ID of its rx table, dispatched to car_sim_on_rx().
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#include "cp_car_sim.h"
#include "car_sim.h"
#include <stddef.h>

/** @brief Marker returned by cp_car_sim_filter_count() for an invalid filter table. */
#define CP_CAR_SIM_INVALID 0xFFU

/** @brief Filters of the active car profile. */
static can_filter_t s_filters[CAN_MAX_FILTERS];

/** @brief Bus configuration of the active car profile. */
static can_config_t s_bus_config = {
    .mode            = CAN_MODE_CLASSIC,
    .baudrate_normal = CAN_BAUDRATE_500K,
    .baudrate_FD     = CAN_BAUDRATE_CUSTOM
};

/** @brief Profile descriptor (rebuilt by cp_car_sim_set_profile()). */
static can_parser_profile_t s_profile = {
    .bus_config  = &s_bus_config,
    .filters     = s_filters,
    .num_filters = 0U
};

/**
 * @brief Collects the unique command IDs of a profile as exact filters.
 * @param[in] p Car profile.
 * @param[out] out Filter table (may be NULL to count only).
 * @return Number of filters, CP_CAR_SIM_INVALID if invalid or above CAN_MAX_FILTERS.
 */
static uint8_t build_filters(const car_sim_profile_t *p, can_filter_t *out)
{
    uint8_t n = 0U;
    can_filter_t f[CAN_MAX_FILTERS];

    for (uint8_t i = 0U; (p->rx != NULL) && (i < p->num_rx); i++)
    {
        const sim_cmd_t *c = &p->rx[i];
        if (c->ext && c->id <= CAN_SFF_MASK)
        {
            return CP_CAR_SIM_INVALID;
        }

        bool known = false;
        for (uint8_t j = 0U; j < n; j++)
        {
            if (f[j].arb == c->id)
            {
                known = true;
                break;
            }
        }
        if (known)
        {
            continue;
        }
        if (n >= CAN_MAX_FILTERS)
        {
            return CP_CAR_SIM_INVALID;
        }
        f[n].arb  = c->id;
        f[n].mask = c->ext ? CAN_MASK_EXACT_EXTENDED : CAN_MASK_EXACT_STANDARD;
        f[n].cb   = car_sim_on_rx;
        n++;
    }

    for (uint8_t j = 0U; (out != NULL) && (j < n); j++)
    {
        out[j] = f[j];
    }
    return n;
}

const can_parser_profile_t *cp_car_sim_profile_get(void)
{
    return &s_profile;
}

uint8_t cp_car_sim_filter_count(const car_sim_profile_t *p)
{
    return (p == NULL) ? CP_CAR_SIM_INVALID : build_filters(p, NULL);
}

bool cp_car_sim_set_profile(const car_sim_profile_t *p)
{
    if (p == NULL || p->bus_config == NULL || cp_car_sim_filter_count(p) == CP_CAR_SIM_INVALID)
    {
        return false;
    }

    s_bus_config = *p->bus_config;
    s_profile.num_filters = build_filters(p, s_filters);
    return true;
}
