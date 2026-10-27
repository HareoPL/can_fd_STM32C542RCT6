/**
 * @file rs485_frame.h
 * @brief Binary RS485 frame codec of the external telematics devices (docs/rs485/rs485_protocol.md).
 *
 * Frame: AA 55 | CRC16 (LE) | LEN | payload[LEN], payload[0] = destination, payload[1] = source.
 * CRC-16/MODBUS (poly 0xA001 reflected, init 0xFFFF) over LEN + payload.
 * Pure C (no RTOS, no HAL): the stream decoder resynchronizes on collisions and on the plain text
 * debug log the devices print on the same bus.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-26
 */

#ifndef RS485_FRAME_H
#define RS485_FRAME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define RS485_FRAME_SYNC0        0xAAU
#define RS485_FRAME_SYNC1        0x55U
#define RS485_FRAME_HEADER_SIZE  5U     /**< Sync (2) + CRC (2) + LEN (1) */
#define RS485_FRAME_PAYLOAD_MIN  2U     /**< Destination + source */
#define RS485_FRAME_PAYLOAD_MAX  255U
#define RS485_FRAME_SIZE_MAX     (RS485_FRAME_HEADER_SIZE + RS485_FRAME_PAYLOAD_MAX)

/** @brief Decoder buffer: one maximum frame plus room for the bytes that follow a false sync. */
#define RS485_FRAME_DECODER_BUF_SIZE  (2U * RS485_FRAME_SIZE_MAX)

/**
 * @brief Decoded frame (payload without sync, CRC and LEN).
 */
typedef struct
{
    uint8_t len;                               /**< Payload length */
    uint8_t payload[RS485_FRAME_PAYLOAD_MAX];  /**< payload[0] = destination, payload[1] = source */
} rs485_frame_t;

/**
 * @brief Stream decoder state (one per bus).
 */
typedef struct
{
    uint8_t buf[RS485_FRAME_DECODER_BUF_SIZE];
    uint16_t len;            /**< Bytes waiting in buf */
    bool idle;               /**< Bus went silent: an incomplete frame candidate is discarded */
    uint32_t frames_ok;      /**< Frames with a valid CRC */
    uint32_t crc_errors;     /**< Complete frame candidates rejected by the CRC check */
    uint32_t skipped_bytes;  /**< Bytes outside frames (text log, collisions, false sync) */
} rs485_frame_decoder_t;

/**
 * @brief Computes CRC-16/MODBUS.
 * @param[in] data Data.
 * @param[in] len Data length.
 * @return CRC value.
 */
uint16_t rs485_frame_crc16(const uint8_t *data, size_t len);

/**
 * @brief Builds a complete frame (sync, CRC, LEN, payload).
 * @param[in] payload Payload (destination, source, data).
 * @param[in] len Payload length (RS485_FRAME_PAYLOAD_MIN..RS485_FRAME_PAYLOAD_MAX).
 * @param[out] out Output buffer.
 * @param[in] out_size Output buffer size.
 * @return Frame length, 0 on invalid parameters or too small buffer.
 */
size_t rs485_frame_build(const uint8_t *payload, size_t len, uint8_t *out, size_t out_size);

/**
 * @brief Resets the decoder state and counters.
 * @param[out] dec Decoder.
 */
void rs485_frame_decoder_init(rs485_frame_decoder_t *dec);

/**
 * @brief Appends one received byte. Call rs485_frame_decoder_next() until it returns false afterwards.
 * @param[in,out] dec Decoder.
 * @param[in] byte Received byte.
 */
void rs485_frame_decoder_push(rs485_frame_decoder_t *dec, uint8_t byte);

/**
 * @brief Signals bus silence: an incomplete frame candidate (false sync, broken frame) is discarded
 *        on the next rs485_frame_decoder_next() calls and the bytes after it are searched again.
 * @param[in,out] dec Decoder.
 */
void rs485_frame_decoder_idle(rs485_frame_decoder_t *dec);

/**
 * @brief Returns the next valid frame from the buffered bytes.
 * @param[in,out] dec Decoder.
 * @param[out] frame Decoded frame.
 * @return true if a frame was decoded, false if more bytes are needed.
 */
bool rs485_frame_decoder_next(rs485_frame_decoder_t *dec, rs485_frame_t *frame);

/**
 * @brief Checks whether bytes are waiting (incomplete frame candidate or unsynchronized bytes).
 * @param[in] dec Decoder.
 * @return true if the buffer is not empty.
 */
bool rs485_frame_decoder_pending(const rs485_frame_decoder_t *dec);

#endif /* RS485_FRAME_H */
