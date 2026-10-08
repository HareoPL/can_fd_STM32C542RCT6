#ifndef SAFE_PRINTF_H
#define SAFE_PRINTF_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Thread-safe printf using FreeRTOS Mutex and HAL_UART
 * @param tag Module name or tag to prepend to the message (e.g. "CAN", "MAIN")
 * @param fmt Format string
 * @param ... Arguments
 */
void safe_printf(const char *tag, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* SAFE_PRINTF_H */

