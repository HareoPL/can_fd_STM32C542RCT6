/**
  ******************************************************************************
  * @file           : mx_freertos_app.c
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

/* Private define ------------------------------------------------------------*/
#define TAG "MAIN"

#define main_task_stack_size  512U        // * 4 = 2048 bajtów
#define heartbeat_task_stack_size  128U   // * 4 = 512 bajtów
#define can1_tx_task_stack_size  128U
#define can1_rx_task_stack_size  128U
/* Private typedef -----------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/*-------------------- Tasks definition --------------------------------------*/
/* Definitions for main_task */
static TaskHandle_t main_task_Handle;
/* Definitions for heartbeat_task */
static TaskHandle_t heartbeat_task_Handle;
/* Definitions for can1_tx_task */
static TaskHandle_t can1_tx_task_Handle;
/* Definitions for can1_rx_task */
static TaskHandle_t can1_rx_task_Handle;

/* Global mutex for thread-safe printf */
SemaphoreHandle_t printf_mutex = NULL;

/* Private functions prototype -----------------------------------------------*/
/* Tasks entry function ------------------------------------------------------*/
static void main_task(void *pvParameters);
static void heartbeat_task(void *pvParameters);
static void can1_tx_task(void *pvParameters);
static void can1_rx_task(void *pvParameters);

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

  /* main_task creation-------------------------------------*/
  ret = xTaskCreate(main_task, "main_task", main_task_stack_size,
                    (void*) NULL, 2, &main_task_Handle);
  if (ret != pdPASS){return -1;}

  /* heartbeat_task creation-------------------------------------*/
  ret = xTaskCreate(heartbeat_task, "heartbeat_task", heartbeat_task_stack_size,
                    (void*) NULL, 2, &heartbeat_task_Handle);
 if (ret != pdPASS){return -1;}

  /* can1_tx_task creation-------------------------------------*/
  ret = xTaskCreate(can1_tx_task, "can1_tx_task", can1_tx_task_stack_size,
                    (void*) NULL, 2, &can1_tx_task_Handle);
  if (ret != pdPASS){return -1;}

  /* can1_rx_task creation-------------------------------------*/
  ret = xTaskCreate(can1_rx_task, "can1_rx_task", can1_rx_task_stack_size,
                    (void*) NULL, 2, &can1_rx_task_Handle);
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
static void main_task(void *pvParameters)
{
  (void)pvParameters;

  for(;;)
  {
    safe_printf(TAG, "main task \r\n");
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
  * @brief Function implementing the can1_tx_task thread.
  * @param pvParameters: A pointer to the parameters passed to the task.
  * @retval None
  */
static void can1_tx_task(void *pvParameters)
{
  (void)pvParameters;

  for(;;)
  {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/**
  * @brief Function implementing the can1_rx_task thread.
  * @param pvParameters: A pointer to the parameters passed to the task.
  * @retval None
  */
static void can1_rx_task(void *pvParameters)
{
  (void)pvParameters;

  for(;;)
  {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

