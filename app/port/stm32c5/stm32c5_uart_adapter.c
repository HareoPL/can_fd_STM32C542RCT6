/**
 * @file stm32c5_uart_adapter.c
 * @brief STM32C5 hardware adapter for interrupt driven UART reception (USART2, ST-LINK VCP).
 *
 * Concurrency with safe_printf(): the STM32C5 HAL keeps separate tx_state and rx_state in the UART
 * handle. HAL_UART_Transmit() (polling, used by safe_printf) only switches tx_state and polls
 * TXE/TC, while HAL_UART_Receive_IT() only switches rx_state and the RXNE interrupt reads RDR.
 * Blocking transmission from a task and interrupt reception on the same handle therefore do not
 * block each other, so a one byte HAL_UART_Receive_IT() re-armed in the completion callback is used.
 *
 * Error handling: framing / noise / parity errors are non-blocking in the HAL (reception continues),
 * an overrun aborts the transfer (rx_state back to IDLE). Both end in HAL_UART_ErrorCallback(),
 * which counts the error and re-arms reception if it was aborted.
 */

#include "stm32c5_uart_adapter.h"
#include "uart_port.h"
#include "stm32_hal.h"
#include "stm32c5xx_hal_uart.h"
#include "stm32c5xx_hal_cortex.h"
#include "mx_usart2.h"
#include <stddef.h>

/** @brief Byte callback and its context (called from the USART2 interrupt). */
static uart_rx_byte_fn s_uart2_rx_cb = NULL;
static void *s_uart2_rx_context = NULL;

/** @brief One byte reception buffer of the HAL transfer. */
static uint8_t s_uart2_rx_byte;

/** @brief Reception errors (FE, NE, PE, ORE) since boot. */
static volatile uint32_t s_uart2_rx_errors = 0U;

/**
 * @brief (Re)starts one byte interrupt reception; no-op if a reception is still active.
 * @return true if reception is armed.
 */
static bool stm32c5_uart2_arm_rx(void)
{
    hal_uart_handle_t *huart = mx_usart2_uart_gethandle();
    if (HAL_UART_GetRxState(huart) == HAL_UART_RX_STATE_ACTIVE)
    {
        return true;
    }
    return (HAL_UART_Receive_IT(huart, &s_uart2_rx_byte, 1U) == HAL_OK);
}

static bool stm32c5_uart2_start_rx(void)
{
    /* Set safe interrupt priority for FreeRTOS (>= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY) */
    HAL_CORTEX_NVIC_SetPriority(USART2_IRQn, HAL_CORTEX_NVIC_PREEMP_PRIORITY_5, HAL_CORTEX_NVIC_SUB_PRIORITY_0);
    HAL_CORTEX_NVIC_EnableIRQ(USART2_IRQn);

    return stm32c5_uart2_arm_rx();
}

static void stm32c5_uart2_attach_rx_callback(uart_rx_byte_fn cb, void *context)
{
    s_uart2_rx_cb = cb;
    s_uart2_rx_context = context;
}

static uint32_t stm32c5_uart2_get_rx_errors(void)
{
    return s_uart2_rx_errors;
}

/**
 * @brief USART2 global interrupt (overrides the weak alias from the startup file).
 */
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(mx_usart2_uart_gethandle());
}

/**
 * @brief Reception complete callback from HAL: hands the byte over and re-arms reception.
 */
void HAL_UART_RxCpltCallback(hal_uart_handle_t *huart, uint32_t size_byte, hal_uart_rx_event_types_t rx_event)
{
    (void)rx_event;

    if (huart != mx_usart2_uart_gethandle())
    {
        return;
    }

    uint8_t byte = s_uart2_rx_byte;
    (void)stm32c5_uart2_arm_rx();

    if (size_byte == 1U && s_uart2_rx_cb != NULL)
    {
        s_uart2_rx_cb(byte, s_uart2_rx_context);
    }
}

/**
 * @brief Reception error callback from HAL: counts the error and re-arms an aborted reception.
 */
void HAL_UART_ErrorCallback(hal_uart_handle_t *huart)
{
    if (huart != mx_usart2_uart_gethandle())
    {
        return;
    }

    s_uart2_rx_errors++;
    (void)stm32c5_uart2_arm_rx();
}

/**
 * @brief USART2 reception interface instance.
 */
static const uart_interface_t s_uart2_interface = {
    .start_rx = stm32c5_uart2_start_rx,
    .attach_rx_callback = stm32c5_uart2_attach_rx_callback,
    .get_rx_errors = stm32c5_uart2_get_rx_errors
};

const uart_interface_t *stm32c5_uart2_interfaceGet(void)
{
    return &s_uart2_interface;
}
