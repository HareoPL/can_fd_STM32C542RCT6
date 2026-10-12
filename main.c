/**
  ******************************************************************************
 * @file main.c
  * brief          : Main program body
  *                  Calls target system initialization then loop in main.
  ******************************************************************************
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "mx_gpio_default.h"
#include "stm32c5xx_hal.h"
#include "stm32c5xx_hal_gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "mx_freertos_app.h"

#include "can_manager.h"
#include "stm32c5_can_adapter.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Alokacja instancji dla CAN1 */
can_instance_t can1_inst;

/* Private functions prototype -----------------------------------------------*/

/**
  * brief:  The application entry point.
  * retval: none but we specify int to comply with C99 standard
  */
int main(void)
{
  /** System Init: this code placed in targets folder initializes your system.
    * It calls the initialization (and sets the initial configuration) of the peripherals.
    * You can use STM32CubeMX to generate and call this code or not in this project.
    * It also contains the HAL initialization and the initial clock configuration.
    */
  if (mx_system_init() != SYSTEM_OK)
  {
    return (-1);
  }

  /* 2. Inicjalizacja instancji CAN1 (domyślna konfiguracja 500k lub z can1_inst.can_config) */
 // CAN NORMAL
  can1_inst.can_config.frame_format = CAN_MSG_FRAME_FORMAT_CAN; // CAN_MSG_FRAME_FORMAT_FD_CAN
  can1_inst.can_config.baudrate_normal = CAN_BAUDRATE_500K;

  // todo przetestowac CANFD
  // trzeba tez w main_task zmienic testowa ramke test_msg.format = CAN_MSG_FRAME_FORMAT_FD_CAN;
  // can1_inst.can_config.frame_format = CAN_MSG_FRAME_FORMAT_FD_CAN; // CAN_MSG_FRAME_FORMAT_CAN
  // can1_inst.can_config.baudrate_normal = CAN_BAUDRATE_500K;
  // can1_inst.can_config.baudrate_FD = CAN_BAUDRATE_500K;

  if (!can_manager_init(&can1_inst, stm32c5_can1_interfaceGet(), "CAN1"))
  {
    return (-1);
  }

  /* Initialize FreeRTOS tasks */
  if (app_synctasks_init() != 0)
  {
    return (-1);
  }

  /* Start the FreeRTOS scheduler */
  vTaskStartScheduler();

  /* We should never get here as control is now taken by the scheduler */
  while (1) {
  }
} /* end main */

