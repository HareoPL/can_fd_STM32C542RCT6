/**
 * @file io_sim.h
 * @brief Input source selection (physical IO or UART simulation) and simulated input state.
 *
 * After MCU reset the source is always IO (physical inputs). Only io_sim_setSource(IO_SRC_UART)
 * hands the inputs over to UART commands. SW4 (locks) is the relay feedback and is always taken
 * from the physical input, also in UART mode.
 *
 * No FreeRTOS / HAL dependencies apart from the critical section (IO_SIM_HOST builds it for the host test).
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-26
 */

#ifndef IO_SIM_H
#define IO_SIM_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Engine RPM per bit of telemetry byte data[2] (the receiver multiplies by 3). */
#define IO_RPM_PER_BIT   3U
/** @brief Maximum engine RPM (data[2] = 250 = 0xFA). */
#define IO_RPM_MAX       750U
/** @brief Engine RPM sent while the physical SW8 switch is active. */
#define IO_RPM_ENGINE_ON 750U
/** @brief Maximum fuel level in percent. */
#define IO_FUEL_MAX      100U

/** @brief Input source. */
typedef enum
{
    IO_SRC_IO = 0,  /**< Physical buttons and potentiometer (default after reset) */
    IO_SRC_UART     /**< Values set by UART commands */
} io_src_t;

/** @brief Discrete inputs, value = bit number in the button state. */
typedef enum
{
    IO_IN_ACC        = 0,  /**< ACC stacyjka (SW0) */
    IO_IN_DOOR_DRV   = 1,  /**< Drzwi kierowcy (SW1) */
    IO_IN_DOOR_OTHER = 2,  /**< Drzwi inne (SW2) */
    IO_IN_LIGHTS     = 3,  /**< Światła (SW3) */
    IO_IN_LOCKS      = 4,  /**< Zamki zamknięte / relay feedback (SW4), read only */
    IO_IN_HOOD       = 5,  /**< Maska (SW5) */
    IO_IN_HAZARD     = 6,  /**< Awaryjne (SW6) */
    IO_IN_HANDBRAKE  = 7,  /**< Hamulec ręczny (SW7) */
    IO_IN_ENGINE     = 8   /**< Obroty silnika (SW8), set with io_sim_setRpm() */
} io_input_t;

/** @brief Result of a simulation setter. */
typedef enum
{
    IO_SIM_OK = 0,
    IO_SIM_ERR_SRC,       /**< Source is IO, switch to UART first */
    IO_SIM_ERR_READONLY,  /**< SW4 (relay feedback) cannot be simulated */
    IO_SIM_ERR_INPUT,     /**< Not a discrete input that can be set */
    IO_SIM_ERR_RANGE,     /**< Value out of range */
    IO_SIM_ERR_RPM_STEP   /**< RPM is not a multiple of IO_RPM_PER_BIT */
} io_sim_result_t;

/** @brief Effective input values (what goes to the CAN telemetry frame). */
typedef struct
{
    io_src_t src;   /**< Active source */
    uint16_t btn;   /**< SW0..SW8 (bit 8 = engine running) */
    uint8_t fuel;   /**< Fuel level 0..100 % */
    uint16_t rpm;   /**< Engine RPM 0..750, multiple of IO_RPM_PER_BIT */
} io_sim_values_t;

/**
 * @brief Selects the input source. Switching IO -> UART copies the physical state to the
 *        simulated values, so the CAN frame does not change at the moment of switching.
 * @param[in] src New source.
 * @param[in] phys_btn Current debounced physical button state (snapshot for UART mode).
 * @param[in] phys_fuel Current physical fuel level 0..100 (snapshot for UART mode).
 */
void io_sim_setSource(io_src_t src, uint16_t phys_btn, uint8_t phys_fuel);

/** @brief Returns the active input source. */
io_src_t io_sim_getSource(void);

/**
 * @brief Sets a simulated discrete input (SW0..SW3, SW5..SW7).
 * @return IO_SIM_OK, IO_SIM_ERR_READONLY (SW4), IO_SIM_ERR_INPUT or IO_SIM_ERR_SRC.
 */
io_sim_result_t io_sim_setInput(io_input_t input, bool active);

/**
 * @brief Sets simulated engine RPM.
 * @param[in] rpm 0..IO_RPM_MAX, multiple of IO_RPM_PER_BIT (no rounding).
 * @return IO_SIM_OK, IO_SIM_ERR_RANGE, IO_SIM_ERR_RPM_STEP or IO_SIM_ERR_SRC.
 */
io_sim_result_t io_sim_setRpm(uint32_t rpm);

/**
 * @brief Sets simulated fuel level.
 * @param[in] fuel 0..IO_FUEL_MAX percent.
 * @return IO_SIM_OK, IO_SIM_ERR_RANGE or IO_SIM_ERR_SRC.
 */
io_sim_result_t io_sim_setFuel(uint32_t fuel);

/**
 * @brief Computes effective input values for the active source.
 * @param[in] phys_btn Debounced physical button state (SW4 always taken from it).
 * @param[in] phys_fuel Physical fuel level 0..100.
 * @param[out] out Effective values.
 */
void io_sim_resolve(uint16_t phys_btn, uint8_t phys_fuel, io_sim_values_t *out);

/** @brief Converts engine RPM to telemetry byte data[2] (RPM / IO_RPM_PER_BIT). */
uint8_t io_sim_rpmToByte(uint16_t rpm);

#endif /* IO_SIM_H */
