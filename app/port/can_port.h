/**
 * @file can_port.h
 * @brief Hardware abstraction layer contract for CAN/FDCAN bus.
 */

#ifndef CAN_PORT_H
#define CAN_PORT_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Supported CAN/FDCAN bit rates.
 */
typedef enum {
    CAN_BAUDRATE_CUSTOM = 0,       /**< Default hardware configuration (e.g. from CubeMX) */
    CAN_BAUDRATE_125K   = 125000,  /**< 125 kbps */
    CAN_BAUDRATE_250K   = 250000,  /**< 250 kbps */
    CAN_BAUDRATE_500K   = 500000,  /**< 500 kbps */
    CAN_BAUDRATE_1000K  = 1000000, /**< 1 Mbps */
    CAN_BAUDRATE_2000K  = 2000000, /**< 2 Mbps (CAN FD data phase) */
    CAN_BAUDRATE_4000K  = 4000000, /**< 4 Mbps (CAN FD data phase) */
    CAN_BAUDRATE_8000K  = 8000000  /**< 8 Mbps (CAN FD data phase) */
} can_baudrate_t;

/**
 * @brief Configuration parameters for CAN/FDCAN bus instance.
 */
typedef struct {
    uint32_t frame_format;          /**< Frame format: CAN_MSG_FRAME_FORMAT_CAN or CAN_MSG_FRAME_FORMAT_FD_CAN */
    can_baudrate_t baudrate_normal; /**< Nominal bit rate for arbitration phase */
    can_baudrate_t baudrate_FD;     /**< Data bit rate for CAN FD fast data phase */
} can_config_t;

/**
 * @brief Notification callback invoked upon receiving frame in ISR.
 * @param[in] context User context passed when attaching callback.
 */
typedef void (*can_rx_notify_fn)(void* context);

/**
 * @brief Generic hardware interface structure for CAN controllers.
 */
typedef struct {
    bool (*init)(const can_config_t *config);
    bool (*transmit)(uint32_t id, const uint8_t* data, uint8_t len);
    bool (*receive)(uint32_t* id, uint8_t* data, uint8_t* len);
    void (*attach_rx_callback)(can_rx_notify_fn cb, void* context);
} can_interface_t;

#endif // CAN_PORT_H
