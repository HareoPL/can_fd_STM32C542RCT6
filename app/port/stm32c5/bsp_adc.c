/**
 * @file bsp_adc.c
 * @brief 
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-13
 * @LastEditors Jan Lukaszewicz
 * @LastEditTime 2026-10-13
 */


 #include "bsp_adc.h"
 #include "mx_hal_def.h"
#include <stdbool.h>

 

/* Static handle for ADC1 test */
static hal_adc_handle_t test_hadc1;


/**
 * @brief Reads the raw 12-bit ADC value from PA0 (0..4095).
 * @return Raw ADC conversion result.
 */
uint32_t bsp_adc_read_raw(void)
{
  if (HAL_ADC_REG_StartConv(&test_hadc1) == HAL_OK)
  {
    if (HAL_ADC_REG_PollForConv(&test_hadc1, 10) == HAL_OK)
    {
      int32_t val = HAL_ADC_REG_ReadConversionData(&test_hadc1);
      return (val < 0) ? 0U : (uint32_t)val;
    }
  }
  return 0U;
}

/**
 * @brief Initializes ADC1 for test reading on PA0 (Arduino A0 / ADC1_IN0).
 */
bool bsp_adc_init(void)
{
  /* 1. Configure and enable ADC kernel clock */
  HAL_RCC_ADCDAC_SetKernelClkPrescaler(HAL_RCC_ADCDAC_PRESCALER4);
  HAL_RCC_ADCDAC_SetKernelClkSource(HAL_RCC_ADCDAC_CLK_SRC_HCLK);
  HAL_RCC_ADC12_EnableClock();

  /* 2. Configure PA0 as analog pin */
  HAL_RCC_GPIOA_EnableClock();
  hal_gpio_config_t gpio_config = {0};
  gpio_config.mode = HAL_GPIO_MODE_ANALOG;
  gpio_config.pull = HAL_GPIO_PULL_NO;
  if (HAL_OK != HAL_GPIO_Init(HAL_GPIOA, HAL_GPIO_PIN_0, &gpio_config))
  {
    return false;
  }

  /* 3. Initialize ADC1 handle */  
  if (HAL_OK != HAL_ADC_Init(&test_hadc1, HAL_ADC1))
  {
    return false;
  }
  /* 4. Configure ADC base parameters (12-bit resolution, normal sampling) */
  hal_adc_config_t adc_cfg = {0};
  adc_cfg.resolution = HAL_ADC_RESOLUTION_12_BIT;
  adc_cfg.sampling_mode = HAL_ADC_SAMPLING_MODE_NORMAL;
  if (HAL_OK != HAL_ADC_SetConfig(&test_hadc1, &adc_cfg))
  {
    return false;
  }

  /* 5. Configure regular conversion sequence (single conversion, software trigger) */
  hal_adc_reg_config_t reg_cfg = {0};
  reg_cfg.trigger_src = HAL_ADC_REG_TRIG_SOFTWARE;
  reg_cfg.trigger_edge = HAL_ADC_REG_TRIG_EDGE_NONE;
  reg_cfg.sequencer_length = 1;
  reg_cfg.sequencer_discont = HAL_ADC_REG_SEQ_DISCONT_DISABLE;
  reg_cfg.continuous = HAL_ADC_REG_CONV_SINGLE;
  reg_cfg.overrun = HAL_ADC_REG_OVR_DATA_OVERWRITTEN;
  if (HAL_OK != HAL_ADC_REG_SetConfig(&test_hadc1, &reg_cfg))
  {
    return false;
  }

  /* 6. Configure Channel 0 on PA0 */
  hal_adc_channel_config_t ch_cfg = {0};
  ch_cfg.group = HAL_ADC_GROUP_REGULAR;
  ch_cfg.sequencer_rank = 1;
  ch_cfg.sampling_time = HAL_ADC_SAMPLING_TIME_48CYCLES;
  ch_cfg.input_mode = HAL_ADC_IN_SINGLE_ENDED;  
  if (HAL_OK != HAL_ADC_SetConfigChannel(&test_hadc1, HAL_ADC_CHANNEL_0, &ch_cfg))
  {
    return false;
  }

  /* 7. Activate and calibrate ADC */
  if (HAL_OK != HAL_ADC_Start(&test_hadc1))
  {
    return false;
  }
  
  if (HAL_OK != HAL_ADC_Calibrate(&test_hadc1))
  {
    return false;
  }

  return true;
}




