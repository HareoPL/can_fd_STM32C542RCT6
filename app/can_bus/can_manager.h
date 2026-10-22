/**
 * @file can_manager.h
 * @brief Universal CAN/FDCAN bus manager supporting multiple instances and FreeRTOS.
 */

#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "can_port.h"
#include "can_msg_types.h"

#define CAN_QUEUE_DEFAULT_LENGTH 16
#define CAN_MAX_FILTERS          16U

/**
 * @brief Structure representing a CAN bus instance (object-oriented model).
 */
typedef struct {
    QueueHandle_t tx_queue;              /**< FreeRTOS queue for outgoing frames */
    SemaphoreHandle_t rx_sem;            /**< Binary semaphore notifying RX task of new frames */
    TaskHandle_t tx_task_handle;         /**< Task handle for TX worker */
    TaskHandle_t rx_task_handle;         /**< Task handle for RX worker */
    const can_interface_t* hw_interface; /**< Hardware port interface adapter */
    char name[16];                       /**< Instance name identifier (e.g. "CAN1") */
    can_config_t can_config;             /**< Bus configuration (defaults applied if uninitialized) */
    can_filter_t filters[CAN_MAX_FILTERS]; /**< Registered filters with dispatch callbacks */
    uint32_t num_filters;                /**< Number of active filters */
} can_instance_t;

/**
 * @brief Initializes CAN manager instance and spawns FreeRTOS tasks.
 * @param[in,out] inst Pointer to CAN instance structure.
 * @param[in] hw_interface Pointer to hardware interface adapter.
 * @param[in] task_name_prefix Name prefix for FreeRTOS tasks (e.g. "CAN1").
 * @return true on success, false on error.
 */
bool can_manager_init(can_instance_t* inst, 
                     const can_interface_t* hw_interface, 
                     const char* task_name_prefix);

/**
 * @brief Enqueues a message for transmission on the given CAN instance.
 * @param[in,out] inst Pointer to CAN instance.
 * @param[in] msg Pointer to CAN message to transmit.
 * @return true if queued successfully, false if queue is full.
 */
bool can_manager_send(can_instance_t* inst, const CAN_msg_t* msg);

/**
 * @brief Configures acceptance filters and dispatch callbacks for the CAN instance.
 * @param[in,out] inst Pointer to CAN instance.
 * @param[in] filters Array of filter configurations.
 * @param[in] num_filters Number of elements (0 to accept all frames without filtering).
 * @return true on success, false on error.
 */
bool can_manager_setFilter(can_instance_t* inst, const can_filter_t *filters, uint32_t num_filters);

/**
 * @brief Reconfigures bus mode and bit rates of an initialized CAN instance.
 *        On failure the previous configuration is restored.
 * @param[in,out] inst Pointer to CAN instance.
 * @param[in] config Desired bus configuration.
 * @return true on success (or configuration unchanged), false on error.
 */
bool can_manager_setBusConfig(can_instance_t* inst, const can_config_t *config);

/**
 * @brief Returns the CAN instance currently executing an RX filter callback.
 * @return Pointer to the active can_instance_t during callback dispatch, or NULL.
 */
can_instance_t *can_manager_get_active_rx_instance(void);

#endif /* CAN_MANAGER_H */
