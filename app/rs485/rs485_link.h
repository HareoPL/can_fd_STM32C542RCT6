/**
 * @file rs485_link.h
 * @brief RS485 link to the external telematics device (LX, LX_micro, Roma, Viale): periodic superStatus
 *        and detection of a device parser change (docs/rs485/rs485_link.md).
 *
 * The task requests superStatus every RS485_LINK_POLL_PERIOD_MS and only on a silent bus. It stays
 * passive while another master (PC service tool) talks to the device and while a file (device
 * parser NNN.CAN) is transferred; superStatus responses requested by the service tool are decoded too.
 *
 * Naming: "device parser" (dev_parser_no) is the CAN parser loaded in the external device,
 * not a v-box CAN parser profile (can_parser_*, cp_*).
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-26
 */

#ifndef RS485_LINK_H
#define RS485_LINK_H

#include <stdbool.h>
#include <stdint.h>
#include "uart_port.h"
#include "superstatus.h"

#define RS485_DEV_PARSER_NO_UNKNOWN  0xFFFFU  /**< Device parser number not read yet */

/**
 * @brief Device parser change notification (called from the RS485 task, must not block).
 * @param[in] can_bus Device CAN bus (1 or 2).
 * @param[in] old_no Previous device parser number, RS485_DEV_PARSER_NO_UNKNOWN on the first reading.
 * @param[in] new_no New device parser number (0 = no parser).
 */
typedef void (*rs485_link_parser_change_fn)(uint8_t can_bus, uint16_t old_no, uint16_t new_no);

/** @brief Prints one console response line (tagged, without line terminator). */
typedef void (*rs485_link_out_fn)(const char *text);

/**
 * @brief Initializes the link and creates its task (reception starts when the task runs).
 * @param[in] uart UART interface with transmit (e.g. stm32c5_uart1_interfaceGet()).
 * @return true on success, false on invalid parameters or out of memory.
 */
bool rs485_link_init(const uart_interface_t *uart);

/**
 * @brief Registers the device parser change callback (call before rs485_link_init()).
 * @param[in] cb Callback, NULL to disable.
 */
void rs485_link_set_parser_change_cb(rs485_link_parser_change_fn cb);

/**
 * @brief Copies the last decoded superStatus.
 * @param[out] status Last superStatus.
 * @param[out] age_ms Time since it was received (may be NULL).
 * @return false if no superStatus was received yet.
 */
bool rs485_link_get_status(superstatus_t *status, uint32_t *age_ms);

/**
 * @brief Executes an RS485 console command (RS485?, RS485#POLL, RS485_HELP).
 * @param[in] line Trimmed command line.
 * @param[in] out Response output.
 * @return true if the line was an RS485 command (response printed), false otherwise.
 */
bool rs485_link_cmd_process(const char *line, rs485_link_out_fn out);

#endif /* RS485_LINK_H */
