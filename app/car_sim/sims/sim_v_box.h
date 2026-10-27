/**
 * @file sim_v_box.h
 * @brief Car simulator V_BOX: the v-box telemetry frame 0x01F2F3F4 and the relay command
 *        0x01F2F3F0, byte for byte as before the car simulators (device parser 192, D1).
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#ifndef SIM_V_BOX_H
#define SIM_V_BOX_H

#include "car_sim_types.h"

/**
 * @brief Returns the V_BOX profile: telemetry 0x01F2F3F4 (ext, 100 ms, 8 B) and relay command
 *        0x01F2F3F0 (ext, data[2] & 0x01), CAN1 Classic 500 kbps, RPM 0..750 step 3, SW8 = 750.
 * @return Immutable profile descriptor.
 */
const car_sim_profile_t *sim_v_box_profile_get(void);

#endif /* SIM_V_BOX_H */
