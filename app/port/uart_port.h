/**
 * @file uart_port.h
 * @brief Hardware abstraction layer contract for interrupt driven UART reception and optional transmission.
 *
 * The log UART (USART2) transmits through safe_printf() (shared UART handle), so its adapter leaves
 * transmit NULL. Ports with their own peer (e.g. USART1 / RS485) implement transmit.
 */

#ifndef UART_PORT_H
#define UART_PORT_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Callback invoked from the UART interrupt for every received byte.
 * @param[in] byte Received byte.
 * @param[in] context User context passed when attaching the callback.
 * @note Runs in ISR context: only hand the byte over (e.g. FreeRTOS ...FromISR API), never block.
 */
typedef void (*uart_rx_byte_fn)(uint8_t byte, void *context);

/**
 * @brief Generic hardware interface structure for UART reception and transmission.
 */
typedef struct {
    bool (*start_rx)(void);                                        /**< Arms interrupt driven reception */
    void (*attach_rx_callback)(uart_rx_byte_fn cb, void *context); /**< Attach before start_rx() */
    uint32_t (*get_rx_errors)(void);                               /**< Framing / noise / overrun errors since boot (optional, may be NULL) */
    bool (*transmit)(const uint8_t *data, uint16_t len, uint32_t timeout_ms); /**< Blocking transmission, task context only (optional, may be NULL) */
} uart_interface_t;

#endif /* UART_PORT_H */
