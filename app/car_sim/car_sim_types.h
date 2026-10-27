/**
 * @file car_sim_types.h
 * @brief Car simulator types: vehicle state model and declarative car profiles (frames and
 *        signals like a DBC file, LX command table, key fob).
 *
 * A car profile describes the car, not the device parser: it sends what the car would send and
 * reacts to commands like the car would. The device parser is only the source of the frame IDs,
 * bit masks and formulas.
 *
 * Pure C (no FreeRTOS, no HAL): profiles return action codes, the car_sim engine executes them.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#ifndef CAR_SIM_TYPES_H
#define CAR_SIM_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include "can_port.h"

/** @brief Period of the car simulator task and resolution of the frame scheduler in ms. */
#define CAR_SIM_TICK_MS        10U
/** @brief Maximum number of cyclic TX frames in one car profile. */
#define CAR_SIM_TX_MAX         32U
/** @brief Payload length of a car simulator frame (Classic CAN). */
#define CAR_SIM_DLC_MAX        8U
/** @brief Number of bytes of a PIN code in a frame (big endian). */
#define CAR_SIM_PIN_LEN        2U

/** @brief State of the simulated car, built from the effective inputs (buttons, potentiometer, UART). */
typedef struct
{
    bool acc;              /**< Ignition / ACC (SW0) */
    bool door_drv;         /**< Driver door open (SW2) */
    bool door_other;       /**< Any other door open (SW1) */
    bool hood;             /**< Hood open (SW6) */
    bool handbrake;        /**< Handbrake applied (SW7) */
    bool hazard;           /**< Hazard lights (SW5) */
    bool lights;           /**< SW3: lights as the device parser reads them (D6), e.g. Arkana = low beam */
    bool light_high;       /**< High beam (UART DROGOWE#) */
    bool locked;           /**< SW4: central lock closed = relay feedback (D7) */
    bool brake_pedal;      /**< Brake pedal pressed (UART HAMULEC_NOZNY#) */
    bool ss_button;        /**< Start/Stop button pressed (stage 2, always false) */
    uint16_t rpm;          /**< Engine RPM, 0 while the ignition is off (D13) */
    uint16_t rpm_in;       /**< Engine RPM from the inputs without the D13 rule (V_BOX simulator) */
    uint16_t speed_kmh;    /**< Vehicle speed in km/h */
    uint8_t fuel_pct;      /**< Fuel level 0..100 % */
    uint32_t odometer_m;   /**< Odometer in metres (advanced with the speed by the car_sim task) */
    int16_t engine_temp_c; /**< Engine coolant temperature in deg C */
    uint16_t btn;          /**< Raw effective SW0..SW8 (V_BOX simulator) */
} vehicle_state_t;

/** @brief Vehicle state signal encoded into a frame. */
typedef enum
{
    SIG_ACC = 0,     /**< vehicle_state_t::acc */
    SIG_DOOR_DRV,    /**< vehicle_state_t::door_drv */
    SIG_DOOR_OTHER,  /**< vehicle_state_t::door_other */
    SIG_HOOD,        /**< vehicle_state_t::hood */
    SIG_HANDBRAKE,   /**< vehicle_state_t::handbrake */
    SIG_HAZARD,      /**< vehicle_state_t::hazard */
    SIG_LIGHTS,      /**< vehicle_state_t::lights */
    SIG_LIGHT_HIGH,  /**< vehicle_state_t::light_high (vehicle signal, UART only) */
    SIG_LOCKED,      /**< vehicle_state_t::locked */
    SIG_BRAKE_PEDAL, /**< vehicle_state_t::brake_pedal (vehicle signal, UART only) */
    SIG_SS_BUTTON,   /**< vehicle_state_t::ss_button */
    SIG_RPM,         /**< vehicle_state_t::rpm (D13 applied) */
    SIG_SPEED,       /**< vehicle_state_t::speed_kmh (vehicle signal, UART only) */
    SIG_FUEL,        /**< vehicle_state_t::fuel_pct */
    SIG_ODOMETER,    /**< vehicle_state_t::odometer_m (vehicle signal, UART only) */
    SIG_ENGINE_TEMP, /**< vehicle_state_t::engine_temp_c (vehicle signal, UART only) */
    SIG_COUNT        /**< Number of signals */
} sim_sig_t;

/** @brief Signal encoding in a frame. */
typedef enum
{
    SIM_ENC_BIT = 0, /**< Single bit (mask) set when the signal value is not 0 */
    SIM_ENC_UINT_BE, /**< Unsigned field of len bytes, big endian (first byte = most significant) */
    SIM_ENC_UINT_LE  /**< Unsigned field of len bytes, little endian (first byte = least significant) */
} sim_enc_t;

/**
 * @brief One signal of a frame. UINT fields: raw = value * mul / div + offset, saturated to
 *        0..max (and to the field width), then shifted left by shift bits inside the field.
 *        Bits of the field below shift keep their init value.
 */
typedef struct
{
    sim_sig_t sig;   /**< Encoded signal (the same signal may have several entries) */
    sim_enc_t enc;   /**< Encoding */
    uint8_t byte;    /**< First byte of the field */
    uint8_t mask;    /**< BIT: bit mask */
    uint8_t len;     /**< UINT: field length in bytes 1..4 */
    uint8_t shift;   /**< UINT: left shift of the value inside the field (e.g. Arkana odometer 4 bits) */
    uint16_t mul;    /**< UINT: multiplier (0 = 1) */
    uint16_t div;    /**< UINT: divisor (0 = 1) */
    int16_t offset;  /**< UINT: offset added after the scaling */
    uint32_t max;    /**< UINT: maximum raw value, e.g. to avoid a "no data" code (0 = field maximum) */
} sim_signal_t;

/** @brief Cyclic TX frame of a car profile. */
typedef struct
{
    uint32_t id;                         /**< CAN ID (11 or 29 bit) */
    bool ext;                            /**< true = 29-bit extended ID */
    uint8_t dlc;                         /**< Payload length 0..8 */
    uint16_t period_ms;                  /**< Period, multiple of CAR_SIM_TICK_MS (0 = 100 ms) */
    uint8_t init[CAR_SIM_DLC_MAX];       /**< Default bytes (unknown bytes = 0) */
    const sim_signal_t *signals;         /**< Signal table (may be NULL) */
    uint8_t num_signals;                 /**< Number of entries in signals */
    void (*encode)(const vehicle_state_t *vs, uint8_t data[CAR_SIM_DLC_MAX]); /**< Hook after the signals (optional) */
} sim_frame_t;

/** @brief Action requested by a received command, executed by the car_sim engine. */
typedef enum
{
    SIM_ACT_NONE = 0, /**< No action */
    SIM_ACT_UNLOCK,   /**< Open the central lock (relay target 0) */
    SIM_ACT_LOCK,     /**< Close the central lock (relay target 1) */
    SIM_ACT_LOG,      /**< Log the frame only */
    SIM_ACT_CUSTOM    /**< Resolved by sim_cmd_t::on_rx */
} sim_action_t;

/** @brief Command frame sent by the device (LX) to the car. */
typedef struct
{
    uint32_t id;                         /**< CAN ID (11 or 29 bit) */
    bool ext;                            /**< true = 29-bit extended ID */
    uint8_t match[CAR_SIM_DLC_MAX];      /**< Expected bytes (only the bits in match_mask are compared) */
    uint8_t match_mask[CAR_SIM_DLC_MAX]; /**< Compared bits; a byte with mask != 0 must be present in the frame */
    int8_t pin_byte;                     /**< First byte of the PIN (2 bytes, big endian), -1 = no PIN */
    sim_action_t action;                 /**< Action when the bytes (and the PIN) match */
    sim_action_t (*on_rx)(const CAN_msg_t *msg); /**< SIM_ACT_CUSTOM: returns the action to execute (optional) */
} sim_cmd_t;

/** @brief Key fob of the car (frames with the PIN, D8). */
typedef struct
{
    uint32_t id;                         /**< CAN ID (11 or 29 bit) */
    bool ext;                            /**< true = 29-bit extended ID */
    uint8_t dlc;                         /**< Payload length */
    uint8_t unlock[CAR_SIM_DLC_MAX];     /**< Unlock frame, PIN bytes overwritten */
    uint8_t lock[CAR_SIM_DLC_MAX];       /**< Lock frame, PIN bytes overwritten */
    uint8_t pin_byte;                    /**< First byte of the PIN (big endian), e.g. Arkana 1 */
    uint16_t default_pin;                /**< Default v-box PIN of the profile, e.g. Arkana 0x1234 */
} sim_fob_t;

/** @brief Car simulator profile. */
typedef struct
{
    const char *key;                     /**< Short key for SIM#<key>, e.g. "ARKANA" */
    const char *name;                    /**< Display name, e.g. "Renault Arkana" */
    const uint16_t *dev_parsers;         /**< Device parser numbers mapped to this simulator (may be NULL) */
    uint8_t num_dev_parsers;             /**< Number of entries in dev_parsers */
    const can_config_t *bus_config;      /**< CAN1 bus configuration of the car */
    const sim_frame_t *tx;               /**< Cyclic TX frames */
    uint8_t num_tx;                      /**< Number of TX frames (max CAR_SIM_TX_MAX) */
    const sim_cmd_t *rx;                 /**< Commands from the device (may be NULL) */
    uint8_t num_rx;                      /**< Number of commands */
    uint16_t rpm_max;                    /**< Maximum RPM (OBROTY#) */
    uint16_t rpm_step;                   /**< RPM step (OBROTY#) */
    uint16_t idle_rpm;                   /**< RPM of the physical SW8 switch */
    const sim_fob_t *fob;                /**< Key fob (optional, NULL = none) */
    void (*on_select)(void);             /**< Resets the profile counters on selection (optional) */
} car_sim_profile_t;

#endif /* CAR_SIM_TYPES_H */
