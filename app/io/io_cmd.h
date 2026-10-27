/**
 * @file io_cmd.h
 * @brief UART commands for input simulation (docs/io/io_uart_simulation.md).
 *
 *   ZRODLO#<UART|IO>, ZRODLO?, IO_HELP, IO_RESET
 *   STACYJKA, DRZWI_INNE, DRZWI_KIER, SWIATLA, AWARYJNE, MASKA, HAMULEC  #<0|1>
 *   OBROTY#<0..max> (range and step of the active car simulator, V_BOX: 0..750, multiple of 3),
 *   PALIWO#<0..100>, AWARIA_ZAMKA#<0|1>
 *   PREDKOSC#<0..250>, PRZEBIEG#<km>, DROGOWE#<0|1>, HAMULEC_NOZNY#<0|1>, TEMP_SILNIKA#<-40..150>
 * Every setting command gets one response: "[IO] OK ..." or "[IO] ERR: <reason>".
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-26
 */

#ifndef IO_CMD_H
#define IO_CMD_H

#include <stdbool.h>

/** @brief Prints one response line (tagged, without line terminator). */
typedef void (*io_cmd_out_fn)(const char *text);

/**
 * @brief Executes an input simulation command.
 * @param[in,out] line Trimmed command line (left unchanged when false is returned).
 * @param[in] out Response output.
 * @return true if the line was an input simulation command (response printed),
 *         false if not (the caller handles it, e.g. as a CAN frame).
 */
bool io_cmd_process(char *line, io_cmd_out_fn out);

#endif /* IO_CMD_H */
