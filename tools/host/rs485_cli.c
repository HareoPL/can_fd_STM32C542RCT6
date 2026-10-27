/**
 * @file rs485_cli.c
 * @brief Host harness for app/rs485/rs485_frame.c + app/rs485/superstatus.c (tools/test_rs485_host.py).
 *
 * stdin:  one line per step:
 *           "REQ <src_hex> <tool_version_hex> <password>"  builds a superStatus request frame
 *           "FEED <hex bytes>"                              pushes bytes into the stream decoder
 *           "IDLE"                                          signals bus silence to the decoder
 *           "STATS"                                         prints the decoder counters
 *           "TIME <seconds>"                                formats a device time (seconds since 2000)
 * stdout: one line per step, items joined with " | ":
 *           REQ   -> frame hex
 *           FEED / IDLE -> "F <payload hex>" per decoded frame, followed by "SS ..." for a superStatus
 *                    response to 0x90 (or "SS WRONG_PASSWORD"); "-" if nothing was decoded
 */

#include "rs485_frame.h"
#include "superstatus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLI_SRC 0x90U

static rs485_frame_decoder_t s_dec;
static char s_out[16384];

static void out(const char *text)
{
    if (s_out[0] != '\0')
    {
        strncat(s_out, " | ", sizeof(s_out) - strlen(s_out) - 1U);
    }
    strncat(s_out, text, sizeof(s_out) - strlen(s_out) - 1U);
}

/** @brief Formats bytes as "AA 55 ..." (size >= 3 * len). */
static void hex(const uint8_t *data, size_t len, char *buf, size_t size)
{
    size_t pos = 0U;

    buf[0] = '\0';
    for (size_t i = 0U; i < len && pos + 4U <= size; i++)
    {
        pos += (size_t)snprintf(&buf[pos], size - pos, i == 0U ? "%02X" : " %02X", data[i]);
    }
}

static void report_frame(const rs485_frame_t *frame)
{
    static char line[1024];
    static char payload_hex[RS485_FRAME_PAYLOAD_MAX * 3U + 1U];

    hex(frame->payload, frame->len, payload_hex, sizeof(payload_hex));
    snprintf(line, sizeof(line), "F %s", payload_hex);
    out(line);

    superstatus_t ss;
    switch (superstatus_decode(frame, CLI_SRC, &ss))
    {
        case SUPERSTATUS_RESULT_OK:
        {
            char time_text[SUPERSTATUS_TIME_TEXT_SIZE];
            superstatus_format_time(ss.time_date, time_text, sizeof(time_text));
            snprintf(line, sizeof(line),
                     "SS fw=%u time=%s in=%02X out=%02X pwr=%d temp=%d sat=%u hdop=%u gsm=%02X op=%s sim=%s "
                     "modem=%u.%u.%u imei=%s can1=%u can2=%u",
                     (unsigned)ss.fw_version, time_text, (unsigned)ss.bin_inputs, (unsigned)ss.bin_outputs,
                     (int)ss.main_power, (int)ss.batt_temp, (unsigned)ss.gps_satellites, (unsigned)ss.hdop,
                     (unsigned)ss.gsm_status, ss.gsm_operator, ss.sim_id, (unsigned)ss.modem_ver[0],
                     (unsigned)ss.modem_ver[1], (unsigned)ss.modem_ver[2], ss.imei,
                     (unsigned)ss.can1_dev_parser_no, (unsigned)ss.can2_dev_parser_no);
            out(line);
            break;
        }
        case SUPERSTATUS_RESULT_WRONG_PASSWORD:
            out("SS WRONG_PASSWORD");
            break;
        case SUPERSTATUS_RESULT_TOO_SHORT:
            out("SS TOO_SHORT");
            break;
        default:
            break;
    }
    if (superstatus_is_request(frame, CLI_SRC))
    {
        out("SS REQUEST");
    }
}

static void drain(void)
{
    rs485_frame_t frame;
    while (rs485_frame_decoder_next(&s_dec, &frame))
    {
        report_frame(&frame);
    }
}

int main(void)
{
    static char line[16384];

    rs485_frame_decoder_init(&s_dec);

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';
        s_out[0] = '\0';

        if (strncmp(line, "REQ ", 4) == 0)
        {
            char *end;
            const uint8_t src = (uint8_t)strtoul(line + 4, &end, 16);
            const uint16_t version = (uint16_t)strtoul(end, &end, 16);
            while (*end == ' ')
            {
                end++;
            }
            uint8_t frame[64];
            char text[256];
            const size_t len = superstatus_build_request(src, version, end, frame, sizeof(frame));
            hex(frame, len, text, sizeof(text));
            out(len > 0U ? text : "ERR");
        }
        else if (strncmp(line, "FEED ", 5) == 0)
        {
            char *p = line + 5;
            char *end;
            for (;;)
            {
                const unsigned long value = strtoul(p, &end, 16);
                if (end == p)
                {
                    break;
                }
                rs485_frame_decoder_push(&s_dec, (uint8_t)value);
                drain();
                p = end;
            }
        }
        else if (strcmp(line, "IDLE") == 0)
        {
            rs485_frame_decoder_idle(&s_dec);
            drain();
        }
        else if (strcmp(line, "STATS") == 0)
        {
            char text[128];
            snprintf(text, sizeof(text), "ok=%lu crc=%lu skipped=%lu pending=%u", (unsigned long)s_dec.frames_ok,
                     (unsigned long)s_dec.crc_errors, (unsigned long)s_dec.skipped_bytes, (unsigned)s_dec.len);
            out(text);
        }
        else if (strncmp(line, "TIME ", 5) == 0)
        {
            char text[SUPERSTATUS_TIME_TEXT_SIZE];
            superstatus_format_time((uint32_t)strtoul(line + 5, NULL, 10), text, sizeof(text));
            out(text);
        }
        else
        {
            out("UNKNOWN");
        }

        printf("%s\n", s_out[0] != '\0' ? s_out : "-");
    }
    return 0;
}
