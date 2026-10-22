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
 * @brief Operating mode of the CAN/FDCAN bus.
 */
typedef enum {
    CAN_MODE_CLASSIC = 0, /**< Classic CAN 2.0 only (max 8 bytes payload) */
    CAN_MODE_FD_NO_BRS,   /**< CAN FD frames allowed, data phase at nominal bit rate */
    CAN_MODE_FD_BRS       /**< CAN FD frames allowed, data phase switched to baudrate_FD */
} can_frame_mode_t;

/**
 * @brief Configuration parameters for CAN/FDCAN bus instance.
 */
typedef struct {
    can_frame_mode_t mode;          /**< Bus operating mode (Classic / FD / FD with BRS) */
    can_baudrate_t baudrate_normal; /**< Nominal bit rate for arbitration phase */
    can_baudrate_t baudrate_FD;     /**< Data bit rate for CAN FD fast data phase (used only in CAN_MODE_FD_BRS) */
} can_config_t;

/**
 * @brief Last protocol error code reported by the controller (ISO 11898-1 error types).
 */
typedef enum {
    CAN_ERROR_NONE      = 0, /**< No error */
    CAN_ERROR_STUFF     = 1, /**< Stuff error */
    CAN_ERROR_FORM      = 2, /**< Form error */
    CAN_ERROR_ACK       = 3, /**< Acknowledge error */
    CAN_ERROR_BIT1      = 4, /**< Bit 1 (recessive) error */
    CAN_ERROR_BIT0      = 5, /**< Bit 0 (dominant) error */
    CAN_ERROR_CRC       = 6, /**< CRC error */
    CAN_ERROR_NO_CHANGE = 7  /**< No new error since last read */
} can_error_code_t;

/** @brief Number of counted protocol error types (CAN_ERROR_STUFF .. CAN_ERROR_CRC, index = code - 1). */
#define CAN_ERROR_TYPES 6U

/**
 * @brief Bus health snapshot of a CAN/FDCAN controller.
 */
typedef struct {
    uint8_t tx_error_cnt;             /**< Transmit error counter (TEC) */
    uint8_t rx_error_cnt;             /**< Receive error counter (REC) */
    bool error_passive;               /**< Controller is error passive */
    bool bus_off;                     /**< Controller is bus-off */
    can_error_code_t last_error;      /**< Last error in arbitration / Classic frames (cleared on read) */
    can_error_code_t data_last_error; /**< Last error in CAN FD data phase with BRS (cleared on read) */
    uint8_t tdc_value;                /**< Measured transmitter delay in clock periods (CAN FD with TDC) */
    uint32_t error_log_cnt;           /**< Protocol errors (TEC/REC increments) accumulated since boot, never decreases */
    uint16_t arb_errors[CAN_ERROR_TYPES];  /**< Errors per type in arbitration / Classic phase since boot (index = code - 1) */
    uint16_t data_errors[CAN_ERROR_TYPES]; /**< Errors per type in CAN FD data phase (BRS) since boot (index = code - 1) */
} can_bus_status_t;

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
    bool (*init)(const can_config_t *config);              /**< (Re)configures and starts the controller */
    bool (*transmit)(const CAN_msg_t *msg);                /**< Sends frame using msg->format (FD rejected on Classic bus) */
    bool (*receive)(CAN_msg_t *msg);                       /**< Reads one frame, msg->format reflects the received frame */
    void (*attach_rx_callback)(can_rx_notify_fn cb, void* context);
    bool (*set_filter)(const can_filter_t *filters, uint32_t num_filters); /**< Hardware filter setup */
    bool (*get_status)(can_bus_status_t *status);          /**< Bus health snapshot (optional, may be NULL) */
} can_interface_t;

#endif // CAN_PORT_H
