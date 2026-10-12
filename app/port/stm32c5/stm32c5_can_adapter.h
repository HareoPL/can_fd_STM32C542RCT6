/**
 * @file stm32c5_can_adapter.h
 * @brief STM32C5 hardware adapter for CAN/FDCAN bus.
 */
#ifndef STM32C5_CAN_ADAPTER_H
#define STM32C5_CAN_ADAPTER_H

#include "can_port.h"

/**
 * @brief Retrieves the pointer to the hardware interface for CAN1 on STM32C5.
 * @return Pointer to constant can_interface_t structure.
 */
const can_interface_t *stm32c5_can1_interfaceGet(void);

#endif /* STM32C5_CAN_ADAPTER_H */

