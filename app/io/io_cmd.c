/**
 * @file io_cmd.c
 * @brief UART commands for input simulation (docs/io/io_uart_simulation.md).
 *
 * Command names contain characters outside hex digits, so they never collide with the
 * CAN frame syntax of the UART bridge (e.g. "ACC#01" stays a frame with ID 0xACC).
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-26
 */

#include "io_cmd.h"
#include "io_sim.h"
#include "io_manager.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IO_CMD_TAG      "[IO] "
#define IO_CMD_OUT_MAX  256U
#define IO_CMD_NUM_MAX  99999U
#define IO_CMD_NAME_MAX 15U    /**< Longest command name ("DRZWI_KIER") with margin */

/** @brief Command kinds. */
typedef enum
{
    IO_CMD_SOURCE = 0,
    IO_CMD_SWITCH,
    IO_CMD_LOCKS,
    IO_CMD_RPM,
    IO_CMD_FUEL
} io_cmd_kind_t;

/** @brief Command table entry. */
typedef struct
{
    const char *name;
    io_cmd_kind_t kind;
    io_input_t input;   /**< IO_CMD_SWITCH / IO_CMD_LOCKS only */
} io_cmd_entry_t;

static const io_cmd_entry_t s_cmds[] = {
    { "ZRODLO",     IO_CMD_SOURCE, IO_IN_ACC },
    { "STACYJKA",   IO_CMD_SWITCH, IO_IN_ACC },
    { "DRZWI_KIER", IO_CMD_SWITCH, IO_IN_DOOR_DRV },
    { "DRZWI_INNE", IO_CMD_SWITCH, IO_IN_DOOR_OTHER },
    { "SWIATLA",    IO_CMD_SWITCH, IO_IN_LIGHTS },
    { "ZAMKI",      IO_CMD_LOCKS,  IO_IN_LOCKS },
    { "MASKA",      IO_CMD_SWITCH, IO_IN_HOOD },
    { "AWARYJNE",   IO_CMD_SWITCH, IO_IN_HAZARD },
    { "HAMULEC",    IO_CMD_SWITCH, IO_IN_HANDBRAKE },
    { "OBROTY",     IO_CMD_RPM,    IO_IN_ENGINE },
    { "PALIWO",     IO_CMD_FUEL,   IO_IN_ENGINE },
};

static io_cmd_out_fn s_out = NULL;
/** @brief Response line buffer (called from the bridge task only, kept off its stack). */
static char s_reply[IO_CMD_OUT_MAX];

/** @brief Prints one tagged response line. */
static void reply(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void reply(const char *fmt, ...)
{
    size_t len = strlen(IO_CMD_TAG);
    memcpy(s_reply, IO_CMD_TAG, len);

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
 * @brief Parses a decimal number (digits only). Values above IO_CMD_NUM_MAX saturate
 *        to IO_CMD_NUM_MAX + 1, so they are reported as out of range, not as a syntax error.
 */
static bool parse_uint(const char *s, uint32_t *value)
{
    uint32_t v = 0U;

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
        if (v <= IO_CMD_NUM_MAX)
        {
            v = v * 10U + (uint32_t)(*s - '0');
        }
    }
    if (v > IO_CMD_NUM_MAX)
    {
        v = IO_CMD_NUM_MAX + 1U;
    }
    *value = v;
    return true;
}

/* --- Commands --- */

static void cmd_help(void)
{
    reply("Symulacja wejść (po resecie źródło = IO, fizyczne wejścia):");
    reply("  ZRODLO#<UART|IO>   przełączenie źródła wejść, ZRODLO? = stan wszystkich wejść");
    reply("  STACYJKA#<0|1>  DRZWI_KIER#<0|1>  DRZWI_INNE#<0|1>  SWIATLA#<0|1>");
    reply("  MASKA#<0|1>  AWARYJNE#<0|1>  HAMULEC#<0|1>        1 = aktywne");
    reply("  OBROTY#<0..%u>    RPM, tylko wielokrotność %u (w ramce RPM/%u, odbiornik mnoży x%u)",
          (unsigned)IO_RPM_MAX, (unsigned)IO_RPM_PER_BIT, (unsigned)IO_RPM_PER_BIT, (unsigned)IO_RPM_PER_BIT);
    reply("  PALIWO#<0..%u>    poziom paliwa w %%", (unsigned)IO_FUEL_MAX);
    reply("  Zamki (SW4) = sprzężenie przekaźnika, tylko odczyt.");
    reply("  Komendy ustawiające działają tylko przy ZRODLO=UART.");
}

static void cmd_status(void)
{
    uint16_t phys_btn;
    uint8_t phys_fuel;
    io_sim_values_t v;

    io_manager_getPhysInputs(&phys_btn, &phys_fuel);
    io_sim_resolve(phys_btn, phys_fuel, &v);

#define IO_CMD_BIT(input) ((unsigned)((v.btn >> (unsigned)(input)) & 1U))
    reply("ZRODLO=%s STACYJKA=%u DRZWI_KIER=%u DRZWI_INNE=%u SWIATLA=%u ZAMKI=%u(fiz) MASKA=%u AWARYJNE=%u "
          "HAMULEC=%u OBROTY=%u PALIWO=%u",
          (v.src == IO_SRC_UART) ? "UART" : "IO",
          IO_CMD_BIT(IO_IN_ACC), IO_CMD_BIT(IO_IN_DOOR_DRV), IO_CMD_BIT(IO_IN_DOOR_OTHER),
          IO_CMD_BIT(IO_IN_LIGHTS), IO_CMD_BIT(IO_IN_LOCKS), IO_CMD_BIT(IO_IN_HOOD),
          IO_CMD_BIT(IO_IN_HAZARD), IO_CMD_BIT(IO_IN_HANDBRAKE),
          (unsigned)v.rpm, (unsigned)v.fuel);
#undef IO_CMD_BIT
}

static void cmd_source(const char *arg)
{
    io_src_t src;

    if (str_ieq(arg, "UART"))
    {
        src = IO_SRC_UART;
    }
    else if (str_ieq(arg, "IO"))
    {
        src = IO_SRC_IO;
    }
    else
    {
        reply("ERR: składnia: ZRODLO#<UART|IO>");
        return;
    }

    uint16_t phys_btn;
    uint8_t phys_fuel;
    io_manager_getPhysInputs(&phys_btn, &phys_fuel);
    io_sim_setSource(src, phys_btn, phys_fuel);
    reply("OK ZRODLO=%s", (src == IO_SRC_UART) ? "UART" : "IO");
}

/** @brief Prints the error for a setter result other than IO_SIM_OK. */
static void reply_error(const io_cmd_entry_t *cmd, io_sim_result_t res, uint32_t value)
{
    switch (res)
    {
        case IO_SIM_ERR_SRC:
            reply("ERR: źródło IO, najpierw ZRODLO#UART");
            break;
        case IO_SIM_ERR_READONLY:
            reply("ERR: %s = sprzężenie przekaźnika, tylko odczyt", cmd->name);
            break;
        case IO_SIM_ERR_RANGE:
            reply("ERR: %s poza zakresem 0..%u", cmd->name,
                  (unsigned)((cmd->kind == IO_CMD_RPM) ? IO_RPM_MAX : IO_FUEL_MAX));
            break;
        case IO_SIM_ERR_RPM_STEP:
        {
            uint32_t lower = (value / IO_RPM_PER_BIT) * IO_RPM_PER_BIT;
            uint32_t upper = lower + IO_RPM_PER_BIT;
            if (upper > IO_RPM_MAX)
            {
                reply("ERR: %s musi być wielokrotnością %u (np. %lu)", cmd->name,
                      (unsigned)IO_RPM_PER_BIT, (unsigned long)lower);
            }
            else
            {
                reply("ERR: %s musi być wielokrotnością %u (np. %lu lub %lu)", cmd->name,
                      (unsigned)IO_RPM_PER_BIT, (unsigned long)lower, (unsigned long)upper);
            }
            break;
        }
        case IO_SIM_ERR_INPUT:
        default:
            reply("ERR: %s nie może być ustawione", cmd->name);
            break;
    }
}

static void cmd_set(const io_cmd_entry_t *cmd, const char *arg)
{
    uint32_t value = 0U;
    bool number = parse_uint(arg, &value);
    io_sim_result_t res;

    switch (cmd->kind)
    {
        case IO_CMD_SWITCH:
        case IO_CMD_LOCKS:
            if (!number)
            {
                reply("ERR: składnia: %s#<0|1>", cmd->name);
                return;
            }
            if (value > 1U)
            {
                reply("ERR: %s: wartość musi być 0 lub 1", cmd->name);
                return;
            }
            res = io_sim_setInput(cmd->input, value != 0U);
            if (res == IO_SIM_OK)
            {
                reply("OK %s=%lu", cmd->name, (unsigned long)value);
                return;
            }
            break;

        case IO_CMD_RPM:
            if (!number)
            {
                reply("ERR: składnia: %s#<0..%u>", cmd->name, (unsigned)IO_RPM_MAX);
                return;
            }
            res = io_sim_setRpm(value);
            if (res == IO_SIM_OK)
            {
                reply("OK %s=%lu RPM (data[2]=0x%02X)", cmd->name, (unsigned long)value,
                      (unsigned)io_sim_rpmToByte((uint16_t)value));
                return;
            }
            break;

        case IO_CMD_FUEL:
        default:
            if (!number)
            {
                reply("ERR: składnia: %s#<0..%u>", cmd->name, (unsigned)IO_FUEL_MAX);
                return;
            }
            res = io_sim_setFuel(value);
            if (res == IO_SIM_OK)
            {
                reply("OK %s=%lu%%", cmd->name, (unsigned long)value);
                return;
            }
            break;
    }

    reply_error(cmd, res, value);
}

bool io_cmd_process(char *line, io_cmd_out_fn out)
{
    if (line == NULL || out == NULL)
    {
        return false;
    }
    s_out = out;

    if (str_ieq(line, "IO_HELP"))
    {
        cmd_help();
        return true;
    }
    if (str_ieq(line, "ZRODLO?"))
    {
        cmd_status();
        return true;
    }

    /* <NAME>[#<value>]: the name is matched on a copy, the line stays intact for the caller */
    char *sep = strchr(line, '#');
    size_t name_len = (sep != NULL) ? (size_t)(sep - line) : strlen(line);
    char name[IO_CMD_NAME_MAX + 1U];

    while (name_len > 0U && line[name_len - 1U] == ' ')
    {
        name_len--;
    }
    if (name_len == 0U || name_len > IO_CMD_NAME_MAX)
    {
        return false;
    }
    memcpy(name, line, name_len);
    name[name_len] = '\0';

    const io_cmd_entry_t *cmd = NULL;
    for (size_t i = 0U; i < (sizeof(s_cmds) / sizeof(s_cmds[0])); i++)
    {
        if (str_ieq(name, s_cmds[i].name))
        {
            cmd = &s_cmds[i];
            break;
        }
    }
    if (cmd == NULL)
    {
        return false;
    }

    const char *arg = (sep != NULL) ? str_trim(sep + 1) : "";

    if (cmd->kind == IO_CMD_SOURCE)
    {
        cmd_source(arg);
    }
    else
    {
        cmd_set(cmd, arg);
    }
    return true;
}
