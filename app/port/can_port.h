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

#include "can_msg_types.h"

/* --- CAN Filter Masks --- */
#define CAN_MASK_EXACT_STANDARD  0x000007FFU /**< Exact match for 11-bit Standard ID */
#define CAN_MASK_EXACT_EXTENDED  0x1FFFFFFFU /**< Exact match for 29-bit Extended ID */
#define CAN_MASK_ALL             0x00000000U /**< Accept all IDs (don't care) */

/**
 * @brief Callback function type for received CAN messages.
 * @param[in] msg Pointer to the received CAN message structure.
 */
typedef void (*can_rx_callback_fn)(const CAN_msg_t *msg);

/**
 * @brief CAN acceptance filter and route configuration structure.
 */
typedef struct
{
    uint32_t arb;           /**< CAN ID (11-bit Standard, 29-bit Extended, or with CAN_EFF_FLAG) */
    uint32_t mask;          /**< Filter mask (1 = bit must match arb, 0 = don't care) */
    can_rx_callback_fn cb;  /**< Message callback invoked when ID matches (optional, NULL if none) */
} can_filter_t;

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
    bool (*set_filter)(const can_filter_t *filters, uint32_t num_filters); /**< Hardware filter setup */
} can_interface_t;

#endif // CAN_PORT_H
