/**
 * @file bsp_io.c
 * @brief 
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-12
 * @LastEditors Jan Lukaszewicz
 * @LastEditTime 2026-10-13
 */
#include "bsp_io.h"
#include "mx_hal_def.h"
#include "stm32c5xx_hal_gpio.h"


/**
 * @brief Initializes the 8 test button pins on GPIOB (PB4..PB7, PB12..PB15)
 *        as inputs with internal pull-down resistors.
 */
static bool bsp_io_buttons_init(void)
{
  /* Enable GPIOB peripheral clock */
  HAL_RCC_GPIOB_EnableClock();

  hal_gpio_config_t gpio_config;
  gpio_config.mode = HAL_GPIO_MODE_INPUT;
  gpio_config.pull = HAL_GPIO_PULL_DOWN;
  
  uint32_t pins = 0;
  pins = HAL_GPIO_PIN_4 | HAL_GPIO_PIN_5 | HAL_GPIO_PIN_6 | HAL_GPIO_PIN_7 |
                  HAL_GPIO_PIN_12 | HAL_GPIO_PIN_13 | HAL_GPIO_PIN_14 | HAL_GPIO_PIN_15;

 if (HAL_OK !=  HAL_GPIO_Init(HAL_GPIOB, pins, &gpio_config))
 {
    return false;
 }

  return true;
}

/**
 * @brief Reads the atomic state of 8 test buttons from GPIOB IDR register.
 * @return 8-bit packed state:
 *         bit 0 -> PB4  (CN10 pin 27)
 *         bit 1 -> PB5  (CN10 pin 6) 
 *         bit 2 -> PB6  (CN10 pin 3)
 *         bit 3 -> PB7  (CN10 pin 5)
 *         bit 4 -> PB12 (CN10 pin 16)
 *         bit 5 -> PB13 (CN10 pin 30)
 *         bit 6 -> PB14 (CN10 pin 28)
 *         bit 7 -> PB15 (CN10 pin 26)
 */
uint8_t bsp_io_buttons_read(void)
{
  uint32_t idr = GPIOB->IDR;
  return (uint8_t)(((idr >> 4) & 0x0F) | ((idr >> 8) & 0xF0));
}

/**
 * @brief Initializes test GPIO output pin PA8 (Arduino D7 / CN10 pin 23) in Push-Pull mode.
 */
static bool bsp_io_output_init(void)
{
  /* Enable GPIOA peripheral clock */
  HAL_RCC_GPIOA_EnableClock();

  hal_gpio_config_t gpio_config;
  gpio_config.mode        = HAL_GPIO_MODE_OUTPUT;
  gpio_config.output_type = HAL_GPIO_OUTPUT_PUSHPULL;
  gpio_config.speed       = HAL_GPIO_SPEED_FREQ_LOW;
  gpio_config.pull        = HAL_GPIO_PULL_NO;
  gpio_config.init_state  = HAL_GPIO_PIN_RESET;

  if (HAL_OK != HAL_GPIO_Init(HAL_GPIOA, HAL_GPIO_PIN_8, &gpio_config))
  {
    return false;
  }

  return true;
}

/**
 * @brief Set the test output pin PA8.
 * @arg state true if HIGH, false if LOW.
 */
void bsp_io_output_setPin(bool state)
{  
  HAL_GPIO_WritePin(HAL_GPIOA, HAL_GPIO_PIN_8, state);
}

/**
 * @brief Reads the current output state of PA8.
 * @return true if HIGH, false if LOW.
 */
bool bsp_io_output_get_state(void)
{
  return (HAL_GPIO_ReadPin(HAL_GPIOA, HAL_GPIO_PIN_8) == HAL_GPIO_PIN_SET);
}


bool bsp_io_init(void)
{

  if (false == bsp_io_buttons_init())
  {
    return false;
  }

  if (false == bsp_io_output_init())
  {
    return false;
  }
  
  return true;
}


  