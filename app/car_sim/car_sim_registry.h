/**
 * @file car_sim_registry.h
 * @brief Registry of car simulators and the map "device parser number -> car simulator".
 *
 * Pure C (no FreeRTOS, no HAL). Each simulator is registered by its profile getter
 * (sim_<car>_profile_get(), zero extern), like the can_parser profiles.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#ifndef CAR_SIM_REGISTRY_H
#define CAR_SIM_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "car_sim_types.h"

/** @brief Identifiers of the registered car simulators. */
typedef enum
{
    CAR_SIM_ID_NONE = 0, /**< BRAK: marker frame for a device parser number without a simulator */

    CAR_SIM_COUNT        /**< Number of simulators, also "not found" */
} car_sim_id_t;

/**
 * @brief Returns the profile of a simulator.
 * @param[in] id Simulator identifier.
 * @return Profile, or NULL for an unknown identifier.
 */
const car_sim_profile_t *car_sim_get(car_sim_id_t id);

/**
 * @brief Maps a device parser number to its car simulator.
 * @param[in] no Device parser number (0 = no parser).
 * @return Simulator with this number, or CAR_SIM_ID_NONE (also for 0).
 */
car_sim_id_t car_sim_find_by_dev_parser(uint16_t no);

/**
 * @brief Finds a simulator by its key (case-insensitive), e.g. "brak".
 * @param[in] key Simulator key.
 * @return Simulator identifier, or CAR_SIM_COUNT if not found.
 */
car_sim_id_t car_sim_find_by_key(const char *key);

/**
 * @brief Validates the registry: every profile (car_sim_profile_check()), unique keys, every device
 *        parser number in one simulator only, number 0 not mapped.
 * @param[out] err Description of the first error (may be NULL).
 * @param[in] err_size Size of err.
 * @return true if the registry is consistent.
 */
bool car_sim_registry_check(char *err, size_t err_size);

#endif /* CAR_SIM_REGISTRY_H */
