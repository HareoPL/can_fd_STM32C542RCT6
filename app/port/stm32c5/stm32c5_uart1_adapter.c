/**
 * @file stm32c5_uart1_adapter.c
 * @brief STM32C5 hardware adapter for USART1 (RS485): interrupt driven reception and blocking transmission (PA6 TX / PA7 RX, 57600 baud).
 *
 * USART1 is initialized by mx_usart1_uart_init() (STM32CubeMX2, called from mx_system_init()).
 * Arduino D12 (PA6) is TX and D11 (PA7) is RX, both on CN10 (pins 13 / 15).
 *
 * Callbacks: the weak HAL_UART_RxCpltCallback() / HAL_UART_ErrorCallback() are already overridden
 * by the USART2 adapter (stm32c5_uart_adapter.c), so this adapter registers its own callbacks on the
 * USART1 handle (USE_HAL_UART_REGISTER_CALLBACKS = 1). Handles without registered callbacks keep
 * the weak (overridden) functions, so USART2 is not affected.
 *
 * Reception and error handling follow the USART2 adapter: one byte HAL_UART_Receive_IT() re-armed
 * in the completion callback, errors counted and an aborted (overrun) reception re-armed.
 */

#include "stm32c5_uart1_adapter.h"
#include "uart_port.h"
#include "stm32_hal.h"
#include "stm32c5xx_hal_uart.h"
#include "stm32c5xx_hal_cortex.h"
#include "mx_usart1.h"
#include <stddef.h>

/** @brief Byte callback and its context (called from the USART1 interrupt). */
static uart_rx_byte_fn s_uart1_rx_cb = NULL;
static void *s_uart1_rx_context = NULL;

/** @brief One byte reception buffer of the HAL transfer. */
static uint8_t s_uart1_rx_byte;

/** @brief Reception errors (FE, NE, PE, ORE) since boot. */
static volatile uint32_t s_uart1_rx_errors = 0U;

/**
 * @brief (Re)starts one byte interrupt reception; no-op if a reception is still active.
 * @return true if reception is armed.
 */
static bool stm32c5_uart1_arm_rx(void)
{
    hal_uart_handle_t *huart = mx_usart1_uart_gethandle();
    if (HAL_UART_GetRxState(huart) == HAL_UART_RX_STATE_ACTIVE)
    {
        return true;
    }
    return (HAL_UART_Receive_IT(huart, &s_uart1_rx_byte, 1U) == HAL_OK);
}

/**
 * @brief Reception complete callback registered on the USART1 handle: hands the byte over and re-arms reception.
 */
static void stm32c5_uart1_rx_cplt_callback(hal_uart_handle_t *huart, uint32_t size_byte, hal_uart_rx_event_types_t rx_event)
{
    (void)huart;
    (void)rx_event;

    uint8_t byte = s_uart1_rx_byte;
    (void)stm32c5_uart1_arm_rx();

    if (size_byte == 1U && s_uart1_rx_cb != NULL)
    {
        s_uart1_rx_cb(byte, s_uart1_rx_context);
    }
}

/**
 * @brief Reception error callback registered on the USART1 handle: counts the error and re-arms an aborted reception.
 */
static void stm32c5_uart1_error_callback(hal_uart_handle_t *huart)
{
    (void)huart;

    s_uart1_rx_errors++;
    (void)stm32c5_uart1_arm_rx();
}

static bool stm32c5_uart1_start_rx(void)
{
    hal_uart_handle_t *huart = mx_usart1_uart_gethandle();

    /* Callbacks can only be registered while no transfer is active */
    if (HAL_UART_GetRxState(huart) != HAL_UART_RX_STATE_ACTIVE)
    {
        if (HAL_UART_RegisterRxCpltCallback(huart, stm32c5_uart1_rx_cplt_callback) != HAL_OK ||
            HAL_UART_RegisterErrorCallback(huart, stm32c5_uart1_error_callback) != HAL_OK)
        {
            return false;
        }
    }

    /* Set safe interrupt priority for FreeRTOS (>= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY) */
    HAL_CORTEX_NVIC_SetPriority(USART1_IRQn, HAL_CORTEX_NVIC_PREEMP_PRIORITY_5, HAL_CORTEX_NVIC_SUB_PRIORITY_0);
    HAL_CORTEX_NVIC_EnableIRQ(USART1_IRQn);

    return stm32c5_uart1_arm_rx();
}

static void stm32c5_uart1_attach_rx_callback(uart_rx_byte_fn cb, void *context)
{
    s_uart1_rx_cb = cb;
    s_uart1_rx_context = context;
}

static uint32_t stm32c5_uart1_get_rx_errors(void)
{
    return s_uart1_rx_errors;
}

/**
 * @brief Blocking transmission on USART1 (polling mode, runs next to the interrupt driven reception).
 *        The RS485 transceiver switches DE/RE automatically, no direction pin is driven here.
 */
static bool stm32c5_uart1_transmit(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (data == NULL || len == 0U)
    {
        return false;
    }
    return (HAL_UART_Transmit(mx_usart1_uart_gethandle(), data, len, timeout_ms) == HAL_OK);
}

/**
 * @brief USART1 global interrupt (overrides the weak alias from the startup file).
 */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(mx_usart1_uart_gethandle());
}

/**
 * @brief USART1 reception / transmission interface instance.
 */
static const uart_interface_t s_uart1_interface = {
    .start_rx = stm32c5_uart1_start_rx,
    .attach_rx_callback = stm32c5_uart1_attach_rx_callback,
    .get_rx_errors = stm32c5_uart1_get_rx_errors,
    .transmit = stm32c5_uart1_transmit
};

const uart_interface_t *stm32c5_uart1_interfaceGet(void)
{
    return &s_uart1_interface;
}
