/**
 * @file sim_none.h
 * @brief Car simulator BRAK: marker frame for a device parser number without a car simulator (D4).
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#ifndef SIM_NONE_H
#define SIM_NONE_H

#include "car_sim_types.h"

/**
 * @brief Returns the BRAK profile: only the frame 0x01F2F3FF (ext, DLC 6, CA FE 00 BA BE 00)
 *        every 1000 ms on CAN1 Classic 500 kbps, no commands, no key fob, no device parser numbers.
 * @return Immutable profile descriptor.
 */
const car_sim_profile_t *sim_none_profile_get(void);

#endif /* SIM_NONE_H */
