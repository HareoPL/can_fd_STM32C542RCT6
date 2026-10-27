/**
 * @file io_sim.h
 * @brief Input source selection (physical IO or UART simulation) and simulated input state.
 *
 * After MCU reset the source is always IO (physical inputs). Only io_sim_setSource(IO_SRC_UART)
 * hands the inputs over to UART commands. SW4 (locks) is the relay feedback and is always taken
 * from the physical input, also in UART mode. The simulated lock fault (relay ignores CAN commands)
 * can be set in UART mode only and is cleared by switching back to IO, io_sim_reset() and MCU reset.
 *
 * The RPM limits come from the active car simulator (io_sim_setRpmLimits(), default V_BOX: 0..750,
 * step 3, SW8 = 750). The vehicle signals (speed, odometer, high beam, brake pedal, engine
 * temperature) have no physical input: they are set by UART commands only, IO mode reports their
 * default values. Switching IO -> UART and io_sim_reset() restore the defaults.
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
/** @brief Maximum engine RPM of the V_BOX simulator (data[2] = 250 = 0xFA), default RPM limit. */
#define IO_RPM_MAX       750U
/** @brief Engine RPM of the physical SW8 switch for the V_BOX simulator, default RPM limit. */
#define IO_RPM_ENGINE_ON 750U
/** @brief Maximum fuel level in percent. */
#define IO_FUEL_MAX      100U
/** @brief Maximum vehicle speed in km/h (PREDKOSC#). */
#define IO_SPEED_MAX     250U
/** @brief Maximum odometer in km (PRZEBIEG#). */
#define IO_ODOMETER_MAX_KM 999999U
/** @brief Odometer in km reported in IO mode and after a reset of the vehicle signals. */
#define IO_ODOMETER_DEFAULT_KM 10000U
/** @brief Minimum engine coolant temperature in deg C (TEMP_SILNIKA#). */
#define IO_ENGINE_TEMP_MIN (-40)
/** @brief Maximum engine coolant temperature in deg C (TEMP_SILNIKA#). */
#define IO_ENGINE_TEMP_MAX 150
/** @brief Engine coolant temperature in deg C reported in IO mode and after a reset of the vehicle signals. */
#define IO_ENGINE_TEMP_DEFAULT 90

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
    IO_IN_DOOR_OTHER = 1,  /**< Drzwi inne (SW1) */
    IO_IN_DOOR_DRV   = 2,  /**< Drzwi kierowcy (SW2) */
    IO_IN_LIGHTS     = 3,  /**< Światła (SW3) */
    IO_IN_LOCKS      = 4,  /**< Zamki zamknięte / relay feedback (SW4), read only */
    IO_IN_HAZARD     = 5,  /**< Awaryjne (SW5) */
    IO_IN_HOOD       = 6,  /**< Maska (SW6) */
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
    IO_SIM_ERR_RPM_STEP   /**< RPM is not a multiple of the RPM step (io_sim_rpm_limits_t::step) */
} io_sim_result_t;

/** @brief RPM limits of the active car simulator. */
typedef struct
{
    uint16_t max;       /**< Maximum engine RPM accepted by io_sim_setRpm() */
    uint16_t step;      /**< io_sim_setRpm() accepts multiples of this value only (1 = any value) */
    uint16_t engine_on; /**< Engine RPM while the physical SW8 switch is active (idle RPM) */
} io_sim_rpm_limits_t;

/** @brief Effective input values (what goes to the CAN frames of the active car simulator). */
typedef struct
{
    io_src_t src;   /**< Active source */
    uint16_t btn;   /**< SW0..SW8 (bit 8 = engine running) */
    uint8_t fuel;   /**< Fuel level 0..100 % */
    uint16_t rpm;   /**< Engine RPM 0..io_sim_rpm_limits_t::max, multiple of the RPM step */
    bool lock_fault; /**< Simulated lock fault: relay commands are ignored */
    uint16_t speed_kmh;   /**< Vehicle speed 0..IO_SPEED_MAX km/h (UART only, IO mode: 0) */
    uint32_t odometer_m;  /**< Odometer in metres (UART only, IO mode: IO_ODOMETER_DEFAULT_KM) */
    bool high_beam;       /**< High beam (UART only, IO mode: off) */
    bool brake_pedal;     /**< Brake pedal pressed (UART only, IO mode: released) */
    int16_t engine_temp_c; /**< Engine coolant temperature in deg C (UART only, IO mode: IO_ENGINE_TEMP_DEFAULT) */
} io_sim_values_t;

/**
 * @brief Sets the RPM limits of the active car simulator. A simulated RPM above the new maximum
 *        is clamped, a value off the new step is rounded down to it.
 * @param[in] max Maximum engine RPM.
 * @param[in] step RPM step (0 is treated as 1).
 * @param[in] engine_on Engine RPM of the physical SW8 switch (clamped to max).
 */
void io_sim_setRpmLimits(uint16_t max, uint16_t step, uint16_t engine_on);

/**
 * @brief Returns the RPM limits of the active car simulator.
 * @return Current limits (default IO_RPM_MAX, IO_RPM_PER_BIT, IO_RPM_ENGINE_ON).
 */
io_sim_rpm_limits_t io_sim_getRpmLimits(void);

/**
 * @brief Tells the UART commands whether the active car simulator sends the vehicle signals.
 *        When false (V_BOX, BRAK), ZRODLO?, IO_HELP, IO_RESET and OBROTY# keep the V_BOX texts;
 *        when true they also list the vehicle signals. The values are stored in both cases.
 * @param[in] used true if the active car simulator encodes the vehicle signals.
 */
void io_sim_setVehicleSignalsUsed(bool used);

/** @brief Returns the flag set by io_sim_setVehicleSignalsUsed() (default false). */
bool io_sim_areVehicleSignalsUsed(void);

/**
 * @brief Selects the input source. Switching IO -> UART copies the physical state to the
 *        simulated values, so the CAN frame does not change at the moment of switching
 *        (the vehicle signals start from their IO mode defaults).
 *        Switching to IO clears the simulated lock fault.
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
 * @param[in] rpm 0..io_sim_rpm_limits_t::max, multiple of io_sim_rpm_limits_t::step (no rounding).
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
 * @brief Sets the simulated vehicle speed.
 * @param[in] speed_kmh 0..IO_SPEED_MAX km/h.
 * @return IO_SIM_OK, IO_SIM_ERR_RANGE or IO_SIM_ERR_SRC.
 */
io_sim_result_t io_sim_setSpeed(uint32_t speed_kmh);

/**
 * @brief Sets the simulated odometer (the car simulator then advances it with the speed).
 * @param[in] km 0..IO_ODOMETER_MAX_KM.
 * @return IO_SIM_OK, IO_SIM_ERR_RANGE or IO_SIM_ERR_SRC.
 */
io_sim_result_t io_sim_setOdometer(uint32_t km);

/**
 * @brief Advances the simulated odometer by the distance driven (UART mode only, no-op in IO mode).
 *        Saturates at IO_ODOMETER_MAX_KM.
 * @param[in] metres Distance driven since the previous call.
 */
void io_sim_advanceOdometer(uint32_t metres);

/**
 * @brief Sets the simulated high beam.
 * @return IO_SIM_OK or IO_SIM_ERR_SRC.
 */
io_sim_result_t io_sim_setHighBeam(bool on);

/**
 * @brief Sets the simulated brake pedal.
 * @return IO_SIM_OK or IO_SIM_ERR_SRC.
 */
io_sim_result_t io_sim_setBrakePedal(bool pressed);

/**
 * @brief Sets the simulated engine coolant temperature.
 * @param[in] temp_c IO_ENGINE_TEMP_MIN..IO_ENGINE_TEMP_MAX deg C.
 * @return IO_SIM_OK, IO_SIM_ERR_RANGE or IO_SIM_ERR_SRC.
 */
io_sim_result_t io_sim_setEngineTemp(int32_t temp_c);

/**
 * @brief Sets or clears the simulated lock fault (relay does not react to CAN commands).
 * @return IO_SIM_OK or IO_SIM_ERR_SRC.
 */
io_sim_result_t io_sim_setLockFault(bool fault);

/** @brief Returns true if the simulated lock fault is active (relay commands must be ignored). */
bool io_sim_isLockFault(void);

/**
 * @brief Resets the simulation settings: all discrete inputs 0, RPM 0, fuel 0, lock fault off,
 *        vehicle signals to their defaults (speed 0, odometer IO_ODOMETER_DEFAULT_KM, high beam off,
 *        brake pedal released, engine temperature IO_ENGINE_TEMP_DEFAULT). The source stays UART.
 * @return IO_SIM_OK or IO_SIM_ERR_SRC.
 */
io_sim_result_t io_sim_reset(void);

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
