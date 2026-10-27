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
#include "car_sim.h"
#include "ota_manager.h"
#include "version.h"
#include "safe_printf.h"
#if defined(CAN_FD_TEST) && (CAN_FD_TEST == 1)
#include "can_fd_test.h"
#endif
#if defined(CAN_UART_BRIDGE) && (CAN_UART_BRIDGE == 1)
#include "can_uart_bridge.h"
#include "stm32c5_uart_adapter.h"
#endif
#if defined(RS485_LINK) && (RS485_LINK == 1)
#include "rs485_link.h"
#include "stm32c5_uart1_adapter.h"
#endif

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Alokacja instancji dla CAN1 i CAN2 */
can_instance_t can1_inst;
can_instance_t can2_inst;

/* Private functions prototype -----------------------------------------------*/
#if defined(RS485_LINK) && (RS485_LINK == 1)
/**
 * @brief Device parser change on the external telematics device (RS485 task context).
 *        CAN1: the number is passed to the car simulator (AUTO mode selects the matching simulator,
 *        executed by the car_sim task, non-blocking). CAN2: logged only.
 */
static void on_dev_parser_change(uint8_t can_bus, uint16_t old_no, uint16_t new_no)
{
  if (old_no == RS485_DEV_PARSER_NO_UNKNOWN)
  {
    safe_printf(NULL, "[RS485] parser urządzenia CAN%u: %u\r\n", (unsigned)can_bus, (unsigned)new_no);
  }
  else
  {
    safe_printf(NULL, "[RS485] ZMIANA parsera urządzenia CAN%u: %u -> %u\r\n", (unsigned)can_bus,
                (unsigned)old_no, (unsigned)new_no);
  }

  if (can_bus == 1U)
  {
    car_sim_request_dev_parser(new_no);
  }
}
#endif

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

  /* 2. Inicjalizacja instancji CAN1 (tryb i prędkości ustawia wybrany profil parsera) */
  if (!can_manager_init(&can1_inst, stm32c5_can1_interfaceGet(), "CAN1"))
  {
    safe_printf(NULL, "[MAIN] ERROR: CAN1 manager init failed!\r\n");
    return (-1);
  }

#if defined(CAN_FD_TEST) && (CAN_FD_TEST == 1)
  /* CAN FD test build: CP_FD_TEST parser (with BOOT_OTA) replaces V_BOX on CAN1. On the test bench CAN2
   * shares the physical bus, so a fixed Classic 500 kbps CAN2 would disturb every other bit rate:
   * - CAN_FD_TEST_LOOP=1: CAN2 is the loop partner and follows every configuration; the PC tool can also
   *   select it as the bus under test (SELECT_BUS, tools/can_fd_test.py --bus 2),
   * - otherwise CAN2 is not started (stays in INIT, silent on the bus). */
#if defined(CAN_FD_TEST_LOOP) && (CAN_FD_TEST_LOOP == 1)
  if (!can_manager_init(&can2_inst, stm32c5_can2_interfaceGet(), "CAN2") ||
      !can_fd_test_init(&can1_inst, &can2_inst))
#else
  if (!can_fd_test_init(&can1_inst, NULL))
#endif
  {
    safe_printf(NULL, "[MAIN] ERROR: CAN1 FD test init failed!\r\n");
    return (-1);
  }

  /* CAN1 belongs to the FD test: the car simulator only sends the V_BOX telemetry, no profile changes */
  if (!car_sim_init(&can1_inst, false))
  {
    safe_printf(NULL, "[MAIN] ERROR: car simulator init failed!\r\n");
    return (-1);
  }
#else
  /* Car simulator: selects V_BOX (AUTO mode) on CAN1 with the BOOT_OTA filters attached (D9) */
  if (!car_sim_init(&can1_inst, true))
  {
    safe_printf(NULL, "[MAIN] ERROR: car simulator init failed!\r\n");
    return (-1);
  }
#endif

#if !defined(CAN_FD_TEST) || (CAN_FD_TEST == 0)
  /* 3. Inicjalizacja instancji CAN2 (tryb i prędkości ustawia wybrany profil parsera) */
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
#endif

  /* 4. Inicjalizacja menedżera aktualizacji OTA (Dummy Update Worker i ISO-TP na CAN1) */
  if (!ota_manager_init(&can1_inst))
  {
    safe_printf(NULL, "[MAIN] ERROR: OTA manager init failed!\r\n");
    return (-1);
  }

#if defined(CAN_UART_BRIDGE) && (CAN_UART_BRIDGE == 1)
  /* 5. Most UART -> CAN (ramki tekstowe Serial Commandera z USART2). CAN2 tylko gdy jest uruchomiony. */
#if defined(CAN_FD_TEST) && (CAN_FD_TEST == 1) && !(defined(CAN_FD_TEST_LOOP) && (CAN_FD_TEST_LOOP == 1))
  can_instance_t *bridge_can2 = NULL;
#else
  can_instance_t *bridge_can2 = &can2_inst;
#endif
  if (!can_uart_bridge_init(stm32c5_uart2_interfaceGet(), &can1_inst, bridge_can2))
  {
    safe_printf(NULL, "[MAIN] ERROR: CAN UART bridge init failed!\r\n");
    return (-1);
  }
#endif

#if defined(RS485_LINK) && (RS485_LINK == 1)
  /* 6. Łącze RS485 z urządzeniem telematycznym (USART1): superStatus i wykrywanie zmiany parsera urządzenia */
  rs485_link_set_parser_change_cb(on_dev_parser_change);
  if (!rs485_link_init(stm32c5_uart1_interfaceGet()))
  {
    safe_printf(NULL, "[MAIN] ERROR: RS485 link init failed!\r\n");
    return (-1);
  }
#endif

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

  /* Report remaining FreeRTOS heap after all tasks and queues are created */
  safe_printf(NULL, "[MAIN] FreeRTOS heap free: %u B\r\n", (unsigned int)xPortGetFreeHeapSize());

  /* Start the FreeRTOS scheduler */
  vTaskStartScheduler();

  /* We should never get here as control is now taken by the scheduler */
  while (1) {
  }
} /* end main */

