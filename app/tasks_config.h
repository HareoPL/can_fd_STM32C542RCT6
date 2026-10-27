/**
 * @file tasks_config.h
 * @brief 
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-12
 * @LastEditors Jan Lukaszewicz
 * @LastEditTime 2026-10-14
 */
#ifndef TASKS_CONFIG_H
#define TASKS_CONFIG_H

// CAN-BUS TASKS
#define TASK_PRIO_CAN_RX    (configMAX_PRIORITIES - 1) // Najwyższy
#define TASK_PRIO_CAN_TX    (configMAX_PRIORITIES - 2)

#define TASK_STACK_CAN_RX       512     // * 4 = 2048 bytes
#define TASK_STACK_CAN_TX       256

// MAIN TASK
#define TASK_PRIO_MAIN          (tskIDLE_PRIORITY + 2)     
#define TASK_STACK_MAIN         512U   // * 4 = 2048 bytes

// HEARTBEAT TASK
#define TASK_PRIO_HEARTBEAT     (tskIDLE_PRIORITY + 1)
#define TASK_STACK_HEARTBEAT    128U   // * 4 = 512 bytes

// IO MANAGER TASK
#define TASK_PRIO_IO_MANAGER    (tskIDLE_PRIORITY + 2U)
#define TASK_STACK_IO_MANAGER   512U

// RELAY ASYNC WORKER TASK
#define TASK_PRIO_RELAY_WORKER  (tskIDLE_PRIORITY + 2U)
#define TASK_STACK_RELAY_WORKER 256U   // * 4 = 1024 bytes

// OTA UPDATE WORKER TASK
#define TASK_PRIO_OTA_WORKER    (tskIDLE_PRIORITY + 2U)
#define TASK_STACK_OTA_WORKER   512U   // * 4 = 2048 bytes

// CAN FD TEST TASK (only with CAN_FD_TEST build option)
#define TASK_PRIO_CAN_FD_TEST   (tskIDLE_PRIORITY + 3U)
#define TASK_STACK_CAN_FD_TEST  512U   // * 4 = 2048 bytes

// UART -> CAN BRIDGE TASK (only with CAN_UART_BRIDGE build option)
#define TASK_PRIO_CAN_UART      (tskIDLE_PRIORITY + 2U)
#define TASK_STACK_CAN_UART     512U   // * 4 = 2048 bytes
// RS485 LINK TASK (only with RS485_LINK build option)
#define TASK_PRIO_RS485         (tskIDLE_PRIORITY + 2U)
#define TASK_STACK_RS485        512U   // * 4 = 2048 bytes

// CAR SIMULATOR TASK (cyclic CAN1 frames of the active car simulator, every 10 ms)
#define TASK_PRIO_CAR_SIM       (tskIDLE_PRIORITY + 2U)
#define TASK_STACK_CAR_SIM      512U   // * 4 = 2048 bytes

#endif /* TASKS_CONFIG_H */
