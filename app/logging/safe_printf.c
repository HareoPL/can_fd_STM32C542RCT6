#include "safe_printf.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* RTOS and System includes */
#if defined(CMSIS_USE_RTOS_FreeRTOS_Core_11_3_1)
#include "FreeRTOS.h"
#include "task.h"
#include "mx_freertos_app.h"
#define HAVE_FREERTOS 1
#else
#define HAVE_FREERTOS 0
#endif
#include "stm32c5xx_hal.h"

/* UART handle declaration from HAL generated code */
#include "mx_hal_def.h"

#define PRINT_BUFFER_SIZE 256

/**
 * @brief Transmits a buffer on the log UART, serialized by the printf mutex once the scheduler runs.
 */
static void safe_transmit(hal_uart_handle_t *huart, const char *buf, uint32_t len)
{
#if HAVE_FREERTOS
    /* If RTOS mutex is ready and scheduler is running, take mutex */
    if (printf_mutex != NULL && xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        if (xSemaphoreTake(printf_mutex, portMAX_DELAY) == pdTRUE)
        {
            HAL_UART_Transmit(huart, buf, len, HAL_MAX_DELAY);
            xSemaphoreGive(printf_mutex);
        }
    }
    else
    {
        /* Pre-scheduler / early boot: transmit directly via polling */
        HAL_UART_Transmit(huart, buf, len, 1000);
    }
#else
    /* Bare-metal / Bootloader: transmit directly via polling */
    HAL_UART_Transmit(huart, buf, len, 1000);
#endif
}

void safe_printf(const char *tag, const char *fmt, ...)
{
    hal_uart_handle_t *huart = mx_usart2_uart_gethandle();
    if (huart == NULL) {
        return;
    }

    char buffer[PRINT_BUFFER_SIZE];
    va_list args;
    int len = 0;
    
    /* Add the tag prefix if provided */
    if (tag != NULL) {
        len = snprintf(buffer, PRINT_BUFFER_SIZE, "[%s] ", tag);
    }

    if (len >= 0 && len < PRINT_BUFFER_SIZE) {
        va_start(args, fmt);
        len += vsnprintf(buffer + len, PRINT_BUFFER_SIZE - len, fmt, args);
        va_end(args);
    }
    
    if (len <= 0) {
        return;
    }

    if (len >= PRINT_BUFFER_SIZE) {
        len = PRINT_BUFFER_SIZE - 1;
    }

    safe_transmit(huart, buffer, (uint32_t)len);
}

void safe_write(const char *buf, size_t len)
{
    hal_uart_handle_t *huart = mx_usart2_uart_gethandle();
    if (huart == NULL || buf == NULL || len == 0U) {
        return;
    }

    safe_transmit(huart, buf, (uint32_t)len);
}

