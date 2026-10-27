/**
 * @file can_uart_bridge.c
 * @brief UART to CAN bridge: Serial Commander text frames (cansend / CAN FD / TLV) sent on CAN1 or CAN2.
 *
 * Data path: UART ISR -> lock-free single producer / single consumer ring buffer -> task notification
 * -> line assembly -> control command or can_frame_text_parse() -> can_manager_send().
 * A ring buffer is used instead of a FreeRTOS stream buffer because configUSE_STREAM_BUFFERS = 0
 * in the generated FreeRTOSConfig.h.
 */

#include "can_uart_bridge.h"
#include "can_frame_text.h"
#include "io_cmd.h"
#include "car_sim_cmd.h"
#if defined(RS485_LINK) && (RS485_LINK == 1)
#include "rs485_link.h"
#endif
#include "safe_printf.h"
#include "tasks_config.h"
#include "ota_protocol.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CAN_UART_TAG               "[CAN_UART] "
#define CAN_UART_RX_BUF_SIZE       512U   /**< ISR -> task ring buffer, power of 2 */
#define CAN_UART_LINE_MAX          255U   /**< Maximum line length without terminator */
#define CAN_UART_OUT_BUF_SIZE      1024U  /**< Response (command echo + frame with TLV suffix) */
#define CAN_UART_ERR_BUF_SIZE      384U   /**< Parser error text (may quote the whole line) */
#define CAN_UART_CFG_TX_IDLE_MS    200U   /**< Wait for queued frames before a bus reconfiguration */
#define CAN_UART_NUM_BUSES         2U

_Static_assert((CAN_UART_RX_BUF_SIZE & (CAN_UART_RX_BUF_SIZE - 1U)) == 0U, "RX buffer size must be a power of 2");

/** @brief OTA identifiers the bridge refuses to send (discovery broadcast, ISO-TP request, discovery response). */
static const uint32_t s_reserved_ids[] = { CAN_ID_OTA_BROADCAST, CAN_ID_ISOTP_REQ, CAN_ID_OTA_DISCOVERY_RESP };

static const uart_interface_t *s_uart = NULL;
static can_instance_t *s_can[CAN_UART_NUM_BUSES] = { NULL, NULL };
static TaskHandle_t s_task = NULL;

/** @brief Bus used for frames without CAN1:/CAN2: prefix (1 or 2). */
static uint8_t s_default_bus = 1U;
/** @brief Forced frame standard (MODE#...). */
static cft_mode_t s_frame_mode = CFT_MODE_AUTO;

/** @brief ISR -> task ring buffer: head written by the ISR only, tail by the task only. */
static volatile uint8_t s_rx_ring[CAN_UART_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;
static volatile uint32_t s_rx_overflow = 0U;
static uint32_t s_rx_overflow_reported = 0U;

/** @brief Line assembly state (task context only). */
static char s_line[CAN_UART_LINE_MAX + 1U];
static size_t s_line_len = 0U;
static bool s_line_too_long = false;
static bool s_line_bad_char = false;
static uint8_t s_line_bad_value = 0U;

/** @brief Response and parser buffers (task context only, kept off the task stack). */
static char s_out[CAN_UART_OUT_BUF_SIZE];
static size_t s_out_len = 0U;
static char s_err[CAN_UART_ERR_BUF_SIZE];
static char s_frame_text[CFT_FORMAT_BUF_SIZE];

/* --- Output helpers --- */

static void out_append(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void out_append(const char *fmt, ...)
{
    if (s_out_len >= sizeof(s_out))
    {
        return;
    }
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(&s_out[s_out_len], sizeof(s_out) - s_out_len, fmt, args);
    va_end(args);
    if (n > 0)
    {
        s_out_len += (size_t)n;
        if (s_out_len >= sizeof(s_out))
        {
            s_out_len = sizeof(s_out) - 1U;
        }
    }
}

/** @brief Appends one tagged response line (CRLF added). */
static void out_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void out_line(const char *fmt, ...)
{
    out_append("%s", CAN_UART_TAG);
    if (s_out_len < sizeof(s_out))
    {
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(&s_out[s_out_len], sizeof(s_out) - s_out_len, fmt, args);
        va_end(args);
        if (n > 0)
        {
            s_out_len += (size_t)n;
            if (s_out_len >= sizeof(s_out))
            {
                s_out_len = sizeof(s_out) - 1U;
            }
        }
    }
    out_append("\r\n");
}

/** @brief Sends the collected response in one piece (not interleaved with other logs). */
static void out_flush(void)
{
    if (s_out_len > 0U)
    {
        safe_write(s_out, s_out_len);
    }
    s_out_len = 0U;
}

/**
 * @brief Output for io_cmd_process(): one tagged line from the input simulation module.
 *        Long responses (IO_HELP table) exceed s_out, so the collected part is sent first.
 */
static void out_io_line(const char *text)
{
    if (s_out_len + strlen(text) + 2U >= sizeof(s_out))
    {
        out_flush();
    }
    out_append("%s\r\n", text);
}

/* --- Text helpers --- */

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
    return (*a == '\0') && (*b == '\0');
}

static bool str_istarts(const char *s, const char *prefix)
{
    for (; *prefix != '\0'; s++, prefix++)
    {
        char cs = (*s >= 'a' && *s <= 'z') ? (char)(*s - 'a' + 'A') : *s;
        char cp = (*prefix >= 'a' && *prefix <= 'z') ? (char)(*prefix - 'a' + 'A') : *prefix;
        if (cs != cp)
        {
            return false;
        }
    }
    return true;
}

/** @brief Strips leading and trailing spaces in place, returns the new start. */
static char *str_trim(char *s)
{
    while (*s == ' ')
    {
        s++;
    }
    size_t len = strlen(s);
    while (len > 0U && s[len - 1U] == ' ')
    {
        s[--len] = '\0';
    }
    return s;
}

/**
 * @brief Formats a bit rate like Serial Commander: 500000 -> "500k", 2000000 -> "2M", 83300 -> "83.3k".
 */
static const char *bitrate_label(uint32_t bps, char *buf, size_t size)
{
    if (bps == 0U)
    {
        (void)snprintf(buf, size, "CubeMX");
    }
    else if (bps >= 1000000U && (bps % 1000000U) == 0U)
    {
        (void)snprintf(buf, size, "%luM", (unsigned long)(bps / 1000000U));
    }
    else if ((bps % 1000U) == 0U)
    {
        (void)snprintf(buf, size, "%luk", (unsigned long)(bps / 1000U));
    }
    else
    {
        unsigned long tenths = ((unsigned long)bps + 50UL) / 100UL;
        (void)snprintf(buf, size, "%lu.%luk", tenths / 10UL, tenths % 10UL);
    }
    return buf;
}

/** @brief Bus configuration label, e.g. "FD+BRS 500k/2M", "FD 1M", "Classic 250k". */
static const char *config_label(const can_config_t *cfg, char *buf, size_t size)
{
    char nominal[16];
    char data[16];
    (void)bitrate_label((uint32_t)cfg->baudrate_normal, nominal, sizeof(nominal));

    switch (cfg->mode)
    {
        case CAN_MODE_FD_BRS:
            (void)snprintf(buf, size, "FD+BRS %s/%s", nominal,
                           bitrate_label((uint32_t)cfg->baudrate_FD, data, sizeof(data)));
            break;
        case CAN_MODE_FD_NO_BRS:
            (void)snprintf(buf, size, "FD %s", nominal);
            break;
        case CAN_MODE_CLASSIC:
        default:
            (void)snprintf(buf, size, "Classic %s", nominal);
            break;
    }
    return buf;
}

/**
 * @brief Parses a bit rate: "500000", "500k", "2M", "83.3k" (k/K = 1e3, M/m = 1e6).
 * @return true if the result is a positive integer number of bit/s.
 */
static bool parse_bitrate(const char *s, uint32_t *bps)
{
    const uint32_t max_bps = 10000000U;
    uint32_t integer = 0U;
    uint32_t frac = 0U;
    uint32_t frac_div = 1U;
    size_t digits = 0U;
    bool in_frac = false;

    for (; *s != '\0'; s++)
    {
        if (*s >= '0' && *s <= '9')
        {
            digits++;
            if (in_frac)
            {
                if (frac_div >= 1000000U)
                {
                    return false;
                }
                frac = frac * 10U + (uint32_t)(*s - '0');
                frac_div *= 10U;
            }
            else
            {
                if (integer > max_bps)
                {
                    return false;
                }
                integer = integer * 10U + (uint32_t)(*s - '0');
            }
        }
        else if (*s == '.' && !in_frac)
        {
            in_frac = true;
        }
        else
        {
            break;
        }
    }

    uint32_t mult = 1U;
    if (*s == 'k' || *s == 'K')
    {
        mult = 1000U;
        s++;
    }
    else if (*s == 'M' || *s == 'm')
    {
        mult = 1000000U;
        s++;
    }

    if (*s != '\0' || digits == 0U)
    {
        return false;
    }

    /* The fraction must resolve to whole bit/s: "83.3k" yes, "1.5" no */
    if ((mult % frac_div) != 0U || integer > (max_bps / mult))
    {
        return false;
    }
    uint32_t value = integer * mult + frac * (mult / frac_div);
    if (value == 0U || value > max_bps)
    {
        return false;
    }
    *bps = value;
    return true;
}

/** @brief Returns the instance of bus 1 or 2, or prints "niedostępny" and returns NULL. */
static can_instance_t *bus_instance(uint8_t bus)
{
    if (bus < 1U || bus > CAN_UART_NUM_BUSES || s_can[bus - 1U] == NULL)
    {
        out_line("ERR: CAN%u niedostępny", (unsigned)bus);
        return NULL;
    }
    return s_can[bus - 1U];
}

/* --- Commands --- */

static void cmd_help(void)
{
    out_line("Ramki (składnia Serial Commandera, opcjonalny prefiks CAN1: / CAN2:):");
    out_line("  187#01 02 03        Classic (> 8 B: FD+BRS)");
    out_line("  321##1#01 02 03     CAN FD, BRS = 1 (##0# bez BRS)");
    out_line("  65A#R4              ramka zdalna (RTR) z DLC");
    out_line("  TLV#321#0xA5#Hello  TLV (hex:01 02 dla danych binarnych)");
    out_line("Polecenia:");
    out_line("  CFG#<1|2>#<CLASSIC|FD|FD_BRS>#<nominal>[#<data>]  np. CFG#1#FD_BRS#500k#2M");
    out_line("  CFG?                konfiguracja i liczniki magistral");
    out_line("  BUS#<1|2>           magistrala domyślna");
    out_line("  MODE#<auto|classic|fd_brs|fd_no_brs|tlv>  wymuszony standard ramki");
    out_line("  SIM_HELP            symulator auta (SIM?, SIM_LIST, SIM#, SIM_PIN#, PILOT#)");
    out_line("  IO_HELP             symulacja wejść (stacyjka, drzwi, obroty, paliwo...)");
#if defined(RS485_LINK) && (RS485_LINK == 1)
    out_line("  RS485_HELP          łącze RS485 z urządzeniem (superStatus, parser urządzenia)");
#endif
}

static void cmd_cfg_query(void)
{
    char label[40];

    for (uint8_t bus = 1U; bus <= CAN_UART_NUM_BUSES; bus++)
    {
        const can_instance_t *inst = s_can[bus - 1U];
        if (inst == NULL)
        {
            out_line("CAN%u: niedostępny", (unsigned)bus);
            continue;
        }

        can_bus_status_t st;
        if (can_manager_getStatus(inst, &st))
        {
            out_line("CAN%u: %s, TEC=%u REC=%u%s%s, tx_error_cnt=%lu, bus_off_cnt=%lu", (unsigned)bus,
                     config_label(&inst->can_config, label, sizeof(label)),
                     (unsigned)st.tx_error_cnt, (unsigned)st.rx_error_cnt,
                     st.error_passive ? " ERROR_PASSIVE" : "", st.bus_off ? " BUS_OFF" : "",
                     (unsigned long)inst->tx_error_cnt, (unsigned long)inst->bus_off_cnt);
        }
        else
        {
            out_line("CAN%u: %s, tx_error_cnt=%lu, bus_off_cnt=%lu", (unsigned)bus,
                     config_label(&inst->can_config, label, sizeof(label)),
                     (unsigned long)inst->tx_error_cnt, (unsigned long)inst->bus_off_cnt);
        }
    }

    uint32_t uart_errors = (s_uart->get_rx_errors != NULL) ? s_uart->get_rx_errors() : 0U;
    out_line("BUS: CAN%u, MODE: %s, rx_overflow=%lu, uart_rx_errors=%lu", (unsigned)s_default_bus,
             can_frame_text_mode_name(s_frame_mode), (unsigned long)s_rx_overflow, (unsigned long)uart_errors);
}

/**
 * @brief CFG#<bus>#<CLASSIC|FD|FD_BRS>#<nominal>[#<data>]
 * @param[in,out] args Text after "CFG#" (modified by tokenizing).
 */
static void cmd_cfg_set(char *args)
{
    static const char *const usage = "ERR: składnia: CFG#<1|2>#<CLASSIC|FD|FD_BRS>#<nominal>[#<data>], np. CFG#1#FD_BRS#500k#2M";
    char *field[4] = { NULL, NULL, NULL, NULL };
    size_t count = 0U;

    /* Split on '#': <bus>#<mode>#<nominal>[#<data>] */
    for (char *p = args; ; )
    {
        char *sep = strchr(p, '#');
        if (sep != NULL)
        {
            *sep = '\0';
        }
        if (count < 4U)
        {
            field[count] = str_trim(p);
        }
        count++;
        if (sep == NULL)
        {
            break;
        }
        p = sep + 1;
    }
    if (count < 3U || count > 4U)
    {
        out_line("%s", usage);
        return;
    }

    uint8_t bus;
    if (strcmp(field[0], "1") == 0)
    {
        bus = 1U;
    }
    else if (strcmp(field[0], "2") == 0)
    {
        bus = 2U;
    }
    else
    {
        out_line("ERR: nieprawidłowa magistrala '%s' (1 lub 2)", field[0]);
        return;
    }

    can_config_t cfg = { .mode = CAN_MODE_CLASSIC, .baudrate_normal = CAN_BAUDRATE_CUSTOM, .baudrate_FD = CAN_BAUDRATE_CUSTOM };
    if (str_ieq(field[1], "CLASSIC"))
    {
        cfg.mode = CAN_MODE_CLASSIC;
    }
    else if (str_ieq(field[1], "FD"))
    {
        cfg.mode = CAN_MODE_FD_NO_BRS;
    }
    else if (str_ieq(field[1], "FD_BRS"))
    {
        cfg.mode = CAN_MODE_FD_BRS;
    }
    else
    {
        out_line("ERR: nieprawidłowy tryb '%s' (CLASSIC, FD, FD_BRS)", field[1]);
        return;
    }

    uint32_t bps;
    if (!parse_bitrate(field[2], &bps))
    {
        out_line("ERR: nieprawidłowa prędkość nominalna '%s' (np. 500000, 500k, 1M, 83.3k)", field[2]);
        return;
    }
    cfg.baudrate_normal = (can_baudrate_t)bps;

    if (cfg.mode == CAN_MODE_FD_BRS)
    {
        if (count < 4U || field[3][0] == '\0')
        {
            out_line("ERR: tryb FD_BRS wymaga prędkości danych, np. CFG#%u#FD_BRS#500k#2M", (unsigned)bus);
            return;
        }
        if (!parse_bitrate(field[3], &bps))
        {
            out_line("ERR: nieprawidłowa prędkość danych '%s' (np. 2M, 4M, 2000000)", field[3]);
            return;
        }
        cfg.baudrate_FD = (can_baudrate_t)bps;
    }

    can_instance_t *inst = bus_instance(bus);
    if (inst == NULL)
    {
        return;
    }

    char label[40];
    char old_label[40];
    (void)config_label(&inst->can_config, old_label, sizeof(old_label));

    /* Frames already queued were meant for the previous configuration */
    (void)can_manager_waitTxIdle(inst, CAN_UART_CFG_TX_IDLE_MS);
    if (!can_manager_setBusConfig(inst, &cfg))
    {
        out_line("ERR: konfiguracja nieosiągalna, pozostaje %s", old_label);
        return;
    }

    out_line("CAN%u: %s OK", (unsigned)bus, config_label(&inst->can_config, label, sizeof(label)));
    /* D4: the parser profile keeps its filters, its bus configuration returns on the next can_parser_select() */
    out_line("WARN: konfiguracja CAN%u nadpisana z UART (profil parsera przywróci swoją przy następnym wyborze profilu)",
             (unsigned)bus);
}

static void cmd_bus(const char *arg)
{
    if (strcmp(arg, "1") != 0 && strcmp(arg, "2") != 0)
    {
        out_line("ERR: nieprawidłowa magistrala '%s' (1 lub 2)", arg);
        return;
    }
    uint8_t bus = (uint8_t)(arg[0] - '0');
    if (bus_instance(bus) == NULL)
    {
        return;
    }
    s_default_bus = bus;
    out_line("BUS: CAN%u", (unsigned)bus);
}

static void cmd_mode(const char *arg)
{
    cft_mode_t mode;
    if (!can_frame_text_mode_parse(arg, &mode))
    {
        out_line("ERR: nieznany tryb '%s' (auto, classic, fd_brs, fd_no_brs, tlv)", arg);
        return;
    }
    s_frame_mode = mode;
    out_line("MODE: %s", can_frame_text_mode_name(mode));
}

static bool is_reserved_id(uint32_t ident)
{
    if (!CAN_msg_is_extended_id(ident))
    {
        return false;
    }
    for (size_t i = 0U; i < sizeof(s_reserved_ids) / sizeof(s_reserved_ids[0]); i++)
    {
        if (CAN_msg_get_can_id(ident) == s_reserved_ids[i])
        {
            return true;
        }
    }
    return false;
}

/** @brief Parses a frame line and queues it on the selected bus. */
static void cmd_frame(const char *line)
{
    cft_frame_t frame;
    if (!can_frame_text_parse(line, s_frame_mode, &frame, s_err, sizeof(s_err)))
    {
        out_line("ERR: %s", s_err);
        return;
    }

    const uint8_t bus = (frame.bus != 0U) ? frame.bus : s_default_bus;
    can_instance_t *inst = bus_instance(bus);
    if (inst == NULL)
    {
        return;
    }

    CAN_msg_t *msg = &frame.msg;
    if (is_reserved_id(msg->ident))
    {
        out_line("ERR: ID zarezerwowane dla OTA");
        return;
    }

    const bool is_fd = (msg->format == CAN_MSG_FRAME_FORMAT_FD_CAN);
    if (is_fd && inst->can_config.mode == CAN_MODE_CLASSIC)
    {
        out_line("ERR: CAN%u pracuje w trybie Classic - ramka FD odrzucona (CFG#%u#FD_BRS#500k#2M)",
                 (unsigned)bus, (unsigned)bus);
        return;
    }
    if (is_fd && (msg->flags & CAN_MSG_FLAG_BRS) != 0U && inst->can_config.mode == CAN_MODE_FD_NO_BRS)
    {
        /* The bus has no data bit rate configured: the adapter sends the frame without BRS */
        msg->flags &= (uint8_t)~CAN_MSG_FLAG_BRS;
        out_line("WARN: CAN%u pracuje w trybie FD bez BRS - ramka wysłana bez BRS", (unsigned)bus);
    }

    if (!can_manager_send(inst, msg))
    {
        out_line("ERR: kolejka TX CAN%u pełna", (unsigned)bus);
        return;
    }

    (void)can_frame_text_format(msg, s_frame_text, sizeof(s_frame_text));
    out_line("Polecenie: %s", line);
    out_line("Ramka: %s", s_frame_text);
    out_line("Rozmiar: %uB (FD: %s, BRS: %s) -> CAN%u OK", (unsigned)msg->len,
             is_fd ? "True" : "False", ((msg->flags & CAN_MSG_FLAG_BRS) != 0U) ? "True" : "False",
             (unsigned)bus);
}

/** @brief Executes one complete, non-empty line. */
static void process_line(char *line)
{
    line = str_trim(line);
    if (line[0] == '\0')
    {
        return;
    }

    if (str_ieq(line, "HELP"))
    {
        cmd_help();
    }
    else if (str_ieq(line, "CFG?"))
    {
        cmd_cfg_query();
    }
    else if (str_istarts(line, "CFG#"))
    {
        cmd_cfg_set(line + 4);
    }
    else if (str_istarts(line, "BUS#"))
    {
        cmd_bus(str_trim(line + 4));
    }
    else if (str_ieq(line, "BUS?"))
    {
        out_line("BUS: CAN%u", (unsigned)s_default_bus);
    }
    else if (str_istarts(line, "MODE#"))
    {
        cmd_mode(str_trim(line + 5));
    }
    else if (str_ieq(line, "MODE?"))
    {
        out_line("MODE: %s", can_frame_text_mode_name(s_frame_mode));
    }
    else if (io_cmd_process(line, out_io_line))
    {
        /* Input simulation command (io_cmd.c), response already collected */
    }
    else if (car_sim_cmd_process(line, out_io_line))
    {
        /* Car simulator command (car_sim_cmd.c), response already collected */
    }
#if defined(RS485_LINK) && (RS485_LINK == 1)
    else if (rs485_link_cmd_process(line, out_io_line))
    {
        /* RS485 link command (rs485_link.c), response already collected */
    }
#endif
    else
    {
        cmd_frame(line);
    }
}

/* --- Line assembly --- */

static void line_reset(void)
{
    s_line_len = 0U;
    s_line_too_long = false;
    s_line_bad_char = false;
}

/** @brief Feeds one received byte into the line assembler; executes the line on CR / LF. */
static void line_feed(uint8_t byte)
{
    if (byte == (uint8_t)'\r' || byte == (uint8_t)'\n')
    {
        uint32_t overflow = s_rx_overflow;
        if (overflow != s_rx_overflow_reported)
        {
            out_line("WARN: przepełnienie bufora RX UART, utracono %lu B", (unsigned long)(overflow - s_rx_overflow_reported));
            s_rx_overflow_reported = overflow;
        }

        if (s_line_too_long)
        {
            out_line("ERR: linia za długa (maks. %u znaków)", CAN_UART_LINE_MAX);
        }
        else if (s_line_bad_char)
        {
            out_line("ERR: niedozwolony znak 0x%02X", (unsigned)s_line_bad_value);
        }
        else if (s_line_len > 0U)
        {
            s_line[s_line_len] = '\0';
            process_line(s_line);
        }
        /* An empty line (second character of CRLF) is skipped silently */

        out_flush();
        line_reset();
        return;
    }

    if (s_line_too_long)
    {
        return; /* discard until the next terminator */
    }
    if (byte < 0x20U || byte >= 0x7FU)
    {
        if (!s_line_bad_char)
        {
            s_line_bad_char = true;
            s_line_bad_value = byte;
        }
        return;
    }
    if (s_line_len >= CAN_UART_LINE_MAX)
    {
        s_line_too_long = true;
        return;
    }
    s_line[s_line_len++] = (char)byte;
}

/**
 * @brief UART byte callback (ISR context): stores the byte and wakes the bridge task.
 */
static void can_uart_bridge_on_rx_byte(uint8_t byte, void *context)
{
    (void)context;

    uint16_t head = s_rx_head;
    uint16_t next = (uint16_t)((head + 1U) & (CAN_UART_RX_BUF_SIZE - 1U));
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

static void can_uart_bridge_task(void *pvParameters)
{
    (void)pvParameters;

    /* Reception is armed from the task, so the ISR never notifies before the scheduler runs */
    if (!s_uart->start_rx())
    {
        safe_printf(NULL, CAN_UART_TAG "ERROR: UART reception start failed\r\n");
        vTaskDelete(NULL);
        return;
    }

    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (s_rx_tail != s_rx_head)
        {
            uint16_t tail = s_rx_tail;
            uint8_t byte = s_rx_ring[tail];
            s_rx_tail = (uint16_t)((tail + 1U) & (CAN_UART_RX_BUF_SIZE - 1U));
            line_feed(byte);
        }
    }
}

bool can_uart_bridge_init(const uart_interface_t *uart, can_instance_t *can1, can_instance_t *can2)
{
    if (uart == NULL || uart->start_rx == NULL || uart->attach_rx_callback == NULL || can1 == NULL || s_task != NULL)
    {
        return false;
    }

    s_uart = uart;
    s_can[0] = can1;
    s_can[1] = can2;
    line_reset();

    if (xTaskCreate(can_uart_bridge_task, "can_uart", TASK_STACK_CAN_UART, NULL, TASK_PRIO_CAN_UART, &s_task) != pdPASS)
    {
        s_task = NULL;
        return false;
    }

    s_uart->attach_rx_callback(can_uart_bridge_on_rx_byte, NULL);

    safe_printf(NULL, CAN_UART_TAG "bridge ready (%s), wpisz HELP\r\n", (can2 != NULL) ? "CAN1, CAN2" : "CAN1");
    return true;
}
