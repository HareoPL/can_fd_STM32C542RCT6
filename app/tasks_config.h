/**
 * @file tasks_config.h
 * @brief 
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-12
 * @LastEditors Jan Lukaszewicz
 * @LastEditTime 2026-10-12
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



#endif /* TASKS_CONFIG_H */
