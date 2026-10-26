/**
 * @file can_uart_bridge.h
 * @brief UART to CAN bridge: Serial Commander text frames (cansend / CAN FD / TLV) sent on CAN1 or CAN2.
 *
 * One line per command, terminated by CR, LF or CRLF (docs/can_bus/can_uart_bridge.md):
 *   [CAN1:|CAN2:]<frame>                frame syntax of can_support.parse_can_frame()
 *   CFG#<bus>#<CLASSIC|FD|FD_BRS>#<nominal>[#<data>], CFG?, BUS#<n>, MODE#<mode>, HELP
 *   input simulation commands of io_cmd.c (IO_HELP, ZRODLO#..., OBROTY#..., ...), tag [IO]
 * Responses and errors are printed with the [CAN_UART] tag on the log UART.
 */

#ifndef CAN_UART_BRIDGE_H
#define CAN_UART_BRIDGE_H

#include <stdbool.h>
#include "uart_port.h"
#include "can_manager.h"

/**
 * @brief Initializes the bridge and creates its task (reception starts when the task runs).
 * @param[in] uart UART reception interface (e.g. stm32c5_uart2_interfaceGet()).
 * @param[in] can1 CAN1 instance (initialized by can_manager_init()).
 * @param[in] can2 CAN2 instance, NULL if not available (commands for CAN2 are rejected).
 * @return true on success, false on invalid parameters or out of memory.
 */
bool can_uart_bridge_init(const uart_interface_t *uart, can_instance_t *can1, can_instance_t *can2);

#endif /* CAN_UART_BRIDGE_H */
