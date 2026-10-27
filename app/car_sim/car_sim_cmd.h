/**
 * @file car_sim_cmd.h
 * @brief UART commands of the car simulator (docs/car_sim/car_sim_tasks.md, T7).
 *
 *   SIM?, SIM_LIST, SIM_HELP
 *   SIM#<KEY|device parser number>, SIM#AUTO
 *   SIM_PIN#<hex4>, SIM_PIN#DOMYSLNY
 *   PILOT#<OTWORZ|ZAMKNIJ>
 * Every command gets at least one response line tagged "[SIM] ": "OK ...", "ERR: <reason>" or a status.
 * Selections are requests executed by the car_sim task (car_sim_request_*()), so the
 * "[CAR_SIM] symulator: ..." log follows the response within one tick.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#ifndef CAR_SIM_CMD_H
#define CAR_SIM_CMD_H

#include <stdbool.h>

/** @brief Prints one response line (tagged, without line terminator). */
typedef void (*car_sim_cmd_out_fn)(const char *text);

/**
 * @brief Executes a car simulator command.
 * @param[in] line Trimmed command line (not modified).
 * @param[in] out Response output.
 * @return true if the line was a car simulator command (response printed),
 *         false if not (the caller handles it, e.g. as a CAN frame).
 */
bool car_sim_cmd_process(const char *line, car_sim_cmd_out_fn out);

#endif /* CAR_SIM_CMD_H */
