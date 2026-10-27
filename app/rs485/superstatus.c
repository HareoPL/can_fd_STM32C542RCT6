/**
 * @file superstatus.c
 * @brief superStatus request / response of the external telematics devices over RS485 (docs/rs485/rs485_protocol.md).
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-26
 */

#include "superstatus.h"
#include <stdio.h>
#include <string.h>

/* Response payload offsets (old UDP documentation offset - 5) */
#define SS_OFF_VERSION          2U
#define SS_OFF_FRAME_TYPE       4U
#define SS_OFF_TIME_DATE        5U
#define SS_OFF_BIN_INPUTS       9U
#define SS_OFF_BIN_OUTPUTS      10U
#define SS_OFF_MAIN_POWER       11U
#define SS_OFF_BATT_CHARGE      13U
#define SS_OFF_BATT_TEMP        14U
#define SS_OFF_POWER_STATUS     15U
#define SS_OFF_FLOW             16U
#define SS_OFF_GPS_SATELLITES   20U
#define SS_OFF_GPS_STATUS       21U
#define SS_OFF_HDOP             22U
#define SS_OFF_GSM_STATUS       24U
#define SS_OFF_GSM_OPERATOR     25U   /* 6 B */
#define SS_OFF_SIM_ID           31U   /* 20 B */
#define SS_OFF_ACTIVITY_LO      51U
#define SS_OFF_ACTIVITY_HI      53U
#define SS_OFF_CHIP             55U   /* 8 B */
#define SS_OFF_MINOR_SOFT_VER   63U
#define SS_OFF_CAN_CAR_ID       64U
#define SS_OFF_CAN_MOD_REV      65U
#define SS_OFF_TIME_DATE_RTC    79U
#define SS_OFF_MODEM_VER        91U   /* 3 B */
#define SS_OFF_IMEI             94U   /* 16 B */
#define SS_OFF_RTC_COUNTER      110U
#define SS_OFF_CAN_LOCKER_VER   112U
#define SS_OFF_SYSTEM_STATUS    114U
#define SS_OFF_FILES_TO_SEND    116U
#define SS_OFF_CAN1_PARSER      118U
#define SS_OFF_CAN2_PARSER      120U
#define SS_OFF_MODEM_VERSION    122U  /* 8 B */
#define SS_OFF_APP_VERSION      130U  /* 16 B */

/** @brief Wrong password answer: payload [src][0xD5][0C 06 02] (constant signature of the device). */
#define SS_WRONG_PWD_SRC        0xD5U
static const uint8_t s_wrong_pwd_tail[] = { 0x0CU, 0x06U, 0x02U };

static uint16_t get_u16(const uint8_t *p, size_t off)
{
    return (uint16_t)(p[off] | ((uint16_t)p[off + 1U] << 8));
}

static uint32_t get_u32(const uint8_t *p, size_t off)
{
    return (uint32_t)p[off] | ((uint32_t)p[off + 1U] << 8) | ((uint32_t)p[off + 2U] << 16) |
           ((uint32_t)p[off + 3U] << 24);
}

/** @brief Copies a zero padded ASCII field and terminates it (dst_size = field length + 1). */
static void get_text(const rs485_frame_t *frame, size_t off, size_t field_len, char *dst, size_t dst_size)
{
    memset(dst, 0, dst_size);
    for (size_t i = 0U; i < field_len && i + 1U < dst_size && off + i < frame->len; i++)
    {
        const uint8_t c = frame->payload[off + i];
        if (c == 0U)
        {
            break;
        }
        dst[i] = (c >= 0x20U && c < 0x7FU) ? (char)c : '?';
    }
}

size_t superstatus_build_request(uint8_t src, uint16_t tool_version, const char *password, uint8_t *out, size_t out_size)
{
    if (password == NULL || strlen(password) > SUPERSTATUS_PASSWORD_LEN)
    {
        return 0U;
    }

    uint8_t payload[SUPERSTATUS_REQ_PAYLOAD_LEN] = { 0U };
    payload[0] = SUPERSTATUS_ADDR;
    payload[1] = src;
    payload[2] = (uint8_t)(tool_version & 0xFFU);
    payload[3] = (uint8_t)(tool_version >> 8);
    payload[4] = SUPERSTATUS_FRAME_TYPE_REQ;
    memcpy(&payload[5], password, strlen(password));

    return rs485_frame_build(payload, sizeof(payload), out, out_size);
}

bool superstatus_is_request(const rs485_frame_t *frame, uint8_t src)
{
    return (frame->len == SUPERSTATUS_REQ_PAYLOAD_LEN && frame->payload[0] == SUPERSTATUS_ADDR &&
            frame->payload[1] == src && frame->payload[4] == SUPERSTATUS_FRAME_TYPE_REQ);
}

superstatus_result_t superstatus_decode(const rs485_frame_t *frame, uint8_t src, superstatus_t *status)
{
    const uint8_t *p = frame->payload;

    if (frame->len == 2U + sizeof(s_wrong_pwd_tail) && p[0] == src && p[1] == SS_WRONG_PWD_SRC &&
        memcmp(&p[2], s_wrong_pwd_tail, sizeof(s_wrong_pwd_tail)) == 0)
    {
        return SUPERSTATUS_RESULT_WRONG_PASSWORD;
    }
    if (frame->len < RS485_FRAME_PAYLOAD_MIN || p[0] != src || p[1] != SUPERSTATUS_ADDR)
    {
        return SUPERSTATUS_RESULT_NOT_RESPONSE;
    }
    if (frame->len < SUPERSTATUS_RESP_MIN_LEN)
    {
        return SUPERSTATUS_RESULT_TOO_SHORT;
    }

    memset(status, 0, sizeof(*status));
    status->fw_version = get_u16(p, SS_OFF_VERSION);
    status->frame_type = p[SS_OFF_FRAME_TYPE];
    status->time_date = get_u32(p, SS_OFF_TIME_DATE);
    status->bin_inputs = p[SS_OFF_BIN_INPUTS];
    status->bin_outputs = p[SS_OFF_BIN_OUTPUTS];
    status->main_power = (int16_t)get_u16(p, SS_OFF_MAIN_POWER);
    status->batt_charge = p[SS_OFF_BATT_CHARGE];
    status->batt_temp = (int8_t)p[SS_OFF_BATT_TEMP];
    status->power_status = p[SS_OFF_POWER_STATUS];
    status->flow = get_u32(p, SS_OFF_FLOW);
    status->gps_satellites = p[SS_OFF_GPS_SATELLITES];
    status->gps_status = p[SS_OFF_GPS_STATUS];
    status->hdop = get_u16(p, SS_OFF_HDOP);
    status->gsm_status = p[SS_OFF_GSM_STATUS];
    get_text(frame, SS_OFF_GSM_OPERATOR, 6U, status->gsm_operator, sizeof(status->gsm_operator));
    get_text(frame, SS_OFF_SIM_ID, 20U, status->sim_id, sizeof(status->sim_id));
    status->activity = (uint32_t)get_u16(p, SS_OFF_ACTIVITY_LO) | ((uint32_t)get_u16(p, SS_OFF_ACTIVITY_HI) << 16);
    memcpy(status->chip, &p[SS_OFF_CHIP], sizeof(status->chip));
    status->minor_soft_version = p[SS_OFF_MINOR_SOFT_VER];
    status->can_car_id = p[SS_OFF_CAN_CAR_ID];
    status->can_mod_revision = get_u16(p, SS_OFF_CAN_MOD_REV);
    status->time_date_rtc = get_u32(p, SS_OFF_TIME_DATE_RTC);
    memcpy(status->modem_ver, &p[SS_OFF_MODEM_VER], sizeof(status->modem_ver));
    get_text(frame, SS_OFF_IMEI, 16U, status->imei, sizeof(status->imei));
    status->rtc_counter = get_u16(p, SS_OFF_RTC_COUNTER);
    status->can_locker_version = get_u16(p, SS_OFF_CAN_LOCKER_VER);
    status->system_status = get_u16(p, SS_OFF_SYSTEM_STATUS);
    status->files_to_send = get_u16(p, SS_OFF_FILES_TO_SEND);
    status->can1_dev_parser_no = get_u16(p, SS_OFF_CAN1_PARSER);
    status->can2_dev_parser_no = get_u16(p, SS_OFF_CAN2_PARSER);
    /* Optional tail (the field map ends at payload 151) */
    get_text(frame, SS_OFF_MODEM_VERSION, 8U, status->modem_version, sizeof(status->modem_version));
    get_text(frame, SS_OFF_APP_VERSION, 16U, status->app_version, sizeof(status->app_version));

    return SUPERSTATUS_RESULT_OK;
}

void superstatus_format_time(uint32_t seconds, char *buf, size_t size)
{
    /* civil_from_days (H. Hinnant): days since 1970-01-01 shifted to an era starting on 0000-03-01 */
    const uint32_t days = seconds / 86400U;
    const uint32_t sod = seconds % 86400U;
    const int32_t z = (int32_t)days + 10957 + 719468;  /* 10957 days from 1970-01-01 to 2000-01-01 */
    const int32_t era = z / 146097;
    const uint32_t doe = (uint32_t)(z - era * 146097);
    const uint32_t yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    const uint32_t doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    const uint32_t mp = (5U * doy + 2U) / 153U;
    const uint32_t day = doy - (153U * mp + 2U) / 5U + 1U;
    const uint32_t month = (mp < 10U) ? mp + 3U : mp - 9U;
    const uint32_t year = (uint32_t)((int32_t)yoe + era * 400) + ((month <= 2U) ? 1U : 0U);

    (void)snprintf(buf, size, "%04lu-%02lu-%02lu %02lu:%02lu:%02lu", (unsigned long)year, (unsigned long)month,
                   (unsigned long)day, (unsigned long)(sod / 3600U), (unsigned long)((sod / 60U) % 60U),
                   (unsigned long)(sod % 60U));
}
