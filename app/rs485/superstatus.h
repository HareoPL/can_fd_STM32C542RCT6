/**
 * @file superstatus.h
 * @brief superStatus request / response of the external telematics devices over RS485 (docs/rs485/rs485_protocol.md).
 *
 * Request payload (14 B): [0x50][src][tool version LE][0x01][password 9 B, zero padded].
 * Response payload: [src][0x50][fields...], field map of the SuperServiceFrame (old UDP documentation,
 * offset in the UDP frame - 5 = payload index). Pure C (no RTOS, no HAL).
 *
 * Naming: "device parser" (dev_parser_no) is the CAN parser file (NNN.CAN) loaded in the external
 * device, not a v-box CAN parser profile (can_parser_*, cp_*).
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-26
 */

#ifndef SUPERSTATUS_H
#define SUPERSTATUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "rs485_frame.h"

#define SUPERSTATUS_ADDR             0x50U  /**< Destination of the request, source of the response */
#define SUPERSTATUS_FRAME_TYPE_REQ   0x01U
#define SUPERSTATUS_PASSWORD_LEN     9U
#define SUPERSTATUS_REQ_PAYLOAD_LEN  14U
#define SUPERSTATUS_REQ_FRAME_LEN    (RS485_FRAME_HEADER_SIZE + SUPERSTATUS_REQ_PAYLOAD_LEN)
#define SUPERSTATUS_RESP_MIN_LEN     122U   /**< Payload up to and including can2_dev_parser_no */
#define SUPERSTATUS_TIME_TEXT_SIZE   20U    /**< "YYYY-MM-DD hh:mm:ss" + NUL */

/**
 * @brief Decoded superStatus response (fields that are not sent by the device stay 0 / empty).
 */
typedef struct
{
    uint16_t fw_version;          /**< Device firmware version (e.g. 9550) */
    uint8_t frame_type;
    uint32_t time_date;           /**< Seconds since 2000-01-01 00:00:00 UTC (not Unix time) */
    uint8_t bin_inputs;           /**< Binary inputs bitmask */
    uint8_t bin_outputs;          /**< Binary outputs bitmask */
    int16_t main_power;           /**< Supply voltage, probably 10 mV units (1290 = 12.90 V, to be verified) */
    uint8_t batt_charge;          /**< Backup battery charge [%] */
    int8_t batt_temp;             /**< Backup battery temperature [°C] */
    uint8_t power_status;
    uint32_t flow;
    uint8_t gps_satellites;
    uint8_t gps_status;
    uint16_t hdop;
    uint8_t gsm_status;
    char gsm_operator[7];         /**< MCC + MNC, e.g. "26002" */
    char sim_id[21];              /**< ICCID */
    uint32_t activity;
    uint8_t chip[8];
    uint8_t minor_soft_version;
    uint8_t can_car_id;
    uint16_t can_mod_revision;
    uint32_t time_date_rtc;       /**< RTC, seconds since 2000-01-01 00:00:00 UTC */
    uint8_t modem_ver[3];         /**< GSM modem version major, minor, ext */
    char imei[17];
    uint16_t rtc_counter;
    uint16_t can_locker_version;
    uint16_t system_status;
    uint16_t files_to_send;
    uint16_t can1_dev_parser_no;  /**< Device parser loaded for CAN1 (file NNN.CAN), 0 = none */
    uint16_t can2_dev_parser_no;  /**< Device parser loaded for CAN2, 0 = none */
    char modem_version[9];
    char app_version[17];
} superstatus_t;

/**
 * @brief Result of superstatus_decode().
 */
typedef enum
{
    SUPERSTATUS_RESULT_OK = 0,        /**< superStatus response decoded */
    SUPERSTATUS_RESULT_NOT_RESPONSE,  /**< Other frame (not addressed to src or not from 0x50) */
    SUPERSTATUS_RESULT_TOO_SHORT,     /**< superStatus response shorter than SUPERSTATUS_RESP_MIN_LEN */
    SUPERSTATUS_RESULT_WRONG_PASSWORD /**< Device rejected the password */
} superstatus_result_t;

/**
 * @brief Builds a complete superStatus request frame.
 * @param[in] src Source address (service tool address, e.g. 0x90).
 * @param[in] tool_version Service tool version sent in the request.
 * @param[in] password Password, NUL terminated, at most SUPERSTATUS_PASSWORD_LEN characters (zero padded).
 * @param[out] out Output buffer.
 * @param[in] out_size Output buffer size (at least SUPERSTATUS_REQ_FRAME_LEN).
 * @return Frame length, 0 on invalid parameters.
 */
size_t superstatus_build_request(uint8_t src, uint16_t tool_version, const char *password, uint8_t *out, size_t out_size);

/**
 * @brief Checks whether the frame is a superStatus request from src (e.g. sent by another master).
 * @param[in] frame Decoded frame.
 * @param[in] src Source address.
 * @return true for a superStatus request.
 */
bool superstatus_is_request(const rs485_frame_t *frame, uint8_t src);

/**
 * @brief Decodes a superStatus response addressed to src.
 * @param[in] frame Decoded frame.
 * @param[in] src Source address of the request (destination of the response).
 * @param[out] status Decoded fields (written only for SUPERSTATUS_RESULT_OK).
 * @return Decoding result.
 */
superstatus_result_t superstatus_decode(const rs485_frame_t *frame, uint8_t src, superstatus_t *status);

/**
 * @brief Formats a device time (seconds since 2000-01-01 UTC) as "YYYY-MM-DD hh:mm:ss".
 * @param[in] seconds Seconds since 2000-01-01 00:00:00 UTC.
 * @param[out] buf Output buffer.
 * @param[in] size Output buffer size (SUPERSTATUS_TIME_TEXT_SIZE).
 */
void superstatus_format_time(uint32_t seconds, char *buf, size_t size);

#endif /* SUPERSTATUS_H */
