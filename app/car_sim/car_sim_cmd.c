/**
 * @file car_sim_cmd.c
 * @brief UART commands of the car simulator (docs/car_sim/car_sim_tasks.md, T7).
 *
 * Command names contain characters outside hex digits, so they never collide with the CAN frame
 * syntax of the UART bridge. The module only posts requests to the car_sim task and reads
 * car_sim_get_info(), so it runs in the bridge task without blocking. Tables are ASCII only
 * (terminals without UTF-8 included), like IO_HELP.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#include "car_sim_cmd.h"
#include "car_sim.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/** @brief Response tag. */
#define CAR_SIM_CMD_TAG      "[SIM] "
/** @brief Longest response line. */
#define CAR_SIM_CMD_OUT_MAX  160U
/** @brief Longest command argument kept (key, number, PIN). */
#define CAR_SIM_CMD_ARG_MAX  15U
/** @brief Separator line of the SIM_LIST table. */
#define CAR_SIM_LIST_SEP     "+----------+----------------------+-----------+----------+----+-------+"

static car_sim_cmd_out_fn s_out = NULL;
/** @brief Response line buffer (called from the bridge task only, kept off its stack). */
static char s_reply[CAR_SIM_CMD_OUT_MAX];

/** @brief Prints one tagged response line. */
static void reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void reply(const char *fmt, ...)
{
    size_t len = strlen(CAR_SIM_CMD_TAG);
    memcpy(s_reply, CAR_SIM_CMD_TAG, len);

    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(&s_reply[len], sizeof(s_reply) - len, fmt, args);
    va_end(args);

    s_out(s_reply);
}

/* --- Text helpers --- */

static char to_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool str_ieq(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++)
    {
        if (to_upper(*a) != to_upper(*b))
        {
            return false;
        }
    }
    return (*a == '\0') && (*b == '\0');
}

/**
 * @brief Copies text[0..len) without leading and trailing spaces.
 * @return false if the trimmed text does not fit into out (out_size - 1 characters).
 */
static bool copy_trimmed(const char *text, size_t len, char *out, size_t out_size)
{
    while (len > 0U && *text == ' ')
    {
        text++;
        len--;
    }
    while (len > 0U && text[len - 1U] == ' ')
    {
        len--;
    }
    if (len >= out_size)
    {
        return false;
    }
    memcpy(out, text, len);
    out[len] = '\0';
    return true;
}

/** @brief Parses a decimal device parser number 0..65535 (digits only). */
static bool parse_dev_parser(const char *s, uint16_t *no, bool *in_range)
{
    uint32_t v = 0U;

    *in_range = true;
    if (*s == '\0')
    {
        return false;
    }
    for (; *s != '\0'; s++)
    {
        if (*s < '0' || *s > '9')
        {
            return false;
        }
        if (v <= 0xFFFFU)
        {
            v = v * 10U + (uint32_t)(*s - '0');
        }
    }
    *in_range = (v <= 0xFFFFU);
    *no = (uint16_t)v;
    return true;
}

/** @brief Parses exactly four hex digits. */
static bool parse_pin(const char *s, uint16_t *pin)
{
    uint16_t v = 0U;

    if (strlen(s) != 4U)
    {
        return false;
    }
    for (; *s != '\0'; s++)
    {
        const char c = to_upper(*s);
        uint16_t digit;
        if (c >= '0' && c <= '9')
        {
            digit = (uint16_t)(c - '0');
        }
        else if (c >= 'A' && c <= 'F')
        {
            digit = (uint16_t)(c - 'A' + 10);
        }
        else
        {
            return false;
        }
        v = (uint16_t)((v << 4) | digit);
    }
    *pin = v;
    return true;
}

/** @brief Device parser numbers of a profile as text ("196", "0 + inne" for BRAK). */
static void format_dev_parsers(car_sim_id_t id, const car_sim_profile_t *p, char *out, size_t size)
{
    size_t pos = 0U;

    out[0] = '\0';
    if (id == CAR_SIM_ID_NONE)
    {
        (void)snprintf(out, size, "0 + inne");
        return;
    }
    for (uint8_t i = 0U; (p->dev_parsers != NULL) && (i < p->num_dev_parsers) && (pos + 7U < size); i++)
    {
        pos += (size_t)snprintf(&out[pos], size - pos, (i == 0U) ? "%u" : ",%u", (unsigned)p->dev_parsers[i]);
    }
    if (pos == 0U)
    {
        (void)snprintf(out, size, "-");
    }
}

/** @brief Bus configuration of a profile as text ("500k", "500k FD", "500k/2000k FD_BRS"). */
static void format_bus(const car_sim_profile_t *p, char *out, size_t size)
{
    const can_config_t *bus = p->bus_config;
    const unsigned long nominal = (unsigned long)bus->baudrate_normal / 1000UL;

    if (bus->mode == CAN_MODE_FD_BRS)
    {
        (void)snprintf(out, size, "%luk/%luk FD", nominal, (unsigned long)bus->baudrate_FD / 1000UL);
    }
    else
    {
        (void)snprintf(out, size, (bus->mode == CAN_MODE_CLASSIC) ? "%luk" : "%luk FD", nominal);
    }
}

/* --- Commands --- */

static void cmd_help(void)
{
    reply("SIM?                     aktywny symulator, tryb, parser urzadzenia, PIN, liczniki");
    reply("SIM_LIST                 lista symulatorow aut");
    reply("SIM#<KLUCZ|numer>        wybor reczny, np. SIM#V_BOX, SIM#192, SIM#BRAK (tryb RECZNY)");
    reply("SIM#AUTO                 wybor wg parsera urzadzenia CAN1 odczytanego przez RS485");
    reply("SIM_PIN#<hex4>           PIN v-box w RAM dla aut z pilotem, np. SIM_PIN#F15C");
    reply("SIM_PIN#DOMYSLNY         PIN z profilu auta");
    reply("PILOT#<OTWORZ|ZAMKNIJ>   ramka pilota z PIN-em (auto z pilotem) i przekaznik zamka");
}

static void cmd_status(void)
{
    car_sim_info_t info;
    char parsers[24];
    char bus[24];

    car_sim_get_info(&info);
    const car_sim_profile_t *p = info.profile;
    if (p == NULL)
    {
        reply("ERR: symulator aut nie działa");
        return;
    }

    format_bus(p, bus, sizeof(bus));
    if (info.last_dev_parser == CAR_SIM_DEV_PARSER_UNKNOWN)
    {
        (void)snprintf(parsers, sizeof(parsers), "nieodczytany");
    }
    else
    {
        (void)snprintf(parsers, sizeof(parsers), "%u", (unsigned)info.last_dev_parser);
    }

    reply("SIM=%s \"%s\", tryb %s, parser urządzenia CAN1: %s", p->key, p->name,
          (info.mode == CAR_SIM_MODE_AUTO) ? "AUTO" : "RECZNY", parsers);
    reply("CAN1 %s, ramek TX: %u, komend LX: %u, pilot: %s", bus, (unsigned)p->num_tx, (unsigned)p->num_rx,
          (p->fob != NULL) ? "tak" : "nie");
    if (info.pin_overridden)
    {
        reply("PIN: %04X (SIM_PIN#, %s)", (unsigned)info.pin, (p->fob != NULL) ? "nadpisany" : "symulator bez pilota");
    }
    else if (p->fob != NULL)
    {
        reply("PIN: %04X (domyślny)", (unsigned)info.pin);
    }
    else
    {
        reply("PIN: - (symulator bez pilota)");
    }
    reply("liczniki: TX nadane %lu, odrzucone %lu; komendy LX odebrane %lu, odrzucone przez PIN %lu",
          (unsigned long)info.tx_sent,
          (unsigned long)info.tx_dropped, (unsigned long)info.rx_cmds, (unsigned long)info.rx_pin_rejected);
    if (!info.owns_can1)
    {
        reply("CAN1 zajęty przez test CAN FD: nadawany tylko V_BOX, wybór symulatora wyłączony");
    }
}

static void cmd_list(void)
{
    car_sim_info_t info;

    car_sim_get_info(&info);
    reply(CAR_SIM_LIST_SEP);
    reply("| %-8s | %-20s | %-9s | %-8s | %-2s | %-5s |", "Klucz", "Nazwa", "Parsery", "CAN1", "TX", "Pilot");
    reply(CAR_SIM_LIST_SEP);
    for (unsigned id = 0U; id < (unsigned)CAR_SIM_COUNT; id++)
    {
        const car_sim_profile_t *p = car_sim_get((car_sim_id_t)id);
        char parsers[16];
        char bus[24];
        if (p == NULL)
        {
            continue;
        }
        format_dev_parsers((car_sim_id_t)id, p, parsers, sizeof(parsers));
        format_bus(p, bus, sizeof(bus));
        reply("|%c%-8s | %-20.20s | %-9s | %-8s | %2u | %-5s |", (id == (unsigned)info.id) ? '*' : ' ', p->key,
              p->name, parsers, bus, (unsigned)p->num_tx, (p->fob != NULL) ? "tak" : "nie");
    }
    reply(CAR_SIM_LIST_SEP);
    reply("* = aktywny, tryb %s", (info.mode == CAR_SIM_MODE_AUTO) ? "AUTO" : "RECZNY");
}

/** @brief Returns false (with an error response) while CAN1 belongs to the CAN FD test. */
static bool check_can1(void)
{
    car_sim_info_t info;

    car_sim_get_info(&info);
    if (!info.owns_can1)
    {
        reply("ERR: CAN1 zajęty przez test CAN FD, wybór symulatora wyłączony");
        return false;
    }
    return true;
}

static void cmd_select(const char *arg)
{
    car_sim_id_t id;
    uint16_t no;
    bool in_range;

    if (arg[0] == '\0')
    {
        reply("ERR: składnia: SIM#<KLUCZ|numer parsera urządzenia> albo SIM#AUTO (SIM_LIST)");
        return;
    }
    if (str_ieq(arg, "AUTO"))
    {
        if (!check_can1())
        {
            return;
        }
        car_sim_info_t info;
        car_sim_get_info(&info);
        car_sim_set_mode(CAR_SIM_MODE_AUTO);
        if (info.last_dev_parser == CAR_SIM_DEV_PARSER_UNKNOWN)
        {
            reply("OK tryb AUTO: parser urządzenia CAN1 jeszcze nieodczytany, symulator bez zmian");
        }
        else
        {
            const car_sim_profile_t *p = car_sim_get(car_sim_find_by_dev_parser(info.last_dev_parser));
            reply("OK tryb AUTO: parser urządzenia %u -> SIM=%s", (unsigned)info.last_dev_parser, p->key);
        }
        return;
    }

    if (parse_dev_parser(arg, &no, &in_range))
    {
        if (!in_range)
        {
            reply("ERR: numer parsera urządzenia poza zakresem 0..65535");
            return;
        }
        id = car_sim_find_by_dev_parser(no);
        if (id == CAR_SIM_ID_NONE && no != 0U)
        {
            reply("ERR: brak symulatora dla parsera urządzenia %u (SIM_LIST)", (unsigned)no);
            return;
        }
    }
    else
    {
        id = car_sim_find_by_key(arg);
        if (id == CAR_SIM_COUNT)
        {
            reply("ERR: nieznany symulator %s (SIM_LIST)", arg);
            return;
        }
    }

    if (!check_can1())
    {
        return;
    }
    car_sim_request_select(id);
    reply("OK SIM=%s, tryb RECZNY", car_sim_get(id)->key);
}

static void cmd_pin(const char *arg)
{
    uint16_t pin;

    if (str_ieq(arg, "DOMYSLNY"))
    {
        car_sim_reset_pin();
        car_sim_info_t info;
        car_sim_get_info(&info);
        if (info.profile != NULL && info.profile->fob != NULL)
        {
            reply("OK SIM_PIN=DOMYSLNY (%04X)", (unsigned)info.pin);
        }
        else
        {
            reply("OK SIM_PIN=DOMYSLNY (symulator bez pilota)");
        }
        return;
    }
    if (!parse_pin(arg, &pin))
    {
        reply("ERR: składnia: SIM_PIN#<4 cyfry hex> albo SIM_PIN#DOMYSLNY");
        return;
    }
    car_sim_set_pin(pin);
    reply("OK SIM_PIN=%04X", (unsigned)pin);
}

static void cmd_fob(const char *arg)
{
    const bool unlock = str_ieq(arg, "OTWORZ");
    car_sim_info_t info;

    if (!unlock && !str_ieq(arg, "ZAMKNIJ"))
    {
        reply("ERR: składnia: PILOT#<OTWORZ|ZAMKNIJ>");
        return;
    }
    car_sim_get_info(&info);
    if (info.profile == NULL)
    {
        reply("ERR: symulator aut nie działa");
        return;
    }
    car_sim_request_fob(unlock);
    if (info.profile->fob != NULL)
    {
        reply("OK PILOT#%s: ramka 0x%03lX z PIN-em %04X i przekaźnik", unlock ? "OTWORZ" : "ZAMKNIJ",
              (unsigned long)info.profile->fob->id, (unsigned)info.pin);
    }
    else
    {
        reply("OK PILOT#%s: symulator %s bez pilota, tylko przekaźnik", unlock ? "OTWORZ" : "ZAMKNIJ",
              info.profile->key);
    }
}

/* --- Public API ------------------------------------------------------------ */

bool car_sim_cmd_process(const char *line, car_sim_cmd_out_fn out)
{
    if (line == NULL || out == NULL)
    {
        return false;
    }
    s_out = out;

    if (str_ieq(line, "SIM?"))
    {
        cmd_status();
        return true;
    }
    if (str_ieq(line, "SIM_LIST"))
    {
        cmd_list();
        return true;
    }
    if (str_ieq(line, "SIM_HELP"))
    {
        cmd_help();
        return true;
    }

    /* <NAME>#<argument>, spaces around '#' allowed */
    const char *sep = strchr(line, '#');
    char name[CAR_SIM_CMD_ARG_MAX + 1U];
    char arg[CAR_SIM_CMD_ARG_MAX + 1U];

    if (!copy_trimmed(line, (sep != NULL) ? (size_t)(sep - line) : strlen(line), name, sizeof(name)))
    {
        return false;
    }

    void (*handler)(const char *arg) = NULL;
    const char *syntax = NULL;
    if (str_ieq(name, "SIM"))
    {
        handler = cmd_select;
        syntax = "SIM#<KLUCZ|numer parsera urządzenia> albo SIM#AUTO";
    }
    else if (str_ieq(name, "SIM_PIN"))
    {
        handler = cmd_pin;
        syntax = "SIM_PIN#<4 cyfry hex> albo SIM_PIN#DOMYSLNY";
    }
    else if (str_ieq(name, "PILOT"))
    {
        handler = cmd_fob;
        syntax = "PILOT#<OTWORZ|ZAMKNIJ>";
    }
    else
    {
        return false;
    }

    if (sep == NULL)
    {
        reply("ERR: składnia: %s", syntax);
        return true;
    }
    if (!copy_trimmed(sep + 1, strlen(sep + 1), arg, sizeof(arg)))
    {
        reply("ERR: składnia: %s", syntax);
        return true;
    }
    handler(arg);
    return true;
}
