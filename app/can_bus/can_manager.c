/**
 * @file can_manager.c
 * @brief Universal CAN/FDCAN bus manager implementation supporting multiple instances.
 */

#include "can_manager.h"
#include "tasks_config.h"
#include "safe_printf.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief Notification callback invoked from interrupt (ISR) upon hardware reception.
 * @param[in] context Pointer to associated can_instance_t structure.
 */
static void can_manager_rx_isr_callback(void* context)
{
    can_instance_t *inst = (can_instance_t *)context;
    if (inst != NULL && inst->rx_sem != NULL)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(inst->rx_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/**
 * @brief Dedicated transmission worker task for a CAN instance.
 * @param[in] pvParameters Pointer to the can_instance_t instance structure.
 */
static void can_tx_task(void *pvParameters)
{
    can_instance_t *inst = (can_instance_t *)pvParameters;
    if (inst == NULL || inst->hw_interface == NULL || inst->tx_queue == NULL)
    {
        vTaskDelete(NULL);
        return;
    }

    CAN_msg_t can_msg;

    for (;;)
    {
        /* Wait for frame to be queued */
        if (xQueueReceive(inst->tx_queue, &can_msg, portMAX_DELAY) == pdTRUE)
        {
            uint8_t payload_len = can_msg.len;
            if (payload_len > sizeof(can_msg.data))
            {
                payload_len = (uint8_t)sizeof(can_msg.data);
            }

            safe_printf(inst->name, "queue msg ID=0x%03lX len=%u\r\n",
                        (unsigned long)CAN_msg_get_can_id(can_msg.ident),
                        (unsigned int)payload_len);

            if (inst->hw_interface->transmit != NULL &&
                inst->hw_interface->transmit(can_msg.ident, can_msg.data, payload_len))
            {
                safe_printf(inst->name, "sent ID=0x%03lX OK\r\n", CAN_msg_get_can_id(can_msg.ident));
            }
            else
            {
                safe_printf(inst->name, "ERROR sending ID=0x%03lX\r\n", CAN_msg_get_can_id(can_msg.ident));
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

/**
 * @brief Matches an incoming CAN message against registered filters and executes its callback.
 * @param[in] inst Pointer to the CAN instance.
 * @param[in] msg Pointer to the received CAN message.
 */
static void can_manager_dispatch_rx(can_instance_t *inst, const CAN_msg_t *msg)
{
    if (inst == NULL || msg == NULL || inst->num_filters == 0U)
    {
        return;
    }

    uint32_t msg_id = msg->ident;
    bool is_ext = (msg->ident > CAN_SFF_MASK);

    for (uint32_t i = 0U; i < inst->num_filters; i++)
    {
        const can_filter_t *f = &inst->filters[i];
        bool filter_is_ext = (f->arb > CAN_SFF_MASK);

        /* Standard filter matches standard frame; Extended filter matches extended frame */
        if (is_ext != filter_is_ext)
        {
            continue;
        }

        uint32_t filter_id = f->arb;

        /* Check bitwise mask match */
        if ((msg_id & f->mask) == (filter_id & f->mask))
        {
            if (f->cb != NULL)
            {
                f->cb(msg);
            }
            break;
        }
    }
}

/**
 * @brief Dedicated event-driven reception worker task for a CAN instance.
 * @param[in] pvParameters Pointer to the can_instance_t instance structure.
 */
static void can_rx_task(void *pvParameters)
{
    can_instance_t *inst = (can_instance_t *)pvParameters;
    if (inst == NULL || inst->hw_interface == NULL || inst->rx_sem == NULL)
    {
        vTaskDelete(NULL);
        return;
    }

    /* Wait indefinitely if interrupt notifications are supported; otherwise poll */
    const TickType_t wait_ticks = (inst->hw_interface->attach_rx_callback != NULL) ? portMAX_DELAY : pdMS_TO_TICKS(10);

    for (;;)
    {
        /* Wait for semaphore signaled by ISR */
        if (xSemaphoreTake(inst->rx_sem, wait_ticks) == pdTRUE || inst->hw_interface->attach_rx_callback == NULL)
        {
            if (inst->hw_interface->receive != NULL)
            {
                uint32_t rx_id = 0;
                uint8_t rx_data[64] = {0};
                uint8_t rx_len = 0;

                /* Flush all pending frames from the hardware FIFO */
                while (inst->hw_interface->receive(&rx_id, rx_data, &rx_len))
                {
                    safe_printf(inst->name, "rx msg ID=0x%03lX len=%u\r\n",
                                (unsigned long)rx_id,
                                (unsigned int)rx_len);

                    CAN_msg_t rx_msg = {0};
                    rx_msg.ident = rx_id;
                    rx_msg.len = rx_len;
                    rx_msg.format = inst->can_config.frame_format;
                    uint8_t copy_len = (rx_len > sizeof(rx_msg.data)) ? (uint8_t)sizeof(rx_msg.data) : rx_len;
                    memcpy(rx_msg.data, rx_data, copy_len);

                    /* Dispatch received frame to matching filter callback */
                    can_manager_dispatch_rx(inst, &rx_msg);
                }
            }
        }
    }
}

/**
 * @brief Initializes a CAN manager instance and creates FreeRTOS tasks and synchronization objects.
 * @param[in,out] inst Pointer to CAN instance (can_config can be pre-filled or zero for defaults).
 * @param[in] hw_interface Pointer to hardware port interface adapter.
 * @param[in] task_name_prefix Name prefix for tasks and logs (e.g. "CAN1").
 * @return true on success, false on error.
 */
bool can_manager_init(can_instance_t* inst, 
                     const can_interface_t* hw_interface, 
                     const char* task_name_prefix)
{
    if (inst == NULL || hw_interface == NULL)
    {
        return false;
    }

    inst->hw_interface = hw_interface;

    /* If user did not specify can_config beforehand (uninitialized/zero), apply default: 500k Classic CAN */
    if (inst->can_config.baudrate_normal == 0)
    {
        inst->can_config.baudrate_normal = CAN_BAUDRATE_500K;
        inst->can_config.frame_format = CAN_MSG_FRAME_FORMAT_CAN;
        inst->can_config.baudrate_FD = CAN_BAUDRATE_CUSTOM;
    }

    if (task_name_prefix != NULL)
    {
        strncpy(inst->name, task_name_prefix, sizeof(inst->name) - 1);
        inst->name[sizeof(inst->name) - 1] = '\0';
    }
    else
    {
        strncpy(inst->name, "CAN", sizeof(inst->name) - 1);
        inst->name[sizeof(inst->name) - 1] = '\0';
    }

    /* Create binary semaphore for RX interrupt notification */
    inst->rx_sem = xSemaphoreCreateBinary();
    if (inst->rx_sem == NULL)
    {
        return false;
    }

    /* Register ISR notification callback in hardware port */
    if (inst->hw_interface->attach_rx_callback != NULL)
    {
        inst->hw_interface->attach_rx_callback(can_manager_rx_isr_callback, inst);
    }

    /* Initialize hardware controller via port adapter using configuration */
    if (inst->hw_interface->init != NULL)
    {
        if (!inst->hw_interface->init(&inst->can_config))
        {
            vSemaphoreDelete(inst->rx_sem);
            inst->rx_sem = NULL;
            return false;
        }
    }

    /* Create TX queue */
    inst->tx_queue = xQueueCreate(CAN_QUEUE_DEFAULT_LENGTH, sizeof(CAN_msg_t));
    if (inst->tx_queue == NULL)
    {
        vSemaphoreDelete(inst->rx_sem);
        inst->rx_sem = NULL;
        return false;
    }

    char tx_task_name[configMAX_TASK_NAME_LEN];
    char rx_task_name[configMAX_TASK_NAME_LEN];
    snprintf(tx_task_name, sizeof(tx_task_name), "%.12s_tx", inst->name);
    snprintf(rx_task_name, sizeof(rx_task_name), "%.12s_rx", inst->name);

    /* Create FreeRTOS tasks with stack and priority configuration from tasks_config.h */
    BaseType_t ret;
    ret = xTaskCreate(can_tx_task, tx_task_name, TASK_STACK_CAN_TX,
                      (void*)inst, TASK_PRIO_CAN_TX, &inst->tx_task_handle);
    if (ret != pdPASS)
    {
        vQueueDelete(inst->tx_queue);
        inst->tx_queue = NULL;
        vSemaphoreDelete(inst->rx_sem);
        inst->rx_sem = NULL;
        return false;
    }

    ret = xTaskCreate(can_rx_task, rx_task_name, TASK_STACK_CAN_RX,
                      (void*)inst, TASK_PRIO_CAN_RX, &inst->rx_task_handle);
    if (ret != pdPASS)
    {
        vTaskDelete(inst->tx_task_handle);
        inst->tx_task_handle = NULL;
        vQueueDelete(inst->tx_queue);
        inst->tx_queue = NULL;
        vSemaphoreDelete(inst->rx_sem);
        inst->rx_sem = NULL;
        return false;
    }

    return true;
}

/**
 * @brief Enqueues a message for transmission on the given CAN instance.
 * @param[in,out] inst Pointer to CAN instance.
 * @param[in] msg Pointer to CAN message to transmit.
 * @return true if queued successfully, false if queue is full.
 */
bool can_manager_send(can_instance_t* inst, const CAN_msg_t* msg)
{
    if (inst == NULL || inst->tx_queue == NULL || msg == NULL)
    {
        return false;
    }

    return (xQueueSend(inst->tx_queue, msg, 0) == pdPASS);
}

/**
 * @brief Configures acceptance filters and dispatch callbacks for the CAN instance.
 * @param[in,out] inst Pointer to CAN instance.
 * @param[in] filters Array of filter configurations.
 * @param[in] num_filters Number of elements (0 to accept all frames without filtering).
 * @return true on success, false on error.
 */
bool can_manager_setFilter(can_instance_t* inst, const can_filter_t *filters, uint32_t num_filters)
{
    if (inst == NULL || inst->hw_interface == NULL)
    {
        return false;
    }

    if (num_filters > CAN_MAX_FILTERS)
    {
        return false;
    }

    /* 1. Configure hardware filters via interface adapter */
    if (inst->hw_interface->set_filter != NULL)
    {
        if (!inst->hw_interface->set_filter(filters, num_filters))
        {
            return false;
        }
    }

    /* 2. Cache filter table and callbacks for RX task dispatching */
    if (filters != NULL && num_filters > 0U)
    {
        memcpy(inst->filters, filters, num_filters * sizeof(can_filter_t));
        inst->num_filters = num_filters;
    }
    else
    {
        memset(inst->filters, 0, sizeof(inst->filters));
        inst->num_filters = 0U;
    }

    return true;
}