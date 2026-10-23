/**
 * @file can_frame_text.c
 * @brief Text frame parser and formatter compatible with Serial Commander (can_support.py).
 *
 * The parsing order and error texts follow can_support.parse_can_frame() step by step:
 * bus prefix -> TLV# -> missing '#' -> '##' + BRS flag -> ID -> RTR -> data -> forced mode -> auto.
 */

#include "can_frame_text.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CFT_MAX_STD_ID      0x7FFU
#define CFT_MAX_EXT_ID      0x1FFFFFFFU
#define CFT_MAX_CLASSIC_DLC 8U
#define CFT_MAX_FD_DLC      64U
#define CFT_TLV_HEADER_SIZE 3U

/** @brief Hex digits that fit a line of 255 characters (decoded before the length check). */
#define CFT_MAX_DIGITS      256U

/** @brief Text span (not NUL terminated). */
typedef struct {
    const char *p;
    size_t n;
} cft_span_t;

static const char *const s_mode_names[] = { "auto", "classic", "fd_brs", "fd_no_brs", "tlv" };

/* --- Character helpers (ASCII, matches Python str.strip() / regex [\s.:_\-] for printable input) --- */

static bool cft_is_space(char c)
{
    return (c == ' ') || (c == '\t') || (c == '\n') || (c == '\r') || (c == '\v') || (c == '\f');
}

static bool cft_is_data_sep(char c)
{
    return cft_is_space(c) || (c == '.') || (c == ':') || (c == '_') || (c == '-');
}

static int cft_hex_val(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
}

static char cft_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static cft_span_t cft_strip(cft_span_t s)
{
    while (s.n > 0U && cft_is_space(s.p[0])) { s.p++; s.n--; }
    while (s.n > 0U && cft_is_space(s.p[s.n - 1U])) { s.n--; }
    return s;
}

/** @brief Case insensitive prefix test. */
static bool cft_starts_with_ci(cft_span_t s, const char *prefix)
{
    size_t len = strlen(prefix);
    if (s.n < len)
    {
        return false;
    }
    for (size_t i = 0U; i < len; i++)
    {
        if (cft_lower(s.p[i]) != cft_lower(prefix[i]))
        {
            return false;
        }
    }
    return true;
}

/** @brief Returns the offset of the first occurrence of pattern in s, or s.n if not found. */
static size_t cft_find(cft_span_t s, const char *pattern)
{
    size_t len = strlen(pattern);
    for (size_t i = 0U; i + len <= s.n; i++)
    {
        if (memcmp(&s.p[i], pattern, len) == 0)
        {
            return i;
        }
    }
    return s.n;
}

static void cft_set_error(char *err, size_t err_size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void cft_set_error(char *err, size_t err_size, const char *fmt, ...)
{
    if (err == NULL || err_size == 0U)
    {
        return;
    }
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(err, err_size, fmt, args);
    va_end(args);
}

/* --- Field parsers (can_support.py equivalents) --- */

/**
 * @brief _parse_can_id(): optional 0x, hex only, max 8 digits; extended for 8 digits or value > 0x7FF.
 */
static bool cft_parse_id(cft_span_t id_part, uint32_t *ident, char *err, size_t err_size)
{
    cft_span_t orig = cft_strip(id_part);
    cft_span_t raw = orig;

    if (cft_starts_with_ci(raw, "0x"))
    {
        raw.p += 2;
        raw.n -= 2U;
    }
    if (raw.n == 0U)
    {
        cft_set_error(err, err_size, "brak identyfikatora ID przed znakiem '#'");
        return false;
    }
    for (size_t i = 0U; i < raw.n; i++)
    {
        if (cft_hex_val(raw.p[i]) < 0)
        {
            cft_set_error(err, err_size, "ID '%.*s' nie jest liczbą szesnastkową", (int)orig.n, orig.p);
            return false;
        }
    }
    if (raw.n > 8U)
    {
        cft_set_error(err, err_size, "ID '%.*s' jest za długie (maks. 8 cyfr hex)", (int)raw.n, raw.p);
        return false;
    }

    uint32_t value = 0U;
    for (size_t i = 0U; i < raw.n; i++)
    {
        value = (value << 4) | (uint32_t)cft_hex_val(raw.p[i]);
    }

    if (value > CFT_MAX_EXT_ID)
    {
        cft_set_error(err, err_size, "ID 0x%lX poza zakresem 29-bit (maks. 0x1FFFFFFF)", (unsigned long)value);
        return false;
    }

    bool is_ext = (raw.n == 8U) || (value > CFT_MAX_STD_ID);
    *ident = is_ext ? CAN_msg_set_extended_id(value) : CAN_msg_set_standard_id(value);
    return true;
}

/**
 * @brief Python int(text) / int(text, 16) for the TLV type: optional sign (decimal), 0x prefix (hex),
 *        single underscores between digits. Values above 0xFFFF are clamped (rejected later anyway).
 */
static bool cft_parse_py_int(cft_span_t s, bool hex, long *out)
{
    size_t i = 0U;
    bool neg = false;
    long value = 0;
    bool prev_digit = false;
    size_t digits = 0U;

    if (hex)
    {
        i = 2U; /* "0x" checked by the caller */
        if (i < s.n && s.p[i] == '_')
        {
            i++; /* Python accepts one underscore right after the base prefix */
        }
    }
    else if (s.n > 0U && (s.p[0] == '+' || s.p[0] == '-'))
    {
        neg = (s.p[0] == '-');
        i = 1U;
    }

    for (; i < s.n; i++)
    {
        char c = s.p[i];
        int d = hex ? cft_hex_val(c) : ((c >= '0' && c <= '9') ? (c - '0') : -1);
        if (d >= 0)
        {
            value = value * (hex ? 16 : 10) + d;
            if (value > 0xFFFFL)
            {
                value = 0x10000L;
            }
            prev_digit = true;
            digits++;
        }
        else if (c == '_' && prev_digit && (i + 1U) < s.n)
        {
            prev_digit = false;
        }
        else
        {
            return false;
        }
    }

    if (digits == 0U || !prev_digit)
    {
        return false;
    }
    *out = neg ? -value : value;
    return true;
}

/**
 * @brief bytes.fromhex() on separator-free text; error text as in CPython 3.8 (ValueError).
 */
static bool cft_from_hex(const char *hex, size_t n, uint8_t *out, size_t out_size, size_t *out_len,
                         char *err, size_t err_size)
{
    size_t len = 0U;
    for (size_t i = 0U; i < n; i += 2U)
    {
        int hi = cft_hex_val(hex[i]);
        if (hi < 0)
        {
            cft_set_error(err, err_size, "non-hexadecimal number found in fromhex() arg at position %u", (unsigned)i);
            return false;
        }
        int lo = (i + 1U < n) ? cft_hex_val(hex[i + 1U]) : -1;
        if (lo < 0)
        {
            cft_set_error(err, err_size, "non-hexadecimal number found in fromhex() arg at position %u", (unsigned)(i + 1U));
            return false;
        }
        if (len < out_size)
        {
            out[len] = (uint8_t)((hi << 4) | lo);
        }
        len++;
    }
    *out_len = len;
    return true;
}

/** @brief Removes data separators [\s.:_\-] into dst (NUL terminated), returns length. */
static size_t cft_compact(cft_span_t s, char *dst, size_t dst_size)
{
    size_t len = 0U;
    for (size_t i = 0U; i < s.n && (len + 1U) < dst_size; i++)
    {
        if (!cft_is_data_sep(s.p[i]))
        {
            dst[len++] = s.p[i];
        }
    }
    dst[len] = '\0';
    return len;
}

/** @brief Packs ot_app_msg_tlv (<BH) padded with zeros to 64 bytes into a CAN FD frame. */
static bool cft_pack_tlv(uint8_t type, const uint8_t *value, size_t value_len, bool brs,
                         CAN_msg_t *msg, char *err, size_t err_size)
{
    if (value_len > CFT_TLV_MAX_VALUE)
    {
        cft_set_error(err, err_size, "za dużo danych TLV: %u B (maks. %u B)", (unsigned)value_len, CFT_TLV_MAX_VALUE);
        return false;
    }

    memset(msg->data, 0, sizeof(msg->data));
    msg->data[0] = type;
    msg->data[1] = (uint8_t)(value_len & 0xFFU);
    msg->data[2] = (uint8_t)(value_len >> 8);
    if (value_len > 0U)
    {
        memcpy(&msg->data[CFT_TLV_HEADER_SIZE], value, value_len);
    }
    msg->len = CFT_MAX_FD_DLC;
    msg->format = CAN_MSG_FRAME_FORMAT_FD_CAN;
    msg->flags = (uint8_t)(CAN_MSG_FLAG_BRS_SET | (brs ? CAN_MSG_FLAG_BRS : 0U));
    return true;
}

/** @brief Sets frame format and BRS flags (FD frames always carry BRS_SET). */
static void cft_set_format(CAN_msg_t *msg, bool is_fd, bool brs)
{
    msg->format = is_fd ? CAN_MSG_FRAME_FORMAT_FD_CAN : CAN_MSG_FRAME_FORMAT_CAN;
    msg->flags = is_fd ? (uint8_t)(CAN_MSG_FLAG_BRS_SET | (brs ? CAN_MSG_FLAG_BRS : 0U)) : 0U;
}

/**
 * @brief TLV helper syntax: TLV#<ID>#<TYPE>#<PAYLOAD>, PAYLOAD as text or "hex:..." bytes.
 */
static bool cft_parse_tlv(cft_span_t s, cft_mode_t mode, CAN_msg_t *msg, char *err, size_t err_size)
{
    if (mode == CFT_MODE_CLASSIC)
    {
        cft_set_error(err, err_size, "Ramka TLV wymaga standardu CAN FD (64B). W trybie 'Klasyczny CAN 2.0' ramki TLV nie są dozwolone.");
        return false;
    }

    /* s.split("#", 3) -> "TLV", id, type, payload (payload may contain '#') */
    cft_span_t parts[4];
    size_t count = 0U;
    cft_span_t rest = s;
    while (count < 3U)
    {
        size_t pos = cft_find(rest, "#");
        if (pos == rest.n)
        {
            break;
        }
        parts[count].p = rest.p;
        parts[count].n = pos;
        count++;
        rest.p += pos + 1U;
        rest.n -= pos + 1U;
    }
    parts[count] = rest;
    count++;

    if (count < 4U)
    {
        cft_set_error(err, err_size, "niepełna składnia TLV. Wymagane: TLV#<ID>#<TYPE>#<PAYLOAD>, np. TLV#321#0xA5#Hello");
        return false;
    }

    if (!cft_parse_id(parts[1], &msg->ident, err, err_size))
    {
        return false;
    }

    cft_span_t type_str = cft_strip(parts[2]);
    long type = -1;
    bool type_ok = cft_starts_with_ci(type_str, "0x") ? cft_parse_py_int(type_str, true, &type)
                                                       : cft_parse_py_int(type_str, false, &type);
    if (!type_ok || type < 0 || type > 0xFF)
    {
        cft_set_error(err, err_size, "nieprawidłowy typ TLV '%.*s' (wymagany bajt 0-255 lub 0x00-0xFF)",
                      (int)type_str.n, type_str.p);
        return false;
    }

    cft_span_t payload = cft_strip(parts[3]);
    const bool brs = (mode != CFT_MODE_FD_NO_BRS);

    if (cft_starts_with_ci(payload, "hex:"))
    {
        cft_span_t hex_part = { payload.p + 4, payload.n - 4U };
        char compact[CFT_MAX_DIGITS];
        size_t compact_len = cft_compact(hex_part, compact, sizeof(compact));
        uint8_t value[CFT_MAX_DIGITS / 2U];
        size_t value_len = 0U;
        if (!cft_from_hex(compact, compact_len, value, sizeof(value), &value_len, err, err_size))
        {
            return false;
        }
        return cft_pack_tlv((uint8_t)type, value, value_len, brs, msg, err, err_size);
    }

    return cft_pack_tlv((uint8_t)type, (const uint8_t *)payload.p, payload.n, brs, msg, err, err_size);
}

/* --- Public API --- */

bool can_frame_text_parse(const char *line, cft_mode_t mode, cft_frame_t *out,
                          char *err, size_t err_size)
{
    if (out == NULL)
    {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (err != NULL && err_size > 0U)
    {
        err[0] = '\0';
    }

    cft_span_t s = { line, (line != NULL) ? strlen(line) : 0U };
    s = cft_strip(s);

    /* Bus prefix (bridge extension, not part of can_support.py) */
    if (cft_starts_with_ci(s, "CAN1:") || cft_starts_with_ci(s, "CAN2:"))
    {
        out->bus = (uint8_t)(s.p[3] - '0');
        s.p += 5;
        s.n -= 5U;
        s = cft_strip(s);
    }

    if (s.n == 0U)
    {
        cft_set_error(err, err_size, "pusta ramka");
        return false;
    }

    CAN_msg_t *msg = &out->msg;

    /* 1. TLV#<ID>#<TYPE>#<PAYLOAD> */
    if (cft_starts_with_ci(s, "TLV#"))
    {
        return cft_parse_tlv(s, mode, msg, err, err_size);
    }

    if (cft_find(s, "#") == s.n)
    {
        cft_set_error(err, err_size, "brak separatora '#'. Format: ID#DANE, np. 187#01 02 03 lub TLV#321#0xA5#Dane");
        return false;
    }

    /* 2. ID##<FLAG>#<DATA> or ID##<FLAG><DATA> */
    bool is_fd_explicit = false;
    bool brs_flag = false;
    cft_span_t id_part;
    cft_span_t data_part;
    size_t pos = cft_find(s, "##");

    if (pos < s.n)
    {
        if (mode == CFT_MODE_CLASSIC)
        {
            cft_set_error(err, err_size, "W trybie 'Klasyczny CAN 2.0' nie można używać składni CAN FD '##'. Zmień '##' na '#' lub zmień standard ramki na CAN FD.");
            return false;
        }
        is_fd_explicit = true;
        id_part.p = s.p;
        id_part.n = pos;
        data_part.p = s.p + pos + 2U;
        data_part.n = s.n - pos - 2U;
        data_part = cft_strip(data_part);
        if (data_part.n > 0U && (data_part.p[0] == '0' || data_part.p[0] == '1'))
        {
            brs_flag = (data_part.p[0] == '1');
            data_part.p++;
            data_part.n--;
            while (data_part.n > 0U && data_part.p[0] == '#') /* lstrip("#") */
            {
                data_part.p++;
                data_part.n--;
            }
        }
    }
    else
    {
        pos = cft_find(s, "#");
        id_part.p = s.p;
        id_part.n = pos;
        data_part.p = s.p + pos + 1U;
        data_part.n = s.n - pos - 1U;
    }

    if (!cft_parse_id(id_part, &msg->ident, err, err_size))
    {
        return false;
    }
    data_part = cft_strip(data_part);

    /* RTR (Classic only): R or R<DLC> */
    if (data_part.n > 0U && (data_part.p[0] == 'R' || data_part.p[0] == 'r'))
    {
        if (is_fd_explicit || mode == CFT_MODE_FD_BRS || mode == CFT_MODE_FD_NO_BRS || mode == CFT_MODE_TLV)
        {
            cft_set_error(err, err_size, "ramki zdalne (RTR) nie są obsługiwane w standardzie CAN FD");
            return false;
        }
        cft_span_t dlc_txt = { data_part.p + 1, data_part.n - 1U };
        dlc_txt = cft_strip(dlc_txt);
        unsigned dlc = 0U;
        for (size_t i = 0U; i < dlc_txt.n; i++)
        {
            if (dlc_txt.p[i] < '0' || dlc_txt.p[i] > '9')
            {
                dlc = CFT_MAX_CLASSIC_DLC + 1U;
                break;
            }
            dlc = dlc * 10U + (unsigned)(dlc_txt.p[i] - '0');
            if (dlc > CFT_MAX_CLASSIC_DLC)
            {
                break;
            }
        }
        if (dlc > CFT_MAX_CLASSIC_DLC)
        {
            cft_set_error(err, err_size, "nieprawidłowy DLC ramki RTR: '%.*s' (dozwolone 0–8)", (int)dlc_txt.n, dlc_txt.p);
            return false;
        }
        msg->format = CAN_MSG_FRAME_FORMAT_CAN;
        msg->flags = CAN_MSG_FLAG_RTR;
        msg->len = (uint8_t)dlc;
        return true;
    }

    /* Data: separators [\s.:_\-] removed, hex only, even number of digits */
    char compact[CFT_MAX_DIGITS];
    size_t compact_len = cft_compact(data_part, compact, sizeof(compact));
    for (size_t i = 0U; i < compact_len; i++)
    {
        if (cft_hex_val(compact[i]) < 0)
        {
            cft_set_error(err, err_size, "dane '%.*s' zawierają znaki spoza hex", (int)data_part.n, data_part.p);
            return false;
        }
    }
    if ((compact_len % 2U) != 0U)
    {
        cft_set_error(err, err_size,
                      "nieparzysta liczba cyfr hex w danych ('%s', długość: %u). Każdy bajt w CAN to 2 cyfry hex (np. '%s0' lub '0%s').",
                      compact, (unsigned)compact_len, compact, compact);
        return false;
    }

    uint8_t data[CFT_MAX_DIGITS / 2U];
    size_t data_len = 0U;
    (void)cft_from_hex(compact, compact_len, data, sizeof(data), &data_len, NULL, 0U);

    switch (mode)
    {
        case CFT_MODE_CLASSIC:
            if (data_len > CFT_MAX_CLASSIC_DLC)
            {
                cft_set_error(err, err_size,
                              "W trybie 'Klasyczny CAN 2.0' dopuszczalne jest maksymalnie 8 bajtów (wpisano: %u B). Skróć dane lub zmień standard ramki na CAN FD.",
                              (unsigned)data_len);
                return false;
            }
            cft_set_format(msg, false, false);
            break;

        case CFT_MODE_TLV:
            return cft_pack_tlv(CFT_TLV_DEFAULT_TYPE, data, data_len, true, msg, err, err_size);

        case CFT_MODE_FD_BRS:
        case CFT_MODE_FD_NO_BRS:
        case CFT_MODE_AUTO:
        default:
            if (data_len > CFT_MAX_FD_DLC)
            {
                cft_set_error(err, err_size, "za dużo danych: %u B (maksimum CAN FD: 64 bajty)", (unsigned)data_len);
                return false;
            }
            if (mode == CFT_MODE_FD_BRS || mode == CFT_MODE_FD_NO_BRS)
            {
                cft_set_format(msg, true, mode == CFT_MODE_FD_BRS);
            }
            else
            {
                /* Auto: FD from '##' or length > 8 (then BRS on by default) */
                bool is_fd = is_fd_explicit || (data_len > CFT_MAX_CLASSIC_DLC);
                bool brs = is_fd_explicit ? brs_flag : is_fd;
                cft_set_format(msg, is_fd, brs);
            }
            break;
    }

    msg->len = (uint8_t)data_len;
    memcpy(msg->data, data, data_len);
    return true;
}

/** @brief Appends formatted text, keeps track of the full length like snprintf. */
static void cft_append(char *buf, size_t size, size_t *pos, const char *fmt, ...) __attribute__((format(printf, 4, 5)));

static void cft_append(char *buf, size_t size, size_t *pos, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char *dst = (*pos < size) ? &buf[*pos] : NULL;
    size_t avail = (*pos < size) ? (size - *pos) : 0U;
    int n = vsnprintf(dst, avail, fmt, args);
    va_end(args);
    if (n > 0)
    {
        *pos += (size_t)n;
    }
}

size_t can_frame_text_format(const CAN_msg_t *msg, char *buf, size_t size)
{
    if (msg == NULL || buf == NULL || size == 0U)
    {
        return 0U;
    }
    buf[0] = '\0';

    size_t pos = 0U;
    const uint32_t id = CAN_msg_get_can_id(msg->ident);
    const bool is_fd = (msg->format == CAN_MSG_FRAME_FORMAT_FD_CAN);
    const uint8_t len = (msg->len > CFT_MAX_FD_DLC) ? CFT_MAX_FD_DLC : msg->len;

    if (CAN_msg_is_extended_id(msg->ident))
    {
        cft_append(buf, size, &pos, "%08lX [%u]", (unsigned long)id, (unsigned)msg->len);
    }
    else
    {
        cft_append(buf, size, &pos, "%03lX [%u]", (unsigned long)id, (unsigned)msg->len);
    }

    if ((msg->flags & CAN_MSG_FLAG_RTR) != 0U)
    {
        cft_append(buf, size, &pos, " RTR");
    }
    else
    {
        if (is_fd)
        {
            cft_append(buf, size, &pos, ((msg->flags & CAN_MSG_FLAG_BRS) != 0U) ? " [FD+BRS]" : " [FD]");
        }
        for (uint8_t i = 0U; i < len; i++)
        {
            cft_append(buf, size, &pos, " %02X", msg->data[i]);
        }

        /* TLV suffix for a valid ot_app_msg_tlv with a non-empty value */
        if (len >= CFT_TLV_HEADER_SIZE)
        {
            const uint16_t tlv_len = (uint16_t)(msg->data[1] | ((uint16_t)msg->data[2] << 8));
            if (tlv_len > 0U && tlv_len <= (uint16_t)(len - CFT_TLV_HEADER_SIZE))
            {
                const uint8_t *value = &msg->data[CFT_TLV_HEADER_SIZE];
                bool non_printable = false;

                cft_append(buf, size, &pos, " | TLV[0x%02X, len=%u]: \"", msg->data[0], (unsigned)tlv_len);
                for (uint16_t i = 0U; i < tlv_len; i++)
                {
                    const bool printable = (value[i] >= 32U && value[i] <= 126U);
                    non_printable = non_printable || !printable;
                    cft_append(buf, size, &pos, "%c", printable ? (char)value[i] : '?');
                }
                cft_append(buf, size, &pos, "\"");

                if (non_printable)
                {
                    for (uint16_t i = 0U; i < tlv_len; i++)
                    {
                        cft_append(buf, size, &pos, (i == 0U) ? " (%02X" : " %02X", value[i]);
                    }
                    cft_append(buf, size, &pos, ")");
                }
            }
        }
    }

    return (pos < size) ? pos : (size - 1U);
}

bool can_frame_text_mode_parse(const char *name, cft_mode_t *mode)
{
    if (name == NULL || mode == NULL)
    {
        return false;
    }
    cft_span_t s = cft_strip((cft_span_t){ name, strlen(name) });
    for (size_t i = 0U; i < sizeof(s_mode_names) / sizeof(s_mode_names[0]); i++)
    {
        if (s.n == strlen(s_mode_names[i]) && cft_starts_with_ci(s, s_mode_names[i]))
        {
            *mode = (cft_mode_t)i;
            return true;
        }
    }
    return false;
}

const char *can_frame_text_mode_name(cft_mode_t mode)
{
    if ((size_t)mode < sizeof(s_mode_names) / sizeof(s_mode_names[0]))
    {
        return s_mode_names[mode];
    }
    return "?";
}
