/**
 * @file io_sim.c
 * @brief Input source selection (physical IO or UART simulation) and simulated input state.
 *
 * Written by the UART bridge task (commands), read by the io_manager task (CAN frame),
 * so every access to the shared state is done in a critical section.
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

void io_sim_setSource(io_src_t src, uint16_t phys_btn, uint8_t phys_fuel)
{
    IO_SIM_ENTER();
    if (src == IO_SRC_UART && s_src != IO_SRC_UART)
    {
        /* Snapshot: UART mode starts from the current physical state */
        s_btn = (uint16_t)(phys_btn & IO_SIM_SETTABLE_MASK);
        s_rpm = ((phys_btn & IO_SIM_ENGINE_MASK) != 0U) ? (uint16_t)IO_RPM_ENGINE_ON : 0U;
        s_fuel = (phys_fuel > IO_FUEL_MAX) ? (uint8_t)IO_FUEL_MAX : phys_fuel;
    }
    s_src = (src == IO_SRC_UART) ? IO_SRC_UART : IO_SRC_IO;
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
    if (rpm > IO_RPM_MAX)
    {
        return IO_SIM_ERR_RANGE;
    }
    if ((rpm % IO_RPM_PER_BIT) != 0U)
    {
        return IO_SIM_ERR_RPM_STEP;
    }

    io_sim_result_t res = IO_SIM_OK;
    IO_SIM_ENTER();
    if (s_src != IO_SRC_UART)
    {
        res = IO_SIM_ERR_SRC;
    }
    else
    {
        s_rpm = (uint16_t)rpm;
    }
    IO_SIM_EXIT();
    return res;
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

void io_sim_resolve(uint16_t phys_btn, uint8_t phys_fuel, io_sim_values_t *out)
{
    IO_SIM_ENTER();
    out->src = s_src;
    if (s_src == IO_SRC_UART)
    {
        out->rpm = s_rpm;
        out->fuel = s_fuel;
        out->btn = (uint16_t)(s_btn | (phys_btn & IO_SIM_LOCKS_MASK));
        if (s_rpm != 0U)
        {
            out->btn = (uint16_t)(out->btn | IO_SIM_ENGINE_MASK);
        }
    }
    else
    {
        out->btn = phys_btn;
        out->fuel = phys_fuel;
        out->rpm = ((phys_btn & IO_SIM_ENGINE_MASK) != 0U) ? (uint16_t)IO_RPM_ENGINE_ON : 0U;
    }
    IO_SIM_EXIT();
}

uint8_t io_sim_rpmToByte(uint16_t rpm)
{
    uint32_t value = (uint32_t)rpm / IO_RPM_PER_BIT;
    return (value > 0xFFU) ? 0xFFU : (uint8_t)value;
}
