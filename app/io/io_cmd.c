/**
 * @file io_cmd.c
 * @brief UART commands for input simulation (docs/io/io_uart_simulation.md).
 *
 * Command names contain characters outside hex digits, so they never collide with the
 * CAN frame syntax of the UART bridge (e.g. "ACC#01" stays a frame with ID 0xACC).
 *
 * The RPM range and step come from the active car simulator (io_sim_getRpmLimits()). While the
 * active simulator does not send the vehicle signals (V_BOX, BRAK), every response is identical
 * to the V_BOX texts; otherwise ZRODLO?, IO_HELP and IO_RESET also list the vehicle signals.
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
#define IO_CMD_NUM_MAX  9999999U
#define IO_CMD_NAME_MAX 15U    /**< Longest command name ("HAMULEC_NOZNY") with margin */

/** @brief Command kinds. */
typedef enum
{
    IO_CMD_SOURCE = 0,
    IO_CMD_SWITCH,
    IO_CMD_LOCKS,
    IO_CMD_RPM,
    IO_CMD_FUEL,
    IO_CMD_LOCK_FAULT,
    IO_CMD_SPEED,
    IO_CMD_ODOMETER,
    IO_CMD_HIGH_BEAM,
    IO_CMD_BRAKE_PEDAL,
    IO_CMD_ENGINE_TEMP
} io_cmd_kind_t;

/** @brief Command table entry. */
typedef struct
{
    const char *name;
    io_cmd_kind_t kind;
    io_input_t input;   /**< IO_CMD_SWITCH / IO_CMD_LOCKS only */
} io_cmd_entry_t;

static const io_cmd_entry_t s_cmds[] = {
    { "ZRODLO",       IO_CMD_SOURCE,     IO_IN_ACC },
    { "STACYJKA",     IO_CMD_SWITCH,     IO_IN_ACC },
    { "DRZWI_INNE",   IO_CMD_SWITCH,     IO_IN_DOOR_OTHER },
    { "DRZWI_KIER",   IO_CMD_SWITCH,     IO_IN_DOOR_DRV },
    { "SWIATLA",      IO_CMD_SWITCH,     IO_IN_LIGHTS },
    { "ZAMKI",        IO_CMD_LOCKS,      IO_IN_LOCKS },
    { "AWARYJNE",     IO_CMD_SWITCH,     IO_IN_HAZARD },
    { "MASKA",        IO_CMD_SWITCH,     IO_IN_HOOD },
    { "HAMULEC",      IO_CMD_SWITCH,     IO_IN_HANDBRAKE },
    { "OBROTY",       IO_CMD_RPM,        IO_IN_ENGINE },
    { "PALIWO",       IO_CMD_FUEL,       IO_IN_ENGINE },
    { "AWARIA_ZAMKA", IO_CMD_LOCK_FAULT, IO_IN_LOCKS },
    { "PREDKOSC",      IO_CMD_SPEED,       IO_IN_ENGINE },
    { "PRZEBIEG",      IO_CMD_ODOMETER,    IO_IN_ENGINE },
    { "DROGOWE",       IO_CMD_HIGH_BEAM,   IO_IN_LIGHTS },
    { "HAMULEC_NOZNY", IO_CMD_BRAKE_PEDAL, IO_IN_ENGINE },
    { "TEMP_SILNIKA",  IO_CMD_ENGINE_TEMP, IO_IN_ENGINE },
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

/** @brief Parses a decimal number with an optional leading '-' (see parse_uint()). */
static bool parse_int(const char *s, int32_t *value)
{
    bool negative = (*s == '-');
    uint32_t v = 0U;

    if (!parse_uint(negative ? (s + 1) : s, &v))
    {
        return false;
    }
    *value = negative ? -(int32_t)v : (int32_t)v;
    return true;
}

/* --- Commands --- */

#define IO_HELP_SEP "+--------------------+-----------+------------------------------------------------+"

/** @brief One row of the IO_HELP table (ASCII only, terminals without UTF-8 included). */
static void help_row(const char *cmd, const char *values, const char *desc)
{
    reply("| %-18s | %-9s | %-46s |", cmd, values, desc);
}

static void cmd_help(void)
{
    char rpm_range[16];
    char fuel_range[16];
    char rpm_note[48];
    const io_sim_rpm_limits_t limits = io_sim_getRpmLimits();
    const bool vehicle = io_sim_areVehicleSignalsUsed();

    (void)snprintf(rpm_range, sizeof(rpm_range), "0..%u", (unsigned)limits.max);
    (void)snprintf(fuel_range, sizeof(fuel_range), "0..%u", (unsigned)IO_FUEL_MAX);
    if (limits.step > 1U)
    {
        (void)snprintf(rpm_note, sizeof(rpm_note), "obroty silnika [RPM], wielokrotnosc %u",
                       (unsigned)limits.step);
    }
    else
    {
        (void)snprintf(rpm_note, sizeof(rpm_note), "obroty silnika [RPM]");
    }

    if (vehicle)
    {
        reply("Symulacja wejsc auta (po resecie MCU zrodlo = IO, fizyczne wejscia)");
    }
    else
    {
        reply("Symulacja wejsc ramki 0x01F2F3F4 (po resecie MCU zrodlo = IO, fizyczne wejscia)");
    }
    reply(IO_HELP_SEP);
    help_row("Komenda", "Wartosci", "Opis");
    reply(IO_HELP_SEP);
    help_row("ZRODLO#<UART|IO>", "UART, IO", "zrodlo wejsc: UART = komendy, IO = fizyczne");
    help_row("ZRODLO?", "-", "stan wszystkich wejsc");
    help_row("IO_RESET", "-", "wejscia 0, OBROTY 0, PALIWO 0, zamek sprawny");
    reply(IO_HELP_SEP);
    help_row("STACYJKA#<v>", "0 / 1", "ACC stacyjka (SW0), 1 = wlaczona");
    help_row("DRZWI_INNE#<v>", "0 / 1", "drzwi inne (SW1), 1 = otwarte");
    help_row("DRZWI_KIER#<v>", "0 / 1", "drzwi kierowcy (SW2), 1 = otwarte");
    help_row("SWIATLA#<v>", "0 / 1", vehicle ? "swiatla wg symulatora (SW3), 1 = wlaczone"
                                            : "swiatla (SW3), 1 = wlaczone");
    help_row("AWARYJNE#<v>", "0 / 1", "swiatla awaryjne (SW5), 1 = wlaczone");
    help_row("MASKA#<v>", "0 / 1", "maska (SW6), 1 = otwarta");
    help_row("HAMULEC#<v>", "0 / 1", "hamulec reczny (SW7), 1 = zaciagniety");
    help_row("OBROTY#<v>", rpm_range, rpm_note);
    help_row("PALIWO#<v>", fuel_range, "poziom paliwa [%]");
    help_row("AWARIA_ZAMKA#<v>", "0 / 1", "1 = uszkodzony zamek, przekaznik nie reaguje");
    reply(IO_HELP_SEP);
    if (vehicle)
    {
        char speed_range[16];
        char odo_range[16];
        char temp_range[16];
        char odo_note[48];
        char temp_note[48];

        (void)snprintf(odo_note, sizeof(odo_note), "przebieg [km], domyslnie %lu",
                       (unsigned long)IO_ODOMETER_DEFAULT_KM);
        (void)snprintf(temp_note, sizeof(temp_note), "temperatura silnika [C], domyslnie %d",
                       IO_ENGINE_TEMP_DEFAULT);
        (void)snprintf(speed_range, sizeof(speed_range), "0..%u", (unsigned)IO_SPEED_MAX);
        (void)snprintf(odo_range, sizeof(odo_range), "0..%lu", (unsigned long)IO_ODOMETER_MAX_KM);
        (void)snprintf(temp_range, sizeof(temp_range), "%d..%d", IO_ENGINE_TEMP_MIN, IO_ENGINE_TEMP_MAX);
        help_row("PREDKOSC#<v>", speed_range, "predkosc [km/h], przebieg rosnie z predkoscia");
        help_row("PRZEBIEG#<v>", odo_range, odo_note);
        help_row("DROGOWE#<v>", "0 / 1", "swiatla drogowe, 1 = wlaczone");
        help_row("HAMULEC_NOZNY#<v>", "0 / 1", "pedal hamulca, 1 = wcisniety");
        help_row("TEMP_SILNIKA#<v>", temp_range, temp_note);
        reply(IO_HELP_SEP);
    }
    reply("Komendy ustawiajace (bez ZRODLO) dzialaja tylko przy ZRODLO=UART.");
    if (vehicle)
    {
        reply("OBROTY: SW8 = %u RPM, przy STACYJKA=0 auto nadaje 0 RPM.", (unsigned)limits.engine_on);
        reply("PREDKOSC..TEMP_SILNIKA: tylko UART, przy ZRODLO=IO wartosci domyslne.");
    }
    else
    {
        reply("OBROTY: w ramce data[2] = RPM/%u, odbiornik mnozy x%u.",
              (unsigned)IO_RPM_PER_BIT, (unsigned)IO_RPM_PER_BIT);
    }
    reply("ZAMKI (SW4) = sprzezenie przekaznika, tylko odczyt.");
}

static void cmd_status(void)
{
    uint16_t phys_btn;
    uint8_t phys_fuel;
    io_sim_values_t v;

    io_manager_getPhysInputs(&phys_btn, &phys_fuel);
    io_sim_resolve(phys_btn, phys_fuel, &v);

#define IO_CMD_BIT(input) ((unsigned)((v.btn >> (unsigned)(input)) & 1U))
    reply("ZRODLO=%s STACYJKA=%u DRZWI_INNE=%u DRZWI_KIER=%u SWIATLA=%u ZAMKI=%u(fiz) AWARYJNE=%u MASKA=%u "
          "HAMULEC=%u OBROTY=%u PALIWO=%u AWARIA_ZAMKA=%u",
          (v.src == IO_SRC_UART) ? "UART" : "IO",
          IO_CMD_BIT(IO_IN_ACC), IO_CMD_BIT(IO_IN_DOOR_OTHER), IO_CMD_BIT(IO_IN_DOOR_DRV),
          IO_CMD_BIT(IO_IN_LIGHTS), IO_CMD_BIT(IO_IN_LOCKS), IO_CMD_BIT(IO_IN_HAZARD),
          IO_CMD_BIT(IO_IN_HOOD), IO_CMD_BIT(IO_IN_HANDBRAKE),
          (unsigned)v.rpm, (unsigned)v.fuel, v.lock_fault ? 1U : 0U);
#undef IO_CMD_BIT
    if (io_sim_areVehicleSignalsUsed())
    {
        reply("PREDKOSC=%u PRZEBIEG=%lu DROGOWE=%u HAMULEC_NOZNY=%u TEMP_SILNIKA=%d",
              (unsigned)v.speed_kmh, (unsigned long)(v.odometer_m / 1000U), v.high_beam ? 1U : 0U,
              v.brake_pedal ? 1U : 0U, (int)v.engine_temp_c);
    }
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

/** @brief Returns the upper limit of a numeric command with the range 0..max. */
static uint32_t range_max(io_cmd_kind_t kind)
{
    switch (kind)
    {
        case IO_CMD_RPM:
            return io_sim_getRpmLimits().max;
        case IO_CMD_SPEED:
            return IO_SPEED_MAX;
        case IO_CMD_ODOMETER:
            return IO_ODOMETER_MAX_KM;
        case IO_CMD_FUEL:
        default:
            return IO_FUEL_MAX;
    }
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
            if (cmd->kind == IO_CMD_ENGINE_TEMP)
            {
                reply("ERR: %s poza zakresem %d..%d", cmd->name, IO_ENGINE_TEMP_MIN, IO_ENGINE_TEMP_MAX);
            }
            else
            {
                reply("ERR: %s poza zakresem 0..%lu", cmd->name, (unsigned long)range_max(cmd->kind));
            }
            break;
        case IO_SIM_ERR_RPM_STEP:
        {
            const io_sim_rpm_limits_t limits = io_sim_getRpmLimits();
            uint32_t lower = (value / limits.step) * limits.step;
            uint32_t upper = lower + limits.step;
            if (upper > limits.max)
            {
                reply("ERR: %s musi być wielokrotnością %u (np. %lu)", cmd->name,
                      (unsigned)limits.step, (unsigned long)lower);
            }
            else
            {
                reply("ERR: %s musi być wielokrotnością %u (np. %lu lub %lu)", cmd->name,
                      (unsigned)limits.step, (unsigned long)lower, (unsigned long)upper);
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
        case IO_CMD_LOCK_FAULT:
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
            res = io_sim_setLockFault(value != 0U);
            if (res == IO_SIM_OK)
            {
                reply("OK %s=%lu%s", cmd->name, (unsigned long)value,
                      (value != 0U) ? " (przekaźnik nie reaguje na ramki CAN)" : "");
                return;
            }
            break;

        case IO_CMD_HIGH_BEAM:
        case IO_CMD_BRAKE_PEDAL:
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
            res = (cmd->kind == IO_CMD_HIGH_BEAM) ? io_sim_setHighBeam(value != 0U)
                                                  : io_sim_setBrakePedal(value != 0U);
            if (res == IO_SIM_OK)
            {
                reply("OK %s=%lu", cmd->name, (unsigned long)value);
                return;
            }
            break;

        case IO_CMD_SPEED:
        case IO_CMD_ODOMETER:
            if (!number)
            {
                reply("ERR: składnia: %s#<0..%lu>", cmd->name, (unsigned long)range_max(cmd->kind));
                return;
            }
            res = (cmd->kind == IO_CMD_SPEED) ? io_sim_setSpeed(value) : io_sim_setOdometer(value);
            if (res == IO_SIM_OK)
            {
                reply("OK %s=%lu %s", cmd->name, (unsigned long)value,
                      (cmd->kind == IO_CMD_SPEED) ? "km/h" : "km");
                return;
            }
            break;

        case IO_CMD_ENGINE_TEMP:
        {
            int32_t temp = 0;
            if (!parse_int(arg, &temp))
            {
                reply("ERR: składnia: %s#<%d..%d>", cmd->name, IO_ENGINE_TEMP_MIN, IO_ENGINE_TEMP_MAX);
                return;
            }
            res = io_sim_setEngineTemp(temp);
            if (res == IO_SIM_OK)
            {
                reply("OK %s=%ld C", cmd->name, (long)temp);
                return;
            }
            break;
        }

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
                reply("ERR: składnia: %s#<0..%lu>", cmd->name, (unsigned long)range_max(cmd->kind));
                return;
            }
            res = io_sim_setRpm(value);
            if (res == IO_SIM_OK)
            {
                if (io_sim_areVehicleSignalsUsed())
                {
                    reply("OK %s=%lu RPM", cmd->name, (unsigned long)value);
                }
                else
                {
                    reply("OK %s=%lu RPM (data[2]=0x%02X)", cmd->name, (unsigned long)value,
                          (unsigned)io_sim_rpmToByte((uint16_t)value));
                }
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
    if (str_ieq(line, "IO_RESET"))
    {
        if (io_sim_reset() == IO_SIM_OK)
        {
            if (io_sim_areVehicleSignalsUsed())
            {
                reply("OK IO_RESET: wejścia=0, OBROTY=0, PALIWO=0, AWARIA_ZAMKA=0, PREDKOSC=0, PRZEBIEG=%lu, "
                      "DROGOWE=0, HAMULEC_NOZNY=0, TEMP_SILNIKA=%d",
                      (unsigned long)IO_ODOMETER_DEFAULT_KM, IO_ENGINE_TEMP_DEFAULT);
            }
            else
            {
                reply("OK IO_RESET: wejścia=0, OBROTY=0, PALIWO=0, AWARIA_ZAMKA=0");
            }
        }
        else
        {
            reply("ERR: źródło IO, najpierw ZRODLO#UART");
        }
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
