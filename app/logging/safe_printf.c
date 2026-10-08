#include "safe_printf.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* RTOS and System includes */
#include "mx_freertos_app.h"
#include "stm32c5xx_hal.h"

/* UART handle declaration from HAL generated code */
#include "mx_hal_def.h"

#define PRINT_BUFFER_SIZE 256

void safe_printf(const char *tag, const char *fmt, ...)
{
    /* Protect against calling before RTOS/mutex is ready */
    if (printf_mutex == NULL) {
        return;
    }

    /* Take the mutex to ensure exclusive access to the UART */
    if (xSemaphoreTake(printf_mutex, portMAX_DELAY) == pdTRUE)
    {
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
        
        if (len > 0)
        {
            /* If string is larger than buffer, it is truncated, but we transmit what we have */
            if (len >= PRINT_BUFFER_SIZE) {
                len = PRINT_BUFFER_SIZE - 1;
            }
            
            /* mx_usart2_uart_gethandle() returns the handle to the UART instance used for logging */
            HAL_UART_Transmit(mx_usart2_uart_gethandle(), (uint8_t*)buffer, len, HAL_MAX_DELAY);
        }
        
        /* Release the mutex so other tasks can print */
        xSemaphoreGive(printf_mutex);
    }
}

