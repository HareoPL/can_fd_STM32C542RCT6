/**
 * @file bsp_adc.h
 * @brief 
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-13
 * @LastEditors Jan Lukaszewicz
 * @LastEditTime 2026-10-13
 */
#ifndef BSP_ADC_H
#define BSP_ADC_H

#include <stdint.h>
#include <stdbool.h>



uint32_t bsp_adc_read_raw(void);
bool bsp_adc_init(void);


#endif /* BSP_ADC_H */
