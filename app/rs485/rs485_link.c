/**
 * @file rs485_link.c
 * @brief RS485 link to the external telematics device: periodic superStatus and detection of a
 *        device parser change (docs/rs485/rs485_link.md).
 *
 * Data path: UART ISR -> lock-free single producer / single consumer ring buffer -> task notification
 * -> rs485_frame decoder -> frame handling. A ring buffer is used instead of a FreeRTOS stream buffer
 * because configUSE_STREAM_BUFFERS = 0 in the generated FreeRTOSConfig.h.
 *
 * Bus access rules (the device bus is shared with the PC service tool):
 *  - a request is sent only after RS485_LINK_BUS_IDLE_MS of silence,
 *  - a frame of another master with the same source address puts the link into passive listening for
 *    RS485_LINK_PASSIVE_HOLD_MS (own requests received back as echo are ignored),
 *  - file transfer frames (device parser update) stop the requests until the transfer end,
 *  - every superStatus response is decoded, also the ones requested by the service tool.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-26
 */

#include "rs485_link.h"
#include "rs485_link_config.h"
#include "rs485_frame.h"
#include "safe_printf.h"
#include "tasks_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

#define RS485_TAG                 "[RS485] "
#define RS485_RX_BUF_SIZE         1024U  /**< ISR -> task ring buffer, power of 2 */
#define RS485_TX_TIMEOUT_MS       50U    /**< 19 B request = 3.3 ms at 57600 baud */
#define RS485_FIRST_POLL_MS       1000U  /**< First request after start */
#define RS485_OUT_LINE_SIZE       160U

/* File transfer (device parser update) frames of the service tool */
#define RS485_ADDR_FILE_WRITE     0x24U  /**< [0x24][src][addr32][data] */
#define RS485_ADDR_FILE_CTRL      0x26U  /**< [0x26][src]...: start (name, size) / finish */
#define RS485_FILE_START_SIZE_OFF 17U    /**< u32 LE file size in the start frame */
#define RS485_FILE_START_NAME_OFF 21U    /**< NUL terminated file name in the start frame (e.g. "195.CAN") */
#define RS485_FILE_NAME_MAX       16U
#define RS485_FILE_ACK_CODE       0x04U  /**< Device ACK [src][0x26][fw16][0x04][step] */
#define RS485_FILE_ACK_END        0x02U  /**< step: transfer finished */

_Static_assert((RS485_RX_BUF_SIZE & (RS485_RX_BUF_SIZE - 1U)) == 0U, "RX buffer size must be a power of 2");
_Static_assert(sizeof(RS485_LINK_PASSWORD) - 1U <= SUPERSTATUS_PASSWORD_LEN, "RS485_LINK_PASSWORD too long");

typedef enum
{
    LINK_STATE_UNKNOWN = 0,  /**< No answer yet */
    LINK_STATE_OK,           /**< superStatus received */
    LINK_STATE_LOST,         /**< RS485_LINK_LOST_POLLS polls without answer */
    LINK_STATE_WRONG_PASSWORD
} link_state_t;

typedef enum
{
    HOLD_NONE = 0,
    HOLD_PASSIVE,   /**< Another master talks to the device */
    HOLD_TRANSFER   /**< File transfer (device parser update) in progress */
} hold_reason_t;

static const uart_interface_t *s_uart = NULL;
static TaskHandle_t s_task = NULL;
static rs485_link_parser_change_fn s_change_cb = NULL;

/** @brief ISR -> task ring buffer: head written by the ISR only, tail by the task only. */
static volatile uint8_t s_rx_ring[RS485_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;
static volatile uint32_t s_rx_overflow = 0U;

/** @brief Decoder and frame (task context only, kept off the task stack). */
static rs485_frame_decoder_t s_dec;
static rs485_frame_t s_frame;
static superstatus_t s_decoded;

/** @brief Polling state (task context only). */
static TickType_t s_last_rx = 0U;
static bool s_rx_seen = false;
static TickType_t s_next_poll = 0U;
static bool s_awaiting = false;
static TickType_t s_deadline = 0U;
static uint8_t s_retries = 0U;
static uint8_t s_failed_polls = 0U;
static uint8_t s_tx_frame[SUPERSTATUS_REQ_FRAME_LEN];
static size_t s_tx_len = 0U;
static TickType_t s_last_tx = 0U;
static uint16_t s_known_no[2] = { RS485_DEV_PARSER_NO_UNKNOWN, RS485_DEV_PARSER_NO_UNKNOWN };

/** @brief State shared with the console command (written by the task). */
static volatile link_state_t s_link_state = LINK_STATE_UNKNOWN;
static volatile hold_reason_t s_hold = HOLD_NONE;
static volatile TickType_t s_hold_until = 0U;
static volatile bool s_poll_request = false;
static volatile uint32_t s_cnt_tx = 0U;
static volatile uint32_t s_cnt_resp = 0U;
static volatile uint32_t s_cnt_timeout = 0U;
static volatile uint32_t s_cnt_echo = 0U;
static volatile uint32_t s_cnt_foreign = 0U;

/** @brief Last superStatus, copied under a critical section. */
static superstatus_t s_status;
static TickType_t s_status_tick = 0U;
static bool s_status_valid = false;

/* --- Time helpers --- */

static bool tick_reached(TickType_t now, TickType_t t)
{
    return (int32_t)(now - t) >= 0;
}

static uint32_t elapsed_ms(TickType_t now, TickType_t since)
{
    return (uint32_t)(now - since) * portTICK_PERIOD_MS;
}

/** @brief Shortens wait so that the task wakes up at tick t. */
static void wake_at(TickType_t *wait, TickType_t now, TickType_t t)
{
    const TickType_t left = tick_reached(now, t) ? 1U : (TickType_t)(t - now);
    if (left < *wait)
    {
        *wait = left;
    }
}

static bool hold_active(TickType_t now)
{
    return (s_hold != HOLD_NONE) && !tick_reached(now, s_hold_until);
}

/* --- Bus access --- */

/** @brief Starts or extends a hold of the requests. An active transfer hold is never downgraded to passive. */
static void hold_set(hold_reason_t reason, TickType_t now, uint32_t duration_ms)
{
    const TickType_t until = now + pdMS_TO_TICKS(duration_ms);
    const bool active = hold_active(now);

    if (active && s_hold == HOLD_TRANSFER && reason == HOLD_PASSIVE)
    {
        return;
    }
    if (!active && reason == HOLD_PASSIVE)
    {
        safe_printf(NULL, RS485_TAG "inny master na magistrali (program serwisowy) - nasłuch pasywny\r\n");
    }
    s_hold = reason;
    if (!active || tick_reached(until, s_hold_until))
    {
        s_hold_until = until;
    }
}

static void send_request(void)
{
    if (s_tx_len == 0U)
    {
        s_tx_len = superstatus_build_request(RS485_LINK_SRC_ADDR, RS485_LINK_TOOL_VERSION, RS485_LINK_PASSWORD,
                                             s_tx_frame, sizeof(s_tx_frame));
    }
    if (s_tx_len == 0U || !s_uart->transmit(s_tx_frame, (uint16_t)s_tx_len, RS485_TX_TIMEOUT_MS))
    {
        safe_printf(NULL, RS485_TAG "ERROR: nadawanie zapytania superStatus nie powiodło się\r\n");
    }
    s_cnt_tx++;
    s_last_tx = xTaskGetTickCount();
    s_awaiting = true;
    s_deadline = s_last_tx + pdMS_TO_TICKS(RS485_LINK_RESP_TIMEOUT_MS);
}

/** @brief Own request received back (transceiver echo) within RS485_LINK_ECHO_WINDOW_MS. */
static bool is_own_echo(const rs485_frame_t *frame, TickType_t now)
{
    return (s_tx_len == RS485_FRAME_HEADER_SIZE + frame->len) &&
           (memcmp(frame->payload, &s_tx_frame[RS485_FRAME_HEADER_SIZE], frame->len) == 0) &&
           (elapsed_ms(now, s_last_tx) <= RS485_LINK_ECHO_WINDOW_MS);
}

/* --- Frame handling --- */

static void report_parser_change(const superstatus_t *ss)
{
    const uint16_t now_no[2] = { ss->can1_dev_parser_no, ss->can2_dev_parser_no };

    for (uint8_t i = 0U; i < 2U; i++)
    {
        if (now_no[i] == s_known_no[i])
        {
            continue;
        }
        const uint16_t old_no = s_known_no[i];
        s_known_no[i] = now_no[i];
        /* CAN2 without a parser is not worth a notification on the first reading */
        const bool silent = (i == 1U) && (old_no == RS485_DEV_PARSER_NO_UNKNOWN) && (now_no[i] == 0U);
        if (!silent && s_change_cb != NULL)
        {
            s_change_cb((uint8_t)(i + 1U), old_no, now_no[i]);
        }
    }
}

static void on_superstatus(const superstatus_t *ss, TickType_t now)
{
    /* Any answer (also requested by the service tool) restarts the polling period */
    s_awaiting = false;
    s_retries = 0U;
    s_failed_polls = 0U;
    s_next_poll = now + pdMS_TO_TICKS(RS485_LINK_POLL_PERIOD_MS);
    s_cnt_resp++;

    if (s_link_state != LINK_STATE_OK)
    {
        safe_printf(NULL, RS485_TAG "komunikacja z urządzeniem OK (fw %u, IMEI %s)\r\n", (unsigned)ss->fw_version,
                    ss->imei);
        s_link_state = LINK_STATE_OK;
    }

    taskENTER_CRITICAL();
    s_status = *ss;
    s_status_tick = now;
    s_status_valid = true;
    taskEXIT_CRITICAL();

    report_parser_change(ss);
}

static void on_wrong_password(TickType_t now)
{
    s_awaiting = false;
    s_retries = 0U;
    s_next_poll = now + pdMS_TO_TICKS(RS485_LINK_POLL_PERIOD_MS);
    if (s_link_state != LINK_STATE_WRONG_PASSWORD)
    {
        safe_printf(NULL, RS485_TAG "ERROR: urządzenie odrzuciło hasło \"%s\" (RS485_LINK_PASSWORD)\r\n",
                    RS485_LINK_PASSWORD);
        s_link_state = LINK_STATE_WRONG_PASSWORD;
    }
}

/** @brief Request of another master using our source address (PC service tool). */
static void on_foreign_request(const rs485_frame_t *frame, TickType_t now)
{
    const uint8_t *p = frame->payload;
    s_cnt_foreign++;

    if (p[0] != RS485_ADDR_FILE_WRITE && p[0] != RS485_ADDR_FILE_CTRL)
    {
        hold_set(HOLD_PASSIVE, now, RS485_LINK_PASSIVE_HOLD_MS);
        return;
    }

    /* Transfer start: [0x26][src][00 00 ...][size u32 @17][name @21] */
    if (p[0] == RS485_ADDR_FILE_CTRL && frame->len > RS485_FILE_START_NAME_OFF && p[2] == 0U && p[3] == 0U)
    {
        char name[RS485_FILE_NAME_MAX + 1U];
        size_t n = 0U;
        while (n < RS485_FILE_NAME_MAX && RS485_FILE_START_NAME_OFF + n < frame->len &&
               p[RS485_FILE_START_NAME_OFF + n] >= 0x20U && p[RS485_FILE_START_NAME_OFF + n] < 0x7FU)
        {
            name[n] = (char)p[RS485_FILE_START_NAME_OFF + n];
            n++;
        }
        name[n] = '\0';
        const uint32_t size = (uint32_t)p[RS485_FILE_START_SIZE_OFF] | ((uint32_t)p[RS485_FILE_START_SIZE_OFF + 1U] << 8) |
                              ((uint32_t)p[RS485_FILE_START_SIZE_OFF + 2U] << 16) |
                              ((uint32_t)p[RS485_FILE_START_SIZE_OFF + 3U] << 24);
        safe_printf(NULL, RS485_TAG "wgrywanie pliku do urządzenia: %s (%lu B) - odpytywanie wstrzymane\r\n", name,
                    (unsigned long)size);
    }
    hold_set(HOLD_TRANSFER, now, RS485_LINK_TRANSFER_HOLD_MS);
}

static void handle_frame(const rs485_frame_t *frame, TickType_t now)
{
    const uint8_t *p = frame->payload;

    switch (superstatus_decode(frame, RS485_LINK_SRC_ADDR, &s_decoded))
    {
        case SUPERSTATUS_RESULT_OK:
            on_superstatus(&s_decoded, now);
            return;
        case SUPERSTATUS_RESULT_WRONG_PASSWORD:
            on_wrong_password(now);
            return;
        case SUPERSTATUS_RESULT_TOO_SHORT:
            return;
        default:
            break;
    }

    if (p[1] == RS485_LINK_SRC_ADDR && p[0] != RS485_LINK_SRC_ADDR)
    {
        if (is_own_echo(frame, now))
        {
            s_cnt_echo++;
            return;
        }
        on_foreign_request(frame, now);
        return;
    }

    /* Transfer end: device ACK [src][0x26][fw16][0x04][0x02] */
    if (p[0] == RS485_LINK_SRC_ADDR && p[1] == RS485_ADDR_FILE_CTRL && frame->len >= 6U &&
        p[4] == RS485_FILE_ACK_CODE && p[5] == RS485_FILE_ACK_END)
    {
        safe_printf(NULL, RS485_TAG "wgrywanie pliku zakończone - superStatus za %u ms\r\n",
                    (unsigned)RS485_LINK_AFTER_TRANSFER_MS);
        s_hold = HOLD_TRANSFER;
        s_hold_until = now + pdMS_TO_TICKS(RS485_LINK_AFTER_TRANSFER_MS);
        s_next_poll = s_hold_until;
    }
}

static void drain_frames(TickType_t now)
{
    while (rs485_frame_decoder_next(&s_dec, &s_frame))
    {
        handle_frame(&s_frame, now);
    }
}

/* --- Polling --- */

static void check_timeout(TickType_t now)
{
    if (!s_awaiting || !tick_reached(now, s_deadline))
    {
        return;
    }

    s_awaiting = false;
    s_cnt_timeout++;
    if (s_retries < RS485_LINK_RETRIES)
    {
        s_retries++;
        s_next_poll = now; /* repeat as soon as the bus is silent */
        return;
    }

    s_retries = 0U;
    s_next_poll = now + pdMS_TO_TICKS(RS485_LINK_POLL_PERIOD_MS);
    if (s_failed_polls < UINT8_MAX)
    {
        s_failed_polls++;
    }
    if (s_failed_polls >= RS485_LINK_LOST_POLLS && s_link_state != LINK_STATE_LOST)
    {
        safe_printf(NULL, RS485_TAG "brak odpowiedzi urządzenia na superStatus (%u prób) - utrata komunikacji\r\n",
                    (unsigned)(RS485_LINK_LOST_POLLS * (RS485_LINK_RETRIES + 1U)));
        s_link_state = LINK_STATE_LOST;
    }
}

static void try_poll(TickType_t now)
{
    bool forced = false;

    if (s_poll_request)
    {
        s_poll_request = false;
        s_next_poll = now;
        forced = true;
    }

    if (s_hold != HOLD_NONE && !hold_active(now))
    {
        if (s_hold == HOLD_PASSIVE)
        {
            safe_printf(NULL, RS485_TAG "koniec nasłuchu pasywnego - odpytywanie superStatus\r\n");
        }
        s_hold = HOLD_NONE;
    }

    if (s_awaiting || !tick_reached(now, s_next_poll))
    {
        return;
    }
    if (hold_active(now) && !(forced && s_hold == HOLD_PASSIVE))
    {
        return;
    }
    if (s_rx_seen && elapsed_ms(now, s_last_rx) < RS485_LINK_BUS_IDLE_MS)
    {
        return; /* wait for a silent bus */
    }
    send_request();
}

/** @brief Time until the next event: data is signalled by the ISR notification. */
static TickType_t next_wait(TickType_t now)
{
    TickType_t wait = pdMS_TO_TICKS(RS485_LINK_POLL_PERIOD_MS);

    if (s_awaiting)
    {
        wake_at(&wait, now, s_deadline);
    }
    else if (s_hold != HOLD_NONE)
    {
        wake_at(&wait, now, s_hold_until); /* a due poll waits for the end of the hold */
    }
    else if (!tick_reached(now, s_next_poll))
    {
        wake_at(&wait, now, s_next_poll);
    }
    else
    {
        /* Poll due: wait for a silent bus */
        wake_at(&wait, now, s_rx_seen ? (s_last_rx + pdMS_TO_TICKS(RS485_LINK_BUS_IDLE_MS)) : now);
    }
    if (rs485_frame_decoder_pending(&s_dec))
    {
        wake_at(&wait, now, s_last_rx + pdMS_TO_TICKS(RS485_LINK_FRAME_GAP_MS));
    }
    return wait;
}

/**
 * @brief UART byte callback (ISR context): stores the byte and wakes the RS485 task.
 */
static void rs485_link_on_rx_byte(uint8_t byte, void *context)
{
    (void)context;

    uint16_t head = s_rx_head;
    uint16_t next = (uint16_t)((head + 1U) & (RS485_RX_BUF_SIZE - 1U));
    if (next == s_rx_tail)
    {
        s_rx_overflow++;
    }
    else
    {
        s_rx_ring[head] = byte;
        s_rx_head = next;
    }

    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    portYIELD_FROM_ISR(woken);
}

static void rs485_link_task(void *pvParameters)
{
    (void)pvParameters;

    /* Reception is armed from the task, so the ISR never notifies before the scheduler runs */
    if (!s_uart->start_rx())
    {
        safe_printf(NULL, RS485_TAG "ERROR: UART reception start failed\r\n");
        vTaskDelete(NULL);
        return;
    }

    TickType_t now = xTaskGetTickCount();
    s_next_poll = now + pdMS_TO_TICKS(RS485_FIRST_POLL_MS);

    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, next_wait(now));
        now = xTaskGetTickCount();

        bool received = false;
        while (s_rx_tail != s_rx_head)
        {
            uint16_t tail = s_rx_tail;
            uint8_t byte = s_rx_ring[tail];
            s_rx_tail = (uint16_t)((tail + 1U) & (RS485_RX_BUF_SIZE - 1U));
            rs485_frame_decoder_push(&s_dec, byte);
            drain_frames(now);
            received = true;
        }
        if (received)
        {
            s_last_rx = now;
            s_rx_seen = true;
        }
        else if (rs485_frame_decoder_pending(&s_dec) && elapsed_ms(now, s_last_rx) >= RS485_LINK_FRAME_GAP_MS)
        {
            rs485_frame_decoder_idle(&s_dec);
            drain_frames(now);
        }

        check_timeout(now);
        try_poll(now);
    }
}

/* --- Public API --- */

bool rs485_link_init(const uart_interface_t *uart)
{
    if (uart == NULL || uart->start_rx == NULL || uart->attach_rx_callback == NULL || uart->transmit == NULL ||
        s_task != NULL)
    {
        return false;
    }

    s_uart = uart;
    rs485_frame_decoder_init(&s_dec);

    if (xTaskCreate(rs485_link_task, "rs485", TASK_STACK_RS485, NULL, TASK_PRIO_RS485, &s_task) != pdPASS)
    {
        s_task = NULL;
        return false;
    }

    s_uart->attach_rx_callback(rs485_link_on_rx_byte, NULL);

    safe_printf(NULL, RS485_TAG "link ready (superStatus co %u ms, src 0x%02X), wpisz RS485_HELP\r\n",
                (unsigned)RS485_LINK_POLL_PERIOD_MS, (unsigned)RS485_LINK_SRC_ADDR);
    return true;
}

void rs485_link_set_parser_change_cb(rs485_link_parser_change_fn cb)
{
    s_change_cb = cb;
}

bool rs485_link_get_status(superstatus_t *status, uint32_t *age_ms)
{
    bool valid;
    TickType_t tick;

    taskENTER_CRITICAL();
    valid = s_status_valid;
    tick = s_status_tick;
    if (valid)
    {
        *status = s_status;
    }
    taskEXIT_CRITICAL();

    if (valid && age_ms != NULL)
    {
        *age_ms = elapsed_ms(xTaskGetTickCount(), tick);
    }
    return valid;
}

/* --- Console commands --- */

static bool str_ieq(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++)
    {
        char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 'a' + 'A') : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 'a' + 'A') : *b;
        if (ca != cb)
        {
            return false;
        }
    }
    return (*a == '\0' && *b == '\0');
}

static const char *link_state_name(link_state_t state)
{
    switch (state)
    {
        case LINK_STATE_OK:             return "OK";
        case LINK_STATE_LOST:           return "BRAK ODPOWIEDZI";
        case LINK_STATE_WRONG_PASSWORD: return "BŁĘDNE HASŁO";
        default:                        return "BRAK DANYCH";
    }
}

static void cmd_status(rs485_link_out_fn out)
{
    char line[RS485_OUT_LINE_SIZE];
    const TickType_t now = xTaskGetTickCount();
    const hold_reason_t hold = s_hold;
    const TickType_t hold_until = s_hold_until;
    const bool held = (hold != HOLD_NONE) && !tick_reached(now, hold_until);

    snprintf(line, sizeof(line), RS485_TAG "link: %s, tryb: %s", link_state_name(s_link_state),
             !held ? "aktywny" : (hold == HOLD_TRANSFER ? "wstrzymany (wgrywanie pliku)" : "pasywny (inny master)"));
    out(line);
    snprintf(line, sizeof(line),
             RS485_TAG "zapytania=%lu odpowiedzi=%lu timeouty=%lu echo=%lu obce=%lu | ramki=%lu CRC=%lu pominięte=%lu "
             "UART=%lu overflow=%lu",
             (unsigned long)s_cnt_tx, (unsigned long)s_cnt_resp, (unsigned long)s_cnt_timeout,
             (unsigned long)s_cnt_echo, (unsigned long)s_cnt_foreign, (unsigned long)s_dec.frames_ok,
             (unsigned long)s_dec.crc_errors, (unsigned long)s_dec.skipped_bytes,
             (unsigned long)((s_uart != NULL && s_uart->get_rx_errors != NULL) ? s_uart->get_rx_errors() : 0U),
             (unsigned long)s_rx_overflow);
    out(line);

    superstatus_t ss;
    uint32_t age_ms = 0U;
    if (!rs485_link_get_status(&ss, &age_ms))
    {
        out(RS485_TAG "superStatus: brak");
        return;
    }

    char time_text[SUPERSTATUS_TIME_TEXT_SIZE];
    superstatus_format_time(ss.time_date, time_text, sizeof(time_text));
    snprintf(line, sizeof(line), RS485_TAG "superStatus sprzed %lu ms: fw=%u czas=%s UTC", (unsigned long)age_ms,
             (unsigned)ss.fw_version, time_text);
    out(line);
    /* main_power: probably 10 mV units (1290 = 12.90 V), the raw value is printed as well */
    const int32_t power = ss.main_power;
    const uint32_t power_abs = (uint32_t)((power < 0) ? -power : power);
    snprintf(line, sizeof(line),
             RS485_TAG "zasilanie=%ld (~%s%lu.%02lu V) temp=%dC wej=0x%02X wyj=0x%02X GPS sat=%u hdop=%u GSM=0x%02X op=%s",
             (long)power, (power < 0) ? "-" : "", (unsigned long)(power_abs / 100U), (unsigned long)(power_abs % 100U),
             (int)ss.batt_temp, (unsigned)ss.bin_inputs, (unsigned)ss.bin_outputs, (unsigned)ss.gps_satellites,
             (unsigned)ss.hdop, (unsigned)ss.gsm_status, ss.gsm_operator);
    out(line);
    snprintf(line, sizeof(line), RS485_TAG "IMEI=%s SIM=%s modem=%u.%u.%u", ss.imei, ss.sim_id,
             (unsigned)ss.modem_ver[0], (unsigned)ss.modem_ver[1], (unsigned)ss.modem_ver[2]);
    out(line);
    snprintf(line, sizeof(line), RS485_TAG "parser urządzenia: CAN1=%u CAN2=%u", (unsigned)ss.can1_dev_parser_no,
             (unsigned)ss.can2_dev_parser_no);
    out(line);
}

bool rs485_link_cmd_process(const char *line, rs485_link_out_fn out)
{
    if (str_ieq(line, "RS485?"))
    {
        cmd_status(out);
    }
    else if (str_ieq(line, "RS485#POLL"))
    {
        if (s_task == NULL)
        {
            out(RS485_TAG "ERR: link RS485 nie działa");
            return true;
        }
        s_poll_request = true;
        xTaskNotifyGive(s_task);
        out(RS485_TAG "OK zapytanie superStatus (poza wgrywaniem pliku)");
    }
    else if (str_ieq(line, "RS485_HELP"))
    {
        out(RS485_TAG "RS485?       stan łącza, liczniki i ostatni superStatus urządzenia");
        out(RS485_TAG "RS485#POLL   natychmiastowe zapytanie superStatus");
    }
    else
    {
        return false;
    }
    return true;
}
