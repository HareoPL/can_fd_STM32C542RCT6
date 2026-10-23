/**
 * @file stm32c5_uart_adapter.h
 * @brief STM32C5 hardware adapter for interrupt driven UART reception (USART2, ST-LINK VCP).
 */
#ifndef STM32C5_UART_ADAPTER_H
#define STM32C5_UART_ADAPTER_H

#include "uart_port.h"

/**
 * @brief Retrieves the pointer to the reception interface of USART2 on STM32C5.
 * @return Pointer to constant uart_interface_t structure.
 */
const uart_interface_t *stm32c5_uart2_interfaceGet(void);

#endif /* STM32C5_UART_ADAPTER_H */
