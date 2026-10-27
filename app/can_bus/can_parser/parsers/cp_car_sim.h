/**
 * @file cp_car_sim.h
 * @brief CAN parser profile of the active car simulator: bus configuration of the car and one
 *        exact filter per unique command ID of its rx table, dispatched to car_sim_on_rx().
 *
 * The profile is rebuilt by cp_car_sim_set_profile() (car_sim task only) before
 * can_parser_select(CAN_PARSER_ID_CAR_SIM), like the variable profile of cp_fd_test.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#ifndef CP_CAR_SIM_H
#define CP_CAR_SIM_H

#include <stdbool.h>
#include <stdint.h>
#include "can_parser.h"
#include "car_sim_types.h"

/**
 * @brief Returns the profile descriptor built by the last cp_car_sim_set_profile().
 * @return Profile descriptor.
 */
const can_parser_profile_t *cp_car_sim_profile_get(void);

/**
 * @brief Counts the filters a car profile needs (unique command IDs).
 * @param[in] p Car profile.
 * @return Number of filters, or 0xFF if an extended command ID is not above 0x7FF (the CAN manager
 *         tells standard and extended filters apart by the ID value).
 */
uint8_t cp_car_sim_filter_count(const car_sim_profile_t *p);

/**
 * @brief Builds the bus configuration and the filters of a car profile.
 * @param[in] p Car profile.
 * @return false if p is NULL, its filters are invalid or exceed CAN_MAX_FILTERS (profile unchanged).
 */
bool cp_car_sim_set_profile(const car_sim_profile_t *p);

#endif /* CP_CAR_SIM_H */
