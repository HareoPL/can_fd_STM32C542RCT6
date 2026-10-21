/**
 * @file isotp.h
 * @brief ISO 15765-2 (ISO-TP) transport layer implementation in pure portable C.
 * @details Supports single frame and multi-frame reception (SF, FF, CF) with Flow Control (FC)
 *          as well as single frame and multi-frame transmission.
 */

#ifndef ISOTP_H
#define ISOTP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Maximum incoming PDU size accepted by the reassembly buffer (bytes).
 * @details Must hold the largest OTA write PDU: cmd(1) + addr(4) + len(2) + OTA_MAX_BLOCK_SIZE + crc(4).
 */
#define ISOTP_RX_BUF_SIZE               1040U

/** @brief Maximum outgoing PDU size (bytes). Responses to the PC tool are short ACK/NACK frames. */
#define ISOTP_TX_BUF_SIZE               64U

/** @brief Default Flow Control Block Size sent to the transmitter (0 = no limit). */
#define ISOTP_DEFAULT_FC_BLOCK_SIZE     0U

/** @brief Default Flow Control STmin sent to the transmitter (0 = back-to-back frames). */
#define ISOTP_DEFAULT_FC_ST_MIN         0U

/** @brief ISO-TP frame types (PCI upper nibble). */
#define ISOTP_PCI_TYPE_SINGLE           0x00U
#define ISOTP_PCI_TYPE_FIRST            0x10U
#define ISOTP_PCI_TYPE_CONSECUTIVE      0x20U
#define ISOTP_PCI_TYPE_FLOW_CONTROL     0x30U

/** @brief Flow Control status codes. */
#define ISOTP_FC_FS_CTS                 0x00U /**< Clear to send */
#define ISOTP_FC_FS_WAIT                0x01U /**< Wait */
#define ISOTP_FC_FS_OVERFLOW            0x02U /**< Buffer overflow */

/**
 * @brief Function pointer type for transmitting a raw 8-byte CAN frame.
 * @param[in] can_id CAN identifier (11-bit or 29-bit).
 * @param[in] data Pointer to frame payload.
 * @param[in] len Frame length in bytes (1 to 8).
 * @param[in] user_ctx User-defined context pointer.
 * @return true if transmitted/queued successfully, false otherwise.
 */
typedef bool (*isotp_can_tx_fn)(uint32_t can_id, const uint8_t *data, uint8_t len, void *user_ctx);

/**
 * @brief Callback invoked when a complete ISO-TP PDU has been received.
 * @param[in] payload Pointer to the complete PDU payload buffer.
 * @param[in] length Number of bytes in the received PDU.
 * @param[in] user_ctx User-defined context pointer.
 */
typedef void (*isotp_rx_pdu_fn)(const uint8_t *payload, uint16_t length, void *user_ctx);

/**
 * @brief ISO-TP link reception state.
 */
typedef enum
{
    ISOTP_RX_STATE_IDLE = 0,
    ISOTP_RX_STATE_RECEIVING_CF
} isotp_rx_state_t;

/**
 * @brief ISO-TP link transmission state.
 */
typedef enum
{
    ISOTP_TX_STATE_IDLE = 0,
    ISOTP_TX_STATE_WAIT_FC,
    ISOTP_TX_STATE_SENDING_CF
} isotp_tx_state_t;

/**
 * @brief ISO-TP connection instance descriptor.
 */
typedef struct
{
    uint32_t rx_can_id;             /**< Filter CAN ID for incoming frames (e.g. 0x7E0) */
    uint32_t tx_can_id;             /**< Destination CAN ID for outgoing frames (e.g. 0x7E8) */
    isotp_can_tx_fn tx_fn;          /**< Hardware frame transmission function */
    isotp_rx_pdu_fn rx_cb;          /**< PDU reception completion callback */
    void *user_ctx;                 /**< Context pointer passed to callbacks */

    /* RX Engine */
    isotp_rx_state_t rx_state;      /**< Current reception state */
    uint16_t rx_total_len;          /**< Expected total length of incoming PDU */
    uint16_t rx_received_len;       /**< Number of bytes received so far */
    uint8_t rx_sn;                  /**< Expected next sequence number (0..15) */
    uint8_t rx_fc_block_size;       /**< Block Size advertised in our Flow Control frames (0 = no limit) */
    uint8_t rx_fc_st_min;           /**< STmin advertised in our Flow Control frames (ISO 15765-2 encoding) */
    uint8_t rx_cf_in_block;         /**< Consecutive frames received since the last Flow Control */
    uint8_t rx_buf[ISOTP_RX_BUF_SIZE]; /**< Reception reassembly buffer */

    /* TX Engine */
    isotp_tx_state_t tx_state;      /**< Current transmission state */
    uint16_t tx_total_len;          /**< Total length of PDU being sent */
    uint16_t tx_sent_len;           /**< Bytes transmitted so far */
    uint8_t tx_sn;                  /**< Next sequence number to send (1..15) */
    uint8_t tx_block_size;          /**< Flow Control Block Size remaining */
    uint8_t tx_st_min_ms;           /**< Separation Time in ms from receiver */
    uint8_t tx_buf[ISOTP_TX_BUF_SIZE]; /**< Transmission buffer */
} isotp_link_t;

/**
 * @brief Initializes an ISO-TP link instance.
 * @param[in,out] link Pointer to link structure.
 * @param[in] rx_can_id Expected CAN ID for incoming frames.
 * @param[in] tx_can_id Destination CAN ID for responses and outgoing frames.
 * @param[in] tx_fn Hardware CAN frame transmission function pointer.
 * @param[in] rx_cb Callback invoked upon receiving a full PDU.
 * @param[in] user_ctx User context pointer passed to callbacks.
 */
void isotp_init(isotp_link_t *link,
                uint32_t rx_can_id,
                uint32_t tx_can_id,
                isotp_can_tx_fn tx_fn,
                isotp_rx_pdu_fn rx_cb,
                void *user_ctx);

/**
 * @brief Configures the Flow Control parameters advertised to the remote transmitter.
 * @details Block Size limits how many consecutive frames the sender may transmit before
 *          waiting for the next Flow Control frame (0 = unlimited). STmin uses the
 *          ISO 15765-2 encoding: 0x00..0x7F = 0..127 ms, 0xF1..0xF9 = 100..900 us.
 * @param[in,out] link Pointer to link structure.
 * @param[in] block_size Block Size (BS) value (0 = no limit).
 * @param[in] st_min Minimum separation time (STmin) between consecutive frames.
 */
void isotp_set_flow_control(isotp_link_t *link, uint8_t block_size, uint8_t st_min);

/**
 * @brief Feeds an incoming CAN frame into the ISO-TP reception engine.
 * @param[in,out] link Pointer to link structure.
 * @param[in] can_id CAN identifier of received frame.
 * @param[in] data Pointer to received data buffer.
 * @param[in] len Length of received frame (1..8).
 */
void isotp_on_can_frame(isotp_link_t *link, uint32_t can_id, const uint8_t *data, uint8_t len);

/**
 * @brief Sends an ISO-TP PDU over the CAN bus (blocking or non-blocking multi-frame).
 * @param[in,out] link Pointer to link structure.
 * @param[in] payload Pointer to payload buffer.
 * @param[in] length Number of bytes in payload (1 to ISOTP_TX_BUF_SIZE).
 * @return true on success, false if busy or invalid parameters.
 */
bool isotp_send_pdu(isotp_link_t *link, const uint8_t *payload, uint16_t length);

/**
 * @brief Resets the ISO-TP link reception state machine.
 * @param[in,out] link Pointer to link structure.
 */
void isotp_reset_rx(isotp_link_t *link);

#ifdef __cplusplus
}
#endif

#endif /* ISOTP_H */

