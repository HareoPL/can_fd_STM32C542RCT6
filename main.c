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
#include "can_parser.h"
#include "stm32c5_can_adapter.h"
#include "io_manager.h"
#include "ota_manager.h"
#include "version.h"
#include "safe_printf.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Alokacja instancji dla CAN1 i CAN2 */
can_instance_t can1_inst;
can_instance_t can2_inst;

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
  system_status_t sys_status = mx_system_init();
  if (sys_status != SYSTEM_OK)
  {
    /* Fast toggle LD1 (PA5) to signal fatal hardware/clock initialization error */
    HAL_RCC_GPIOA_EnableClock();
    hal_gpio_config_t led_cfg = {
        .mode        = HAL_GPIO_MODE_OUTPUT,
        .pull        = HAL_GPIO_PULL_NO,
        .speed       = HAL_GPIO_SPEED_FREQ_LOW,
        .output_type = HAL_GPIO_OUTPUT_PUSHPULL
    };
    (void)HAL_GPIO_Init(HAL_GPIOA, HAL_GPIO_PIN_5, &led_cfg);

    while (1)
    {
      HAL_GPIO_TogglePin(HAL_GPIOA, HAL_GPIO_PIN_5);
      for (volatile uint32_t i = 0; i < 1000000UL; i++)
      {
        __NOP();
      }
    }
  }

  /* Print system boot banner with FW/HW version and build metadata */
  version_printBootBanner();

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
    safe_printf(NULL, "[MAIN] ERROR: CAN1 manager init failed!\r\n");
    return (-1);
  }

  if (!can_parser_select(&can1_inst, CAN_PARSER_ID_V_BOX))
  {
    safe_printf(NULL, "[MAIN] ERROR: CAN1 parser select failed!\r\n");
    return (-1);
  }

  /* Attach Bootloader/OTA listener to CAN1 alongside V_BOX parser (dual-bus support) */
  if (!can_parser_attach_boot_ota(&can1_inst))
  {
    safe_printf(NULL, "[MAIN] ERROR: CAN1 attach BOOT_OTA parser failed!\r\n");
    return (-1);
  }

  /* 3. Inicjalizacja instancji CAN2 dla Bootloadera / OTA (Classic CAN 2.0 @ 500 kbps) */
  can2_inst.can_config.frame_format = CAN_MSG_FRAME_FORMAT_CAN;
  can2_inst.can_config.baudrate_normal = CAN_BAUDRATE_500K;
  can2_inst.can_config.baudrate_FD = CAN_BAUDRATE_CUSTOM;

  if (!can_manager_init(&can2_inst, stm32c5_can2_interfaceGet(), "CAN2"))
  {
    safe_printf(NULL, "[MAIN] ERROR: CAN2 manager init failed!\r\n");
    return (-1);
  }

  if (!can_parser_select(&can2_inst, CAN_PARSER_ID_EXAMPLE))
  {
    safe_printf(NULL, "[MAIN] ERROR: CAN2 parser select failed!\r\n");
    return (-1);
  }

  /* 4. Inicjalizacja menedżera aktualizacji OTA (Dummy Update Worker i ISO-TP na CAN1) */
  if (!ota_manager_init(&can1_inst))
  {
    safe_printf(NULL, "[MAIN] ERROR: OTA manager init failed!\r\n");
    return (-1);
  }

  if(!io_manager_init())
  {
    safe_printf(NULL, "[MAIN] ERROR: IO manager init failed!\r\n");
    return (-1);
  }
  
  /* Initialize FreeRTOS tasks */
  if (app_synctasks_init() != 0)
  {
    safe_printf(NULL, "[MAIN] ERROR: app_synctasks_init failed!\r\n");
    return (-1);
  }

  /* Start the FreeRTOS scheduler */
  vTaskStartScheduler();

  /* We should never get here as control is now taken by the scheduler */
  while (1) {
  }
} /* end main */

