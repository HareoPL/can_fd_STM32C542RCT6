/**
 * @file bsp_io.h
 * @brief 
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-12
 * @LastEditors Jan Lukaszewicz
 * @LastEditTime 2026-10-13
 */

#ifndef IO_PORT_H
#define IO_PORT_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Reads the atomic state of 9 test buttons (SW0..SW8) from GPIOB and GPIOC registers.
 * @return 16-bit packed button state mask.
 */
uint16_t bsp_io_buttons_read(void);

void bsp_io_output_setPin(bool state);
bool bsp_io_output_get_state(void);
bool bsp_io_init(void);

#endif /* IO_PORT_H */
