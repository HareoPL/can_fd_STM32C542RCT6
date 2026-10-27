/**
 * @file sim_renault_arkana.h
 * @brief Car simulator Renault Arkana (device parser 196): stage 1 frames read by the device
 *        parser, carsharing open / close commands with the v-box PIN and the key fob frame.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#ifndef SIM_RENAULT_ARKANA_H
#define SIM_RENAULT_ARKANA_H

#include "car_sim_types.h"

/**
 * @brief Returns the Renault Arkana profile: 13 standard frames every 100 ms (DLC 8), commands
 *        0x46F with the PIN in [1..2], key fob 0x46F (default PIN 0x1234), CAN1 Classic 500 kbps,
 *        RPM 0..8000 step 1, SW8 = 800.
 * @return Immutable profile descriptor.
 */
const car_sim_profile_t *sim_renault_arkana_profile_get(void);

#endif /* SIM_RENAULT_ARKANA_H */
