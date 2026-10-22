/**
 * @file can_manager.c
 * @brief Universal CAN/FDCAN bus manager implementation supporting multiple instances.
 */

#include "can_manager.h"
#include "tasks_config.h"
#include "safe_printf.h"
#include <stdio.h>
#include <string.h>

/** @brief Per-frame TX logging (disable for high frame rates, e.g. CAN FD test build). */
#ifndef CAN_MANAGER_TX_LOG
#define CAN_MANAGER_TX_LOG 1
#endif

/** @brief Pointer to the CAN instance currently dispatching an RX callback. */
static can_instance_t *s_active_rx_inst = NULL;

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

/** @brief Period of the bus-off supervision while the TX queue is idle. */
#define CAN_MANAGER_HEALTH_POLL_MS 100U

/**
 * @brief Restarts the controller after bus-off (M_CAN stays in INIT after bus-off until restarted).
 *        After the restart the controller rejoins the bus after 128 x 11 recessive bits (ISO 11898-1).
 * @param[in,out] inst Pointer to CAN instance.
 */
static void can_manager_recover_bus_off(can_instance_t *inst)
{
    can_bus_status_t status = {0};
    if (!can_manager_getStatus(inst, &status) || !status.bus_off)
    {
        return;
    }

    inst->bus_off_cnt++;
    safe_printf(inst->name, "BUS-OFF detected (TEC=%u), restarting controller (#%lu)\r\n",
                (unsigned int)status.tx_error_cnt, (unsigned long)inst->bus_off_cnt);

    if (inst->hw_interface->init == NULL || !inst->hw_interface->init(&inst->can_config))
    {
        safe_printf(inst->name, "ERROR: bus-off recovery failed\r\n");
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
        /* Wait for frame to be queued; wake up periodically for bus-off supervision */
        if (xQueueReceive(inst->tx_queue, &can_msg, pdMS_TO_TICKS(CAN_MANAGER_HEALTH_POLL_MS)) != pdTRUE)
        {
            can_manager_recover_bus_off(inst);
            continue;
        }

        {
            if (can_msg.len > sizeof(can_msg.data))
            {
                can_msg.len = (uint8_t)sizeof(can_msg.data);
            }

#if CAN_MANAGER_TX_LOG
            safe_printf(inst->name, "queue msg ID=0x%03lX len=%u %s\r\n",
                        (unsigned long)CAN_msg_get_can_id(can_msg.ident),
                        (unsigned int)can_msg.len,
                        (can_msg.format == CAN_MSG_FRAME_FORMAT_FD_CAN) ? "FD" : "CAN");
#endif

            if (inst->hw_interface->transmit != NULL &&
                inst->hw_interface->transmit(&can_msg))
            {
#if CAN_MANAGER_TX_LOG
                safe_printf(inst->name, "sent ID=0x%03lX OK\r\n", CAN_msg_get_can_id(can_msg.ident));
#endif
            }
            else
            {
                inst->tx_error_cnt++;
                safe_printf(inst->name, "ERROR sending ID=0x%03lX\r\n", CAN_msg_get_can_id(can_msg.ident));
                can_manager_recover_bus_off(inst);
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
                s_active_rx_inst = inst;
                f->cb(msg);
                s_active_rx_inst = NULL;
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
                CAN_msg_t rx_msg = {0};

                /* Flush all pending frames from the hardware FIFO */
                while (inst->hw_interface->receive(&rx_msg))
                {
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
        inst->can_config.mode = CAN_MODE_CLASSIC;
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
 * @brief Returns printable name of a bus operating mode.
 * @param[in] mode Bus operating mode.
 * @return Constant string.
 */
static const char *can_manager_mode_name(can_frame_mode_t mode)
{
    switch (mode)
    {
        case CAN_MODE_CLASSIC:   return "Classic";
        case CAN_MODE_FD_NO_BRS: return "FD";
        case CAN_MODE_FD_BRS:    return "FD+BRS";
        default:                 return "?";
    }
}

/**
 * @brief Reconfigures bus mode and bit rates of an initialized CAN instance.
 *        The controller is stopped, reconfigured and restarted; on failure the previous
 *        configuration is restored. Must not race with frames in flight (call during
 *        startup or while the bus is idle).
 * @param[in,out] inst Pointer to CAN instance.
 * @param[in] config Desired bus configuration.
 * @return true on success (or configuration unchanged), false on error.
 */
bool can_manager_setBusConfig(can_instance_t* inst, const can_config_t *config)
{
    if (inst == NULL || inst->hw_interface == NULL || config == NULL)
    {
        return false;
    }

    if (inst->can_config.mode == config->mode &&
        inst->can_config.baudrate_normal == config->baudrate_normal &&
        inst->can_config.baudrate_FD == config->baudrate_FD)
    {
        return true;
    }

    if (inst->hw_interface->init != NULL && !inst->hw_interface->init(config))
    {
        safe_printf(inst->name, "ERROR: bus config %s %lu/%lu bps not supported, restoring previous\r\n",
                    can_manager_mode_name(config->mode),
                    (unsigned long)config->baudrate_normal,
                    (unsigned long)config->baudrate_FD);
        (void)inst->hw_interface->init(&inst->can_config);
        return false;
    }

    inst->can_config = *config;
    safe_printf(inst->name, "bus config: %s nominal=%lu bps data=%lu bps\r\n",
                can_manager_mode_name(config->mode),
                (unsigned long)config->baudrate_normal,
                (unsigned long)((config->mode == CAN_MODE_FD_BRS) ? config->baudrate_FD : config->baudrate_normal));
    return true;
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

/**
 * @brief Reads bus health (error counters, last protocol errors, TDC value) of a CAN instance.
 * @param[in] inst Pointer to CAN instance.
 * @param[out] status Bus health snapshot.
 * @return true on success, false if not supported by the port or on error.
 */
bool can_manager_getStatus(const can_instance_t* inst, can_bus_status_t *status)
{
    if (inst == NULL || inst->hw_interface == NULL || inst->hw_interface->get_status == NULL || status == NULL)
    {
        return false;
    }

    return inst->hw_interface->get_status(status);
}

/**
 * @brief Waits until all queued frames have been handed to the hardware.
 * @param[in] inst Pointer to CAN instance.
 * @param[in] timeout_ms Maximum waiting time in milliseconds.
 * @return true if the TX queue is empty, false on timeout or invalid instance.
 */
bool can_manager_waitTxIdle(const can_instance_t* inst, uint32_t timeout_ms)
{
    if (inst == NULL || inst->tx_queue == NULL)
    {
        return false;
    }

    TickType_t start = xTaskGetTickCount();
    while (uxQueueMessagesWaiting(inst->tx_queue) > 0U)
    {
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms))
        {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return true;
}

can_instance_t *can_manager_get_active_rx_instance(void)
{
    return s_active_rx_inst;
}
