/**
 * @file car_sim_codec.h
 * @brief Car simulator codec: vehicle state model, signal encoder, LX command matching with
 *        PIN check, key fob frames, frame scheduler and profile validation.
 *
 * Pure C (no FreeRTOS, no HAL), compiled into the host test (tools/test_car_sim_host.py).
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#ifndef CAR_SIM_CODEC_H
#define CAR_SIM_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "car_sim_types.h"
#include "io_sim.h"

/** @brief Result of car_sim_match_cmd(). */
typedef struct
{
    const sim_cmd_t *cmd;  /**< Matched command table entry */
    sim_action_t action;   /**< Action to execute (SIM_ACT_NONE when the PIN does not match) */
    bool has_pin;          /**< The command carries a PIN */
    bool pin_mismatch;     /**< Received PIN differs from the v-box PIN, action not executed (D8) */
    uint16_t pin_rx;       /**< Received PIN (valid when has_pin) */
} sim_match_t;

/** @brief Phases of the cyclic frames of one profile (car_sim_schedule_init()). */
typedef struct
{
    const sim_frame_t *frames;           /**< Frame table of the profile */
    uint8_t num;                         /**< Number of frames (max CAR_SIM_TX_MAX) */
    uint16_t period_ms[CAR_SIM_TX_MAX];  /**< Normalised period (multiple of CAR_SIM_TICK_MS) */
    uint16_t phase_ms[CAR_SIM_TX_MAX];   /**< Offset of the first transmission inside the period */
} car_sim_schedule_t;

/**
 * @brief Builds the vehicle state from the effective inputs.
 *        SW0..SW7 -> discrete signals (SW3 -> lights, SW4 -> locked), rpm_in = inputs RPM,
 *        D13: rpm = acc ? rpm_in : 0.
 * @param[in] eff Effective inputs (io_sim_resolve()).
 * @param[out] out Vehicle state.
 */
void vehicle_state_build(const io_sim_values_t *eff, vehicle_state_t *out);

/**
 * @brief Encodes one cyclic frame: init bytes, signals, encode hook, CAN ID helpers (TX only).
 * @param[in] f Frame description.
 * @param[in] vs Vehicle state.
 * @param[out] out Classic CAN frame ready for can_manager_send().
 */
void car_sim_encode_frame(const sim_frame_t *f, const vehicle_state_t *vs, CAN_msg_t *out);

/**
 * @brief Matches a received frame against a command table: ID, masked bytes, then the PIN.
 *        SIM_ACT_CUSTOM entries are resolved by their on_rx hook.
 * @param[in] table Command table (may be NULL when n is 0).
 * @param[in] n Number of entries.
 * @param[in] msg Received frame.
 * @param[in] pin Current v-box PIN.
 * @param[out] out Match result (zeroed when nothing matches).
 * @return true if an entry matched the ID and the bytes (also on a PIN mismatch).
 */
bool car_sim_match_cmd(const sim_cmd_t *table, uint8_t n, const CAN_msg_t *msg, uint16_t pin,
                       sim_match_t *out);

/**
 * @brief Builds a key fob frame with the PIN inserted at pin_byte (big endian).
 * @param[in] fob Key fob description.
 * @param[in] unlock true = unlock frame, false = lock frame.
 * @param[in] pin PIN to insert.
 * @param[out] out Classic CAN frame ready for can_manager_send().
 * @return false if fob is NULL or the PIN does not fit into the frame.
 */
bool car_sim_build_fob(const sim_fob_t *fob, bool unlock, uint16_t pin, CAN_msg_t *out);

/**
 * @brief Spreads the frames over the scheduler ticks: frame i starts in tick (i mod period ticks),
 *        so with equal periods no tick carries more than ceil(n / period ticks) frames.
 * @param[out] s Schedule.
 * @param[in] frames Frame table.
 * @param[in] num Number of frames (clamped to CAR_SIM_TX_MAX).
 */
void car_sim_schedule_init(car_sim_schedule_t *s, const sim_frame_t *frames, uint8_t num);

/**
 * @brief Returns the frames due in a tick.
 * @param[in] s Schedule.
 * @param[in] tick_ms Time since the schedule start, multiple of CAR_SIM_TICK_MS.
 * @param[out] due Indexes of the due frames.
 * @param[in] max_due Capacity of due.
 * @return Number of due frames.
 */
uint8_t car_sim_schedule_due(const car_sim_schedule_t *s, uint32_t tick_ms, uint8_t *due, uint8_t max_due);

/**
 * @brief Tells whether a profile encodes any vehicle signal without a physical input
 *        (speed, odometer, high beam, brake pedal, engine temperature), see io_sim_setVehicleSignalsUsed().
 * @param[in] p Profile.
 * @return true if at least one TX frame encodes such a signal.
 */
bool car_sim_profile_uses_vehicle_signals(const car_sim_profile_t *p);

/**
 * @brief Validates a profile: key, bus configuration, RPM limits, frame and signal layout,
 *        command and key fob PIN positions.
 * @param[in] p Profile.
 * @param[out] err Description of the first error (may be NULL).
 * @param[in] err_size Size of err.
 * @return true if the profile is valid.
 */
bool car_sim_profile_check(const car_sim_profile_t *p, char *err, size_t err_size);

#endif /* CAR_SIM_CODEC_H */
