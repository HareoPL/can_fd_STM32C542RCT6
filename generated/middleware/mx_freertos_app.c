/**
  ******************************************************************************
 * @file mx_freertos_app.c
  * @brief          : FreeRTOS initialization
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the mx_freertos_license.md file
  * in the same directory as the generated code.
  * If no mx_freertos_license.md file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "mx_freertos_app.h"
#include "safe_printf.h"
#include "can_msg_types.h"
#include "can_manager.h"
#include "main.h"
#include "queue.h"

#include "tasks_config.h"

/* Private define ------------------------------------------------------------*/
#define TAG "MAIN"

#ifndef FDCAN_CLASSIC_CAN
#define FDCAN_CLASSIC_CAN 0U
#endif
#ifndef FDCAN_FD_CAN
#define FDCAN_FD_CAN 1U
#endif

/* Private typedef -----------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/*-------------------- Tasks definition --------------------------------------*/
/* Definitions for main_task */
static TaskHandle_t main_task_Handle;
/* Definitions for heartbeat_task */
static TaskHandle_t heartbeat_task_Handle;

/* Global mutex for thread-safe printf */
SemaphoreHandle_t printf_mutex = NULL;

/* Queue for CAN1 TX messages (mapped to can1_inst.tx_queue) */
QueueHandle_t CAN_1_queue = NULL;

/* Private functions prototype -----------------------------------------------*/
/* Tasks entry function ------------------------------------------------------*/
static void main_task(void *pvParameters);
static void heartbeat_task(void *pvParameters);
static void can1_tx_task(void *pvParameters) __attribute__((unused));
static void can1_rx_task(void *pvParameters) __attribute__((unused));

/**
  * @brief  Verifies if there is enough free heap for the scheduler to start.
  *         It checks if we have memory for at least the Idle task.
  * @retval 0 if OK, -1 if not enough memory.
  */
static int32_t verify_remaining_heap(void)
{
  /* Get remaining FreeRTOS heap size in bytes */
  size_t free_heap_bytes = xPortGetFreeHeapSize();
  
  /* Estimate required memory for Idle task (Stack + TCB margin) */
  size_t required_for_scheduler = (configMINIMAL_STACK_SIZE * 4) + 128;
  
  /* You can inspect free_heap_bytes in the debugger here */
  if (free_heap_bytes < required_for_scheduler)
  {
    return -1; /* Not enough memory to start the scheduler */
  }
  
  return 0;
}

/**
  * @brief Initializes FreeRTOS kernel objects.
  * @param None
  * @retval int32_t Returns 0 on success, -1 on failure.
  */
int32_t app_synctasks_init (void)
{
  
  BaseType_t ret;

  /* Create the global printf mutex */
  printf_mutex = xSemaphoreCreateMutex();
  if (printf_mutex == NULL)
  {
      return -1;
  }

  /* Alias CAN1 TX queue from can1_inst */
  CAN_1_queue = can1_inst.tx_queue;

  /* main_task creation-------------------------------------*/
  ret = xTaskCreate(main_task, "main_task", TASK_STACK_MAIN,
                    (void*) NULL, TASK_PRIO_MAIN, &main_task_Handle);
  if (ret != pdPASS){return -1;}

  /* heartbeat_task creation-------------------------------------*/
  ret = xTaskCreate(heartbeat_task, "heartbeat_task", TASK_STACK_HEARTBEAT,
                    (void*) NULL, TASK_PRIO_HEARTBEAT, &heartbeat_task_Handle);
 if (ret != pdPASS){return -1;}

  /* Verify if enough heap remains to start the scheduler */
  if (verify_remaining_heap() != 0)
  {
    safe_printf("SYSTEM", "heap error \r\n");
    return -1;
  }

  return 0;
}

/* Tasks entry function ------------------------------------------------------*/
/**
  * @brief Function implementing the main_task thread.
  * @param pvParameters: A pointer to the parameters passed to the task.
  * @retval None
  */
  CAN_msg_t test_msg = {0};
static void main_task(void *pvParameters)
{
  (void)pvParameters;
  uint32_t counter = 0;

  for(;;)
  {
    safe_printf(TAG, "main task \r\n");
    
    /* Create a test CAN message */
    
    // test_msg.format = CAN_MSG_FRAME_FORMAT_CAN; // todo aktualnie nie bierze tego pod uwage podczas wysylki. 
    // test_msg.format = CAN_MSG_FRAME_FORMAT_FD_CAN;
    
    // test_msg.ident = CAN_msg_set_standard_id(0x1AA); /* Standard ID = 0x1AA */
    test_msg.ident = CAN_msg_set_extended_id(0x1FFFFFFF); /* extended ID */
    test_msg.len = 8;
    test_msg.data[0] = 0xDE;
    test_msg.data[1] = 0xAD;
    test_msg.data[2] = 0xBE;
    test_msg.data[3] = 0xEF;
    test_msg.data[4] = (uint8_t)(counter >> 24);
    test_msg.data[5] = (uint8_t)(counter >> 16);
    test_msg.data[6] = (uint8_t)(counter >> 8);
    test_msg.data[7] = (uint8_t)(counter & 0xFF);
    
    /* Send it to the CAN1 manager */
    if (!can_manager_send(&can1_inst, &test_msg))
    {
        safe_printf(TAG, "Failed to enqueue CAN message! Queue full?\r\n");
    }
    
    counter++;

    vTaskDelay(pdMS_TO_TICKS(1200));
  }
}

/**
  * @brief Function implementing the heartbeat_task thread.
  * @param pvParameters: A pointer to the parameters passed to the task.
  * @retval None
  */
static void heartbeat_task(void *pvParameters)
{
  (void)pvParameters;

  for(;;)
  {
    HAL_GPIO_TogglePin(LD1_PORT, LD1_PIN);
    
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

/**
  * @brief Function implementing the can1_tx_task thread (migrated to can_manager).
  * @param pvParameters: A pointer to the parameters passed to the task.
  * @retval None
  */
static void can1_tx_task(void *pvParameters)
{
  (void)pvParameters;

  for(;;)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

/**
  * @brief Function implementing the can1_rx_task thread (migrated to can_manager).
  * @param pvParameters: A pointer to the parameters passed to the task.
  * @retval None
  */
static void can1_rx_task(void *pvParameters)
{
  (void)pvParameters;

  for(;;)
  {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

