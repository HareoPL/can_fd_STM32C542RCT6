/**
 * @file rs485_frame.c
 * @brief Binary RS485 frame codec of the external telematics devices (docs/rs485/rs485_protocol.md).
 *
 * The decoder keeps the received bytes in a linear buffer. A frame candidate starts at AA 55; when
 * LEN + payload are complete the CRC decides. A rejected candidate drops only its first byte, so a
 * real frame swallowed by a false sync (collision, AA 55 inside data) is found again.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-26
 */

#include "rs485_frame.h"
#include <string.h>

uint16_t rs485_frame_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFU;

    for (size_t i = 0U; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1U) != 0U) ? (uint16_t)((crc >> 1) ^ 0xA001U) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

size_t rs485_frame_build(const uint8_t *payload, size_t len, uint8_t *out, size_t out_size)
{
    if (payload == NULL || out == NULL || len < RS485_FRAME_PAYLOAD_MIN || len > RS485_FRAME_PAYLOAD_MAX ||
        out_size < RS485_FRAME_HEADER_SIZE + len)
    {
        return 0U;
    }

    out[0] = RS485_FRAME_SYNC0;
    out[1] = RS485_FRAME_SYNC1;
    out[4] = (uint8_t)len;
    memcpy(&out[RS485_FRAME_HEADER_SIZE], payload, len);

    const uint16_t crc = rs485_frame_crc16(&out[4], len + 1U);
    out[2] = (uint8_t)(crc & 0xFFU);
    out[3] = (uint8_t)(crc >> 8);
    return RS485_FRAME_HEADER_SIZE + len;
}

void rs485_frame_decoder_init(rs485_frame_decoder_t *dec)
{
    memset(dec, 0, sizeof(*dec));
}

/** @brief Removes count bytes from the front of the buffer. */
static void decoder_drop(rs485_frame_decoder_t *dec, uint16_t count)
{
    if (count >= dec->len)
    {
        dec->len = 0U;
        return;
    }
    memmove(dec->buf, &dec->buf[count], (size_t)(dec->len - count));
    dec->len = (uint16_t)(dec->len - count);
}

void rs485_frame_decoder_push(rs485_frame_decoder_t *dec, uint8_t byte)
{
    if (dec->len >= RS485_FRAME_DECODER_BUF_SIZE)
    {
        /* Not reachable when next() runs after every push; keeps the newest bytes */
        decoder_drop(dec, 1U);
        dec->skipped_bytes++;
    }
    dec->buf[dec->len++] = byte;
    dec->idle = false;
}

void rs485_frame_decoder_idle(rs485_frame_decoder_t *dec)
{
    dec->idle = true;
}

bool rs485_frame_decoder_pending(const rs485_frame_decoder_t *dec)
{
    return (dec->len > 0U);
}

bool rs485_frame_decoder_next(rs485_frame_decoder_t *dec, rs485_frame_t *frame)
{
    for (;;)
    {
        /* Skip everything before the next AA 55 */
        uint16_t start = 0U;
        while (start + 1U < dec->len &&
               !(dec->buf[start] == RS485_FRAME_SYNC0 && dec->buf[start + 1U] == RS485_FRAME_SYNC1))
        {
            start++;
        }
        if (start + 1U >= dec->len)
        {
            /* No sync: keep a trailing AA (first sync byte) unless the bus is idle */
            const bool keep_last = (!dec->idle && dec->len > 0U && dec->buf[dec->len - 1U] == RS485_FRAME_SYNC0);
            const uint16_t drop = keep_last ? (uint16_t)(dec->len - 1U) : dec->len;
            dec->skipped_bytes += drop;
            decoder_drop(dec, drop);
            return false;
        }
        if (start > 0U)
        {
            dec->skipped_bytes += start;
            decoder_drop(dec, start);
        }

        /* Candidate at buf[0] */
        bool reject = false;
        if (dec->len >= RS485_FRAME_HEADER_SIZE && dec->buf[4] < RS485_FRAME_PAYLOAD_MIN)
        {
            reject = true;
        }
        else if (dec->len < RS485_FRAME_HEADER_SIZE ||
                 dec->len < (uint16_t)(RS485_FRAME_HEADER_SIZE + dec->buf[4]))
        {
            if (!dec->idle)
            {
                return false; /* wait for the rest of the frame */
            }
            reject = true;    /* bus silent: the candidate will never complete */
        }
        else
        {
            const uint8_t len = dec->buf[4];
            const uint16_t crc = (uint16_t)(dec->buf[2] | ((uint16_t)dec->buf[3] << 8));
            if (rs485_frame_crc16(&dec->buf[4], (size_t)len + 1U) == crc)
            {
                frame->len = len;
                memcpy(frame->payload, &dec->buf[RS485_FRAME_HEADER_SIZE], len);
                decoder_drop(dec, (uint16_t)(RS485_FRAME_HEADER_SIZE + len));
                dec->frames_ok++;
                return true;
            }
            dec->crc_errors++;
            reject = true;
        }

        if (reject)
        {
            /* Drop the first sync byte only: a real frame may start inside the rejected candidate */
            dec->skipped_bytes++;
            decoder_drop(dec, 1U);
        }
    }
}
