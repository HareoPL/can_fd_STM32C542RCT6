/**
 * @file bsp_io.c
 * @brief 
 *      Przyciski (SW0..SW7) – stan spoczynkowy 0 not connect, naciśnięcie podaje +3.3 V:
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-12
 * @LastEditors Jan Lukaszewicz
 * @LastEditTime 2026-10-14
 */
#include "bsp_io.h"
#include "mx_hal_def.h"
#include "stm32c5xx_hal_gpio.h"


/**
 * @brief Initializes the 9 test button pins on GPIOB (PB2, PB4, PB7, PB12..PB15)
 *        and GPIOC (PC7, PC8) as inputs with internal pull-down resistors.
 * @return true on success, false on initialization error.
 */
static bool bsp_io_buttons_init(void)
{
  /* Enable GPIOB and GPIOC peripheral clocks */
  HAL_RCC_GPIOB_EnableClock();
  HAL_RCC_GPIOC_EnableClock();

  hal_gpio_config_t gpio_config;
  gpio_config.mode        = HAL_GPIO_MODE_INPUT;
  gpio_config.pull        = HAL_GPIO_PULL_DOWN;
  gpio_config.speed       = HAL_GPIO_SPEED_FREQ_LOW;
  gpio_config.output_type = HAL_GPIO_OUTPUT_PUSHPULL;

  uint32_t pins_b = HAL_GPIO_PIN_2 | HAL_GPIO_PIN_4 | HAL_GPIO_PIN_7 |
                    HAL_GPIO_PIN_12 | HAL_GPIO_PIN_13 | HAL_GPIO_PIN_14 | HAL_GPIO_PIN_15;

  if (HAL_OK != HAL_GPIO_Init(HAL_GPIOB, pins_b, &gpio_config))
  {
    return false;
  }

  uint32_t pins_c = HAL_GPIO_PIN_7 | HAL_GPIO_PIN_8;

  if (HAL_OK != HAL_GPIO_Init(HAL_GPIOC, pins_c, &gpio_config))
  {
    return false;
  }

  return true;
}

/**
 * @brief Reads the state of 9 test buttons from GPIOB and GPIOC IDR registers.
 * @return 16-bit packed state:
 *         ACC stacyjka    - SW0 -> bit 0 -> PB4  (CN10 pin 27)
 *         Drzwi INNE      - SW1 -> bit 1 -> PC8  (CN10 pin 31)
 *         Drzwi KIER      - SW2 -> bit 2 -> PC7  (CN10 pin 19)
 *         Światła         - SW3 -> bit 3 -> PB7  (CN10 pin 5)
 *         Zamki ZAMKNIĘTE - SW4 -> bit 4 -> PB12 (CN10 pin 16)
 *         Awaryjne        - SW5 -> bit 5 -> PB13 (CN10 pin 30)
 *         Maska           - SW6 -> bit 6 -> PB14 (CN10 pin 28)
 *         Hamulec RĘCZNY  - SW7 -> bit 7 -> PB15 (CN10 pin 26)
 *         Obroty silnika  - SW8 -> bit 8 -> PB2  (CN10 pin 22)
 */
uint16_t bsp_io_buttons_read(void)
{
  uint32_t idr_b = GPIOB->IDR;
  uint32_t idr_c = GPIOC->IDR;

  uint16_t state = 0U;

  /* Bit 0: PB4 (SW0 - ACC) */
  if ((idr_b & HAL_GPIO_PIN_4) != 0U)
  {
    state |= (1U << 0U);
  }

  /* Bit 1: PC8 (SW1 - Drzwi INNE) */
  if ((idr_c & HAL_GPIO_PIN_8) != 0U)
  {
    state |= (1U << 1U);
  }

  /* Bit 2: PC7 (SW2 - Drzwi KIER) */
  if ((idr_c & HAL_GPIO_PIN_7) != 0U)
  {
    state |= (1U << 2U);
  }

  /* Bit 3: PB7 (SW3 - Światła) */
  if ((idr_b & HAL_GPIO_PIN_7) != 0U)
  {
    state |= (1U << 3U);
  }

  /* Bit 4: PB12 (SW4 - Zamki ZAMKNIĘTE) */
  if ((idr_b & HAL_GPIO_PIN_12) != 0U)
  {
    state |= (1U << 4U);
  }

  /* Bit 5: PB13 (SW5 - Awaryjne) */
  if ((idr_b & HAL_GPIO_PIN_13) != 0U)
  {
    state |= (1U << 5U);                                                         
  }

  /* Bit 6: PB14 (SW6 - Maska) */
  if ((idr_b & HAL_GPIO_PIN_14) != 0U)
  {
    state |= (1U << 6U);
  }

  /* Bit 7: PB15 (SW7 - Hamulec RĘCZNY) */
  if ((idr_b & HAL_GPIO_PIN_15) != 0U)
  {
    state |= (1U << 7U);
  }

  /* Bit 8: PB2 (SW8 - Obroty silnika) */
  if ((idr_b & HAL_GPIO_PIN_2) != 0U)
  {
    state |= (1U << 8U);
  }

  return state;
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
 * @brief Set the test output pin PA8. pin 23
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


  