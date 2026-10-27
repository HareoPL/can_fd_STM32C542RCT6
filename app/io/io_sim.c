/**
 * @file io_sim.c
 * @brief Input source selection (physical IO or UART simulation) and simulated input state.
 *
 * Written by the UART bridge task (commands) and the car simulator task (RPM limits, odometer),
 * read by the io_manager and car simulator tasks, so every access to the shared state is done
 * in a critical section.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-26
 */

#include "io_sim.h"

#ifdef IO_SIM_HOST
#define IO_SIM_ENTER() do { } while (0)
#define IO_SIM_EXIT()  do { } while (0)
#else
#include "FreeRTOS.h"
#include "task.h"
#define IO_SIM_ENTER() taskENTER_CRITICAL()
#define IO_SIM_EXIT()  taskEXIT_CRITICAL()
#endif

#define IO_BIT(input)      ((uint16_t)(1U << (uint16_t)(input)))
#define IO_SIM_LOCKS_MASK  IO_BIT(IO_IN_LOCKS)
#define IO_SIM_ENGINE_MASK IO_BIT(IO_IN_ENGINE)
/** @brief Discrete inputs that can be set by UART: SW0..SW3, SW5..SW7. */
#define IO_SIM_SETTABLE_MASK ((uint16_t)(0x00FFU & ~IO_SIM_LOCKS_MASK))

static io_src_t s_src = IO_SRC_IO;
static uint16_t s_btn = 0U;   /**< Simulated SW0..SW3, SW5..SW7 */
static uint16_t s_rpm = 0U;
static uint8_t s_fuel = 0U;
static bool s_lock_fault = false;   /**< Relay ignores CAN commands (UART mode only) */
static io_sim_rpm_limits_t s_limits = { IO_RPM_MAX, IO_RPM_PER_BIT, IO_RPM_ENGINE_ON };
static bool s_vehicle_signals_used = false;   /**< Active car simulator encodes the vehicle signals */

#define IO_ODOMETER_DEFAULT_M ((uint32_t)IO_ODOMETER_DEFAULT_KM * 1000U)
#define IO_ODOMETER_MAX_M     ((uint32_t)IO_ODOMETER_MAX_KM * 1000U)

static uint16_t s_speed_kmh = 0U;
static uint32_t s_odometer_m = IO_ODOMETER_DEFAULT_M;
static bool s_high_beam = false;
static bool s_brake_pedal = false;
static int16_t s_engine_temp_c = IO_ENGINE_TEMP_DEFAULT;

/** @brief Restores the vehicle signal defaults (caller holds the critical section). */
static void vehicle_signals_default(void)
{
    s_speed_kmh = 0U;
    s_odometer_m = IO_ODOMETER_DEFAULT_M;
    s_high_beam = false;
    s_brake_pedal = false;
    s_engine_temp_c = IO_ENGINE_TEMP_DEFAULT;
}

/**
 * @brief Common part of the UART-only setters: enters the critical section and checks the source.
 * @return true if the source is UART (critical section held), false otherwise (section released).
 */
static bool uart_enter(void)
{
    IO_SIM_ENTER();
    if (s_src != IO_SRC_UART)
    {
        IO_SIM_EXIT();
        return false;
    }
    return true;
}

void io_sim_setRpmLimits(uint16_t max, uint16_t step, uint16_t engine_on)
{
    if (step == 0U)
    {
        step = 1U;
    }
    IO_SIM_ENTER();
    s_limits.max = max;
    s_limits.step = step;
    s_limits.engine_on = (engine_on > max) ? max : engine_on;
    if (s_rpm > max)
    {
        s_rpm = max;
    }
    s_rpm = (uint16_t)((s_rpm / step) * step);
    IO_SIM_EXIT();
}

io_sim_rpm_limits_t io_sim_getRpmLimits(void)
{
    IO_SIM_ENTER();
    io_sim_rpm_limits_t limits = s_limits;
    IO_SIM_EXIT();
    return limits;
}

void io_sim_setVehicleSignalsUsed(bool used)
{
    s_vehicle_signals_used = used;
}

bool io_sim_areVehicleSignalsUsed(void)
{
    return s_vehicle_signals_used;
}

void io_sim_setSource(io_src_t src, uint16_t phys_btn, uint8_t phys_fuel)
{
    IO_SIM_ENTER();
    if (src == IO_SRC_UART && s_src != IO_SRC_UART)
    {
        /* Snapshot: UART mode starts from the current physical state */
        s_btn = (uint16_t)(phys_btn & IO_SIM_SETTABLE_MASK);
        s_rpm = ((phys_btn & IO_SIM_ENGINE_MASK) != 0U) ? s_limits.engine_on : 0U;
        s_fuel = (phys_fuel > IO_FUEL_MAX) ? (uint8_t)IO_FUEL_MAX : phys_fuel;
        vehicle_signals_default();
    }
    s_src = (src == IO_SRC_UART) ? IO_SRC_UART : IO_SRC_IO;
    if (s_src == IO_SRC_IO)
    {
        s_lock_fault = false;
    }
    IO_SIM_EXIT();
}

io_src_t io_sim_getSource(void)
{
    return s_src;
}

io_sim_result_t io_sim_setInput(io_input_t input, bool active)
{
    if (input == IO_IN_LOCKS)
    {
        return IO_SIM_ERR_READONLY;
    }
    if ((unsigned)input > 15U || (IO_BIT(input) & IO_SIM_SETTABLE_MASK) == 0U)
    {
        return IO_SIM_ERR_INPUT;
    }

    io_sim_result_t res = IO_SIM_OK;
    IO_SIM_ENTER();
    if (s_src != IO_SRC_UART)
    {
        res = IO_SIM_ERR_SRC;
    }
    else if (active)
    {
        s_btn = (uint16_t)(s_btn | IO_BIT(input));
    }
    else
    {
        s_btn = (uint16_t)(s_btn & ~IO_BIT(input));
    }
    IO_SIM_EXIT();
    return res;
}

io_sim_result_t io_sim_setRpm(uint32_t rpm)
{
    io_sim_rpm_limits_t limits = io_sim_getRpmLimits();

    if (rpm > limits.max)
    {
        return IO_SIM_ERR_RANGE;
    }
    if ((rpm % limits.step) != 0U)
    {
        return IO_SIM_ERR_RPM_STEP;
    }

    if (!uart_enter())
    {
        return IO_SIM_ERR_SRC;
    }
    s_rpm = (uint16_t)rpm;
    IO_SIM_EXIT();
    return IO_SIM_OK;
}

io_sim_result_t io_sim_setFuel(uint32_t fuel)
{
    if (fuel > IO_FUEL_MAX)
    {
        return IO_SIM_ERR_RANGE;
    }

    io_sim_result_t res = IO_SIM_OK;
    IO_SIM_ENTER();
    if (s_src != IO_SRC_UART)
    {
        res = IO_SIM_ERR_SRC;
    }
    else
    {
        s_fuel = (uint8_t)fuel;
    }
    IO_SIM_EXIT();
    return res;
}

io_sim_result_t io_sim_setSpeed(uint32_t speed_kmh)
{
    if (speed_kmh > IO_SPEED_MAX)
    {
        return IO_SIM_ERR_RANGE;
    }
    if (!uart_enter())
    {
        return IO_SIM_ERR_SRC;
    }
    s_speed_kmh = (uint16_t)speed_kmh;
    IO_SIM_EXIT();
    return IO_SIM_OK;
}

io_sim_result_t io_sim_setOdometer(uint32_t km)
{
    if (km > IO_ODOMETER_MAX_KM)
    {
        return IO_SIM_ERR_RANGE;
    }
    if (!uart_enter())
    {
        return IO_SIM_ERR_SRC;
    }
    s_odometer_m = km * 1000U;
    IO_SIM_EXIT();
    return IO_SIM_OK;
}

void io_sim_advanceOdometer(uint32_t metres)
{
    if (!uart_enter())
    {
        return;
    }
    s_odometer_m = (metres >= (IO_ODOMETER_MAX_M - s_odometer_m)) ? IO_ODOMETER_MAX_M : (s_odometer_m + metres);
    IO_SIM_EXIT();
}

io_sim_result_t io_sim_setHighBeam(bool on)
{
    if (!uart_enter())
    {
        return IO_SIM_ERR_SRC;
    }
    s_high_beam = on;
    IO_SIM_EXIT();
    return IO_SIM_OK;
}

io_sim_result_t io_sim_setBrakePedal(bool pressed)
{
    if (!uart_enter())
    {
        return IO_SIM_ERR_SRC;
    }
    s_brake_pedal = pressed;
    IO_SIM_EXIT();
    return IO_SIM_OK;
}

io_sim_result_t io_sim_setEngineTemp(int32_t temp_c)
{
    if (temp_c < IO_ENGINE_TEMP_MIN || temp_c > IO_ENGINE_TEMP_MAX)
    {
        return IO_SIM_ERR_RANGE;
    }
    if (!uart_enter())
    {
        return IO_SIM_ERR_SRC;
    }
    s_engine_temp_c = (int16_t)temp_c;
    IO_SIM_EXIT();
    return IO_SIM_OK;
}

io_sim_result_t io_sim_setLockFault(bool fault)
{
    io_sim_result_t res = IO_SIM_OK;
    IO_SIM_ENTER();
    if (s_src != IO_SRC_UART)
    {
        res = IO_SIM_ERR_SRC;
    }
    else
    {
        s_lock_fault = fault;
    }
    IO_SIM_EXIT();
    return res;
}

bool io_sim_isLockFault(void)
{
    return s_lock_fault;
}

io_sim_result_t io_sim_reset(void)
{
    io_sim_result_t res = IO_SIM_OK;
    IO_SIM_ENTER();
    if (s_src != IO_SRC_UART)
    {
        res = IO_SIM_ERR_SRC;
    }
    else
    {
        s_btn = 0U;
        s_rpm = 0U;
        s_fuel = 0U;
        s_lock_fault = false;
        vehicle_signals_default();
    }
    IO_SIM_EXIT();
    return res;
}

void io_sim_resolve(uint16_t phys_btn, uint8_t phys_fuel, io_sim_values_t *out)
{
    IO_SIM_ENTER();
    out->src = s_src;
    out->lock_fault = s_lock_fault;
    if (s_src == IO_SRC_UART)
    {
        out->rpm = s_rpm;
        out->fuel = s_fuel;
        out->btn = (uint16_t)(s_btn | (phys_btn & IO_SIM_LOCKS_MASK));
        if (s_rpm != 0U)
        {
            out->btn = (uint16_t)(out->btn | IO_SIM_ENGINE_MASK);
        }
        out->speed_kmh = s_speed_kmh;
        out->odometer_m = s_odometer_m;
        out->high_beam = s_high_beam;
        out->brake_pedal = s_brake_pedal;
        out->engine_temp_c = s_engine_temp_c;
    }
    else
    {
        out->btn = phys_btn;
        out->fuel = phys_fuel;
        out->rpm = ((phys_btn & IO_SIM_ENGINE_MASK) != 0U) ? s_limits.engine_on : 0U;
        out->speed_kmh = 0U;
        out->odometer_m = IO_ODOMETER_DEFAULT_M;
        out->high_beam = false;
        out->brake_pedal = false;
        out->engine_temp_c = IO_ENGINE_TEMP_DEFAULT;
    }
    IO_SIM_EXIT();
}

uint8_t io_sim_rpmToByte(uint16_t rpm)
{
    uint32_t value = (uint32_t)rpm / IO_RPM_PER_BIT;
    return (value > 0xFFU) ? 0xFFU : (uint8_t)value;
}
