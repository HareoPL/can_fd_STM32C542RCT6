/**
 * @file stm32c5_uart1_adapter.h
 * @brief STM32C5 hardware adapter for interrupt driven UART reception (USART1, PA6 TX / PA7 RX, 57600 baud).
 */
#ifndef STM32C5_UART1_ADAPTER_H
#define STM32C5_UART1_ADAPTER_H

#include "uart_port.h"

/**
 * @brief Retrieves the pointer to the reception interface of USART1 on STM32C5.
 * @return Pointer to constant uart_interface_t structure.
 */
const uart_interface_t *stm32c5_uart1_interfaceGet(void);

#endif /* STM32C5_UART1_ADAPTER_H */
