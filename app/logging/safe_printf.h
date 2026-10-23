#ifndef SAFE_PRINTF_H
#define SAFE_PRINTF_H

#include <stddef.h>

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

/**
 * @brief Thread-safe raw write of a pre-formatted buffer (no length limit, written in one piece)
 * @param buf Text to send
 * @param len Number of bytes
 */
void safe_write(const char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* SAFE_PRINTF_H */

