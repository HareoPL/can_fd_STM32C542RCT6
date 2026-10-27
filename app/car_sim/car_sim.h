/**
 * @file car_sim.h
 * @brief Car simulator engine: car_sim task (vehicle state, cyclic CAN1 frames), LX commands
 *        received on CAN1, automatic and manual selection of the car simulator.
 *
 * Concurrency (docs/car_sim/car_sim_tasks.md, section 4.3):
 * - the simulator is selected by the car_sim task only; the RS485 callback and the UART commands
 *   only post a request (car_sim_request_*(), non-blocking),
 * - car_sim_on_rx() runs in can_rx_task: matching, PIN check, io_manager_setRelayStateAsync()
 *   and safe_printf() only.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#ifndef CAR_SIM_H
#define CAR_SIM_H

#include <stdbool.h>
#include <stdint.h>
#include "can_manager.h"
#include "car_sim_registry.h"

/** @brief Device parser number not received yet (car_sim_info_t::last_dev_parser). */
#define CAR_SIM_DEV_PARSER_UNKNOWN 0xFFFFU

/** @brief Selection mode of the car simulator (D3). */
typedef enum
{
    CAR_SIM_MODE_AUTO = 0, /**< Simulator follows the device parser number read over RS485 */
    CAR_SIM_MODE_MANUAL    /**< Simulator selected by SIM#<key>, device parser numbers only stored */
} car_sim_mode_t;

/** @brief Snapshot of the car simulator state (car_sim_get_info()). */
typedef struct
{
    car_sim_id_t id;            /**< Active simulator */
    const car_sim_profile_t *profile; /**< Active profile */
    car_sim_mode_t mode;        /**< Selection mode */
    uint16_t last_dev_parser;   /**< Last device parser number, CAR_SIM_DEV_PARSER_UNKNOWN if none */
    uint16_t pin;               /**< Effective v-box PIN */
    bool pin_overridden;        /**< PIN set by car_sim_set_pin() instead of the profile default */
    bool owns_can1;             /**< car_sim configures CAN1 (false in the CAN_FD_TEST build) */
    uint32_t tx_sent;           /**< Frames queued for CAN1 */
    uint32_t tx_dropped;        /**< Frames dropped because the CAN1 TX queue was full */
    uint32_t rx_cmds;           /**< LX commands matched */
    uint32_t rx_pin_rejected;   /**< LX commands ignored because of a wrong PIN */
} car_sim_info_t;

/**
 * @brief Validates the simulator registry, selects V_BOX in AUTO mode and creates the car_sim task.
 *        Call before vTaskStartScheduler(), after can_manager_init() of CAN1.
 * @param[in,out] can1 CAN1 instance.
 * @param[in] own_can1 true: car_sim selects the CAN1 parser profile and attaches BOOT_OTA;
 *            false (CAN_FD_TEST build): CAN1 belongs to another module, car_sim only sends V_BOX.
 * @return false if the registry is inconsistent, a profile exceeds the filter capacity of CAN1
 *         with the BOOT_OTA filters, the CAN1 configuration fails or the task cannot be created.
 */
bool car_sim_init(can_instance_t *can1, bool own_can1);

/**
 * @brief Requests a manual selection (mode MANUAL), executed by the car_sim task.
 * @param[in] id Simulator to select.
 */
void car_sim_request_select(car_sim_id_t id);

/**
 * @brief Reports the device parser number of CAN1 (RS485 callback), executed by the car_sim task:
 *        AUTO -> matching simulator or BRAK, MANUAL -> number stored only (D2-D4).
 * @param[in] no Device parser number.
 */
void car_sim_request_dev_parser(uint16_t no);

/**
 * @brief Requests the key fob: fob frame with the v-box PIN (if the profile has a fob) and the relay.
 * @param[in] unlock true = open, false = close.
 */
void car_sim_request_fob(bool unlock);

/**
 * @brief Requests the selection mode. AUTO applies the last device parser number at once.
 * @param[in] mode New mode.
 */
void car_sim_set_mode(car_sim_mode_t mode);

/**
 * @brief Overrides the v-box PIN in RAM for every profile (D8).
 * @param[in] pin New PIN.
 */
void car_sim_set_pin(uint16_t pin);

/** @brief Returns to the default PIN of the active profile. */
void car_sim_reset_pin(void);

/**
 * @brief Returns a snapshot of the car simulator state.
 * @param[out] info State.
 */
void car_sim_get_info(car_sim_info_t *info);

/**
 * @brief CAN1 RX callback of the cp_car_sim parser profile (can_rx_task context, non-blocking):
 *        matches the frame against the commands of the active simulator and executes the action.
 * @param[in] msg Received frame.
 */
void car_sim_on_rx(const CAN_msg_t *msg);

#endif /* CAR_SIM_H */
