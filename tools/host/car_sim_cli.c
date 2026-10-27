/**
 * @file car_sim_cli.c
 * @brief Host harness for app/car_sim (codec, registry, car profiles) (tools/test_car_sim_host.py).
 *
 * stdin:  one line per step:
 *           "EFF key=value ..."  sets effective inputs (btn hex, fuel, rpm, speed, odo [m], high, brake,
 *                                temp), builds the vehicle state and prints it; omitted keys keep their value
 *           "SIM <key>"          selects a profile: a registry key or a harness profile (TEST, SCHED13, BAD_*)
 *           "ENCODE"             encodes all TX frames of the selected profile
 *           "PIN <hex>"          sets the v-box PIN used by RX
 *           "RX <id> <bytes>"    matches a received frame (8 hex digits = extended ID)
 *           "FOB <OTWORZ|ZAMKNIJ> <pin hex>"  builds the key fob frame
 *           "SCHED <ms>"         frames due in the tick
 *           "SCHEDSTAT <ms>"     runs the scheduler for <ms>: transmissions per frame, maximum per tick
 *           "MAP <no>"           device parser number -> simulator key
 *           "KEY <key>"          simulator key lookup
 *           "CHECK"              validates the registry; "CHECKP" validates the selected profile
 *           "CMD <line>"         UART command (car_sim_cmd.c) against the engine stub below; the responses are
 *                                followed by " >> " and the engine requests it made (select, mode, pin, fob)
 *           "ENGINE key=value"   engine stub state: dev (device parser number, "-" = not read yet), own (CAN1
 *                                owned), tx, drop, rx, rej (counters)
 * stdout: one line per step, items joined with " | ". Frames: "<ID>#<hex bytes>", ID with 3 hex digits
 *         (standard) or 8 hex digits (extended).
 */

#include "car_sim.h"
#include "car_sim_cmd.h"
#include "car_sim_codec.h"
#include "car_sim_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_out[8192];
static io_sim_values_t s_eff;
static const car_sim_profile_t *s_sim = NULL;
static car_sim_schedule_t s_sched;
static uint16_t s_pin = 0x1234U;

static void out(const char *text)
{
    if (s_out[0] != '\0')
    {
        strncat(s_out, " | ", sizeof(s_out) - strlen(s_out) - 1U);
    }
    strncat(s_out, text, sizeof(s_out) - strlen(s_out) - 1U);
}

/* --- Engine stub (car_sim.c API used by car_sim_cmd.c; requests are applied at once) ------------- */

static car_sim_id_t s_eng_id = CAR_SIM_ID_V_BOX;
static car_sim_mode_t s_eng_mode = CAR_SIM_MODE_AUTO;
static uint16_t s_eng_dev = CAR_SIM_DEV_PARSER_UNKNOWN;
static bool s_eng_pin_set = false;
static uint16_t s_eng_pin = 0U;
static bool s_eng_own = true;
static uint32_t s_eng_cnt[4];
static char s_calls[256];

static void call(const char *fmt, const char *arg)
{
    char text[64];
    snprintf(text, sizeof(text), fmt, arg);
    if (s_calls[0] != '\0')
    {
        strncat(s_calls, ", ", sizeof(s_calls) - strlen(s_calls) - 1U);
    }
    strncat(s_calls, text, sizeof(s_calls) - strlen(s_calls) - 1U);
}

void car_sim_request_select(car_sim_id_t id)
{
    s_eng_id = id;
    s_eng_mode = CAR_SIM_MODE_MANUAL;
    call("select %s", car_sim_get(id)->key);
}

void car_sim_request_dev_parser(uint16_t no)
{
    s_eng_dev = no;
}

void car_sim_request_fob(bool unlock)
{
    call("fob %s", unlock ? "OTWORZ" : "ZAMKNIJ");
}

void car_sim_set_mode(car_sim_mode_t mode)
{
    s_eng_mode = mode;
    if (mode == CAR_SIM_MODE_AUTO && s_eng_dev != CAR_SIM_DEV_PARSER_UNKNOWN)
    {
        s_eng_id = car_sim_find_by_dev_parser(s_eng_dev);
    }
    call("mode %s", (mode == CAR_SIM_MODE_AUTO) ? "AUTO" : "MANUAL");
}

void car_sim_set_pin(uint16_t pin)
{
    char text[8];
    snprintf(text, sizeof(text), "%04X", (unsigned)pin);
    s_eng_pin_set = true;
    s_eng_pin = pin;
    call("pin %s", text);
}

void car_sim_reset_pin(void)
{
    s_eng_pin_set = false;
    call("pin %s", "reset");
}

void car_sim_get_info(car_sim_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->id = s_eng_id;
    info->profile = car_sim_get(s_eng_id);
    info->mode = s_eng_mode;
    info->last_dev_parser = s_eng_dev;
    info->pin_overridden = s_eng_pin_set;
    info->pin = s_eng_pin_set ? s_eng_pin : ((info->profile->fob != NULL) ? info->profile->fob->default_pin : 0U);
    info->owns_can1 = s_eng_own;
    info->tx_sent = s_eng_cnt[0];
    info->tx_dropped = s_eng_cnt[1];
    info->rx_cmds = s_eng_cnt[2];
    info->rx_pin_rejected = s_eng_cnt[3];
}

void car_sim_on_rx(const CAN_msg_t *msg)
{
    (void)msg;
}

static void step_cmd(const char *line)
{
    s_calls[0] = '\0';
    if (!car_sim_cmd_process(line, out))
    {
        out("NOT_CMD");
    }
    if (s_calls[0] != '\0')
    {
        strncat(s_out, " >> ", sizeof(s_out) - strlen(s_out) - 1U);
        strncat(s_out, s_calls, sizeof(s_out) - strlen(s_out) - 1U);
    }
}

static void step_engine(char *args)
{
    static const char *const counters[] = { "tx", "drop", "rx", "rej" };

    for (char *tok = strtok(args, " "); tok != NULL; tok = strtok(NULL, " "))
    {
        char *eq = strchr(tok, '=');
        if (eq == NULL)
        {
            continue;
        }
        *eq = '\0';
        const char *v = eq + 1;
        if (strcmp(tok, "dev") == 0)
        {
            s_eng_dev = (strcmp(v, "-") == 0) ? CAR_SIM_DEV_PARSER_UNKNOWN : (uint16_t)strtoul(v, NULL, 10);
        }
        else if (strcmp(tok, "own") == 0)
        {
            s_eng_own = strtoul(v, NULL, 10) != 0U;
        }
        for (unsigned i = 0U; i < 4U; i++)
        {
            if (strcmp(tok, counters[i]) == 0)
            {
                s_eng_cnt[i] = (uint32_t)strtoul(v, NULL, 10);
            }
        }
    }
    out("ENGINE");
}

/* --- Harness profiles ---------------------------------------------------------------------------- */

static const can_config_t s_bus_500k = { CAN_MODE_CLASSIC, CAN_BAUDRATE_500K, CAN_BAUDRATE_CUSTOM };

/** @brief Every encoding: bits with init bits, BE / LE fields, mul / div / offset / max. */
static const sim_signal_t s_test_sig_a[] = {
    { .sig = SIG_ACC,         .enc = SIM_ENC_BIT,     .byte = 0, .mask = 0x80 },
    { .sig = SIG_LIGHTS,      .enc = SIM_ENC_BIT,     .byte = 0, .mask = 0x01 },
    { .sig = SIG_RPM,         .enc = SIM_ENC_UINT_BE, .byte = 1, .len = 2, .mul = 8 },
    { .sig = SIG_SPEED,       .enc = SIM_ENC_UINT_LE, .byte = 3, .len = 2, .mul = 160, .max = 0xFEFF },
    { .sig = SIG_ENGINE_TEMP, .enc = SIM_ENC_UINT_BE, .byte = 5, .len = 1, .offset = 40 },
    { .sig = SIG_FUEL,        .enc = SIM_ENC_UINT_BE, .byte = 6, .len = 1, .div = 2 },
    { .sig = SIG_LOCKED,      .enc = SIM_ENC_BIT,     .byte = 7, .mask = 0x04 },
    { .sig = SIG_LOCKED,      .enc = SIM_ENC_BIT,     .byte = 7, .mask = 0x80 },
};

/** @brief 28-bit field shifted by 4 bits, low nibble from init. */
static const sim_signal_t s_test_sig_odo[] = {
    { .sig = SIG_ODOMETER, .enc = SIM_ENC_UINT_BE, .byte = 0, .len = 4, .shift = 4, .div = 10, .max = 0x0FFFFFFE },
};

static const sim_signal_t s_test_sig_brake[] = {
    { .sig = SIG_BRAKE_PEDAL, .enc = SIM_ENC_UINT_BE, .byte = 6, .len = 2, .mul = 100 },
};

static uint8_t s_hook_cnt = 0U;

static void test_hook(const vehicle_state_t *vs, uint8_t data[CAR_SIM_DLC_MAX])
{
    data[0] = (uint8_t)(vs->btn & 0xFFU);
    data[3] = s_hook_cnt++;
}

static void test_on_select(void)
{
    s_hook_cnt = 0U;
}

static const sim_frame_t s_test_tx[] = {
    { .id = 0x100, .dlc = 8, .period_ms = 100, .init = { 0x41 },
      .signals = s_test_sig_a, .num_signals = sizeof(s_test_sig_a) / sizeof(s_test_sig_a[0]) },
    { .id = 0x438, .dlc = 8, .period_ms = 100, .init = { 0, 0, 0, 0x0A },
      .signals = s_test_sig_odo, .num_signals = 1 },
    { .id = 0x206, .dlc = 8, .period_ms = 100, .init = { 0, 0, 0, 0, 0x04 },
      .signals = s_test_sig_brake, .num_signals = 1 },
    { .id = 0x18FEF100, .ext = true, .dlc = 4, .period_ms = 1000, .encode = test_hook },
};

static sim_action_t test_custom(const CAN_msg_t *msg)
{
    return (msg->data[0] == 1U) ? SIM_ACT_LOCK : SIM_ACT_NONE;
}

static const sim_cmd_t s_test_rx[] = {
    { .id = 0x46F, .match = { 0x32, 0, 0, 0x18, 0x04, 0x84, 0x07, 0x30 },
      .match_mask = { 0xFF, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, .pin_byte = 1, .action = SIM_ACT_UNLOCK },
    { .id = 0x46F, .match = { 0x32, 0, 0, 0x10, 0x04, 0x84, 0x07, 0x30 },
      .match_mask = { 0xFF, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, .pin_byte = 1, .action = SIM_ACT_LOCK },
    { .id = 0x01F2F3F0, .ext = true, .match = { 0, 0, 0x01 }, .match_mask = { 0, 0, 0x01 }, .pin_byte = -1,
      .action = SIM_ACT_LOCK },
    { .id = 0x01F2F3F0, .ext = true, .match = { 0 }, .match_mask = { 0, 0, 0x01 }, .pin_byte = -1,
      .action = SIM_ACT_UNLOCK },
    { .id = 0x123, .pin_byte = -1, .action = SIM_ACT_LOG },
    { .id = 0x124, .pin_byte = -1, .action = SIM_ACT_CUSTOM, .on_rx = test_custom },
};

static const sim_fob_t s_test_fob = {
    .id = 0x46F, .dlc = 8, .pin_byte = 1, .default_pin = 0x1234,
    .unlock = { 0x32, 0, 0, 0x18, 0x04, 0x84, 0x07, 0x30 },
    .lock = { 0x32, 0, 0, 0x10, 0x04, 0x84, 0x07, 0x30 },
};

static const uint16_t s_test_dev[] = { 900 };

static const car_sim_profile_t s_test = {
    .key = "TEST", .name = "harness test car", .dev_parsers = s_test_dev, .num_dev_parsers = 1,
    .bus_config = &s_bus_500k, .tx = s_test_tx, .num_tx = sizeof(s_test_tx) / sizeof(s_test_tx[0]),
    .rx = s_test_rx, .num_rx = sizeof(s_test_rx) / sizeof(s_test_rx[0]),
    .rpm_max = 8000, .rpm_step = 1, .idle_rpm = 800, .fob = &s_test_fob, .on_select = test_on_select,
};

/** @brief 13 frames of 100 ms, like Renault Arkana stage 1 (scheduler test). */
static const sim_frame_t s_sched13_tx[] = {
    { .id = 0x200, .dlc = 8 }, { .id = 0x201, .dlc = 8 }, { .id = 0x202, .dlc = 8 }, { .id = 0x203, .dlc = 8 },
    { .id = 0x204, .dlc = 8 }, { .id = 0x205, .dlc = 8 }, { .id = 0x206, .dlc = 8 }, { .id = 0x207, .dlc = 8 },
    { .id = 0x208, .dlc = 8 }, { .id = 0x209, .dlc = 8 }, { .id = 0x20A, .dlc = 8 }, { .id = 0x20B, .dlc = 8 },
    { .id = 0x20C, .dlc = 8 },
};

static const car_sim_profile_t s_sched13 = {
    .key = "SCHED13", .name = "13 frames", .bus_config = &s_bus_500k, .tx = s_sched13_tx,
    .num_tx = sizeof(s_sched13_tx) / sizeof(s_sched13_tx[0]), .rpm_max = 8000, .rpm_step = 1, .idle_rpm = 800,
};

/* Invalid profiles for CHECKP */
static const sim_signal_t s_bad_sig[] = {
    { .sig = SIG_RPM, .enc = SIM_ENC_UINT_BE, .byte = 7, .len = 2 },
};
static const sim_frame_t s_bad_sig_tx[] = { { .id = 0x300, .dlc = 8, .signals = s_bad_sig, .num_signals = 1 } };
static const sim_frame_t s_bad_dup_tx[] = { { .id = 0x300, .dlc = 8 }, { .id = 0x300, .dlc = 8 } };
static const sim_frame_t s_bad_period_tx[] = { { .id = 0x300, .dlc = 8, .period_ms = 105 } };
static const sim_frame_t s_bad_id_tx[] = { { .id = 0x800, .dlc = 8 } };
static const sim_cmd_t s_bad_pin_rx[] = { { .id = 0x46F, .pin_byte = 7, .action = SIM_ACT_LOCK } };

static const car_sim_profile_t s_bad[] = {
    { .key = "BAD_SIG", .name = "", .bus_config = &s_bus_500k, .tx = s_bad_sig_tx, .num_tx = 1, .rpm_step = 1 },
    { .key = "BAD_DUP", .name = "", .bus_config = &s_bus_500k, .tx = s_bad_dup_tx, .num_tx = 2, .rpm_step = 1 },
    { .key = "BAD_PERIOD", .name = "", .bus_config = &s_bus_500k, .tx = s_bad_period_tx, .num_tx = 1, .rpm_step = 1 },
    { .key = "BAD_ID", .name = "", .bus_config = &s_bus_500k, .tx = s_bad_id_tx, .num_tx = 1, .rpm_step = 1 },
    { .key = "BAD_NOTX", .name = "", .bus_config = &s_bus_500k, .tx = s_bad_id_tx, .num_tx = 0, .rpm_step = 1 },
    { .key = "BAD_RPM", .name = "", .bus_config = &s_bus_500k, .tx = s_sched13_tx, .num_tx = 1, .rpm_max = 750,
      .rpm_step = 3, .idle_rpm = 800 },
    { .key = "BAD_RXPIN", .name = "", .bus_config = &s_bus_500k, .tx = s_sched13_tx, .num_tx = 1, .rx = s_bad_pin_rx,
      .num_rx = 1, .rpm_step = 1 },
    { .key = "BAD_BUS", .name = "", .tx = s_sched13_tx, .num_tx = 1, .rpm_step = 1 },
};

static const car_sim_profile_t *harness_profile(const char *key)
{
    if (strcmp(key, "TEST") == 0)
    {
        return &s_test;
    }
    if (strcmp(key, "SCHED13") == 0)
    {
        return &s_sched13;
    }
    for (size_t i = 0U; i < sizeof(s_bad) / sizeof(s_bad[0]); i++)
    {
        if (strcmp(key, s_bad[i].key) == 0)
        {
            return &s_bad[i];
        }
    }
    return NULL;
}

/* --- Formatting ---------------------------------------------------------------------------------- */

static void format_frame(const CAN_msg_t *msg, char *buf, size_t size)
{
    const bool ext = (msg->ident & CAN_EFF_FLAG) != 0U;
    int pos = snprintf(buf, size, ext ? "%08lX#" : "%03lX#",
                       (unsigned long)(msg->ident & (ext ? CAN_EFF_MASK : CAN_SFF_MASK)));
    for (uint8_t i = 0U; i < msg->len && pos > 0 && (size_t)pos + 3U < size; i++)
    {
        pos += snprintf(&buf[pos], size - (size_t)pos, "%02X", msg->data[i]);
    }
}

static const char *action_name(sim_action_t a)
{
    switch (a)
    {
        case SIM_ACT_UNLOCK: return "UNLOCK";
        case SIM_ACT_LOCK:   return "LOCK";
        case SIM_ACT_LOG:    return "LOG";
        case SIM_ACT_CUSTOM: return "CUSTOM";
        case SIM_ACT_NONE:
        default:             return "NONE";
    }
}

/* --- Steps --------------------------------------------------------------------------------------- */

static void step_eff(char *args)
{
    for (char *tok = strtok(args, " "); tok != NULL; tok = strtok(NULL, " "))
    {
        char *eq = strchr(tok, '=');
        if (eq == NULL)
        {
            continue;
        }
        *eq = '\0';
        const char *v = eq + 1;
        if (strcmp(tok, "btn") == 0)        s_eff.btn = (uint16_t)strtoul(v, NULL, 16);
        else if (strcmp(tok, "fuel") == 0)  s_eff.fuel = (uint8_t)strtoul(v, NULL, 10);
        else if (strcmp(tok, "rpm") == 0)   s_eff.rpm = (uint16_t)strtoul(v, NULL, 10);
        else if (strcmp(tok, "speed") == 0) s_eff.speed_kmh = (uint16_t)strtoul(v, NULL, 10);
        else if (strcmp(tok, "odo") == 0)   s_eff.odometer_m = (uint32_t)strtoul(v, NULL, 10);
        else if (strcmp(tok, "high") == 0)  s_eff.high_beam = strtoul(v, NULL, 10) != 0U;
        else if (strcmp(tok, "brake") == 0) s_eff.brake_pedal = strtoul(v, NULL, 10) != 0U;
        else if (strcmp(tok, "temp") == 0)  s_eff.engine_temp_c = (int16_t)strtol(v, NULL, 10);
    }

    vehicle_state_t vs;
    char text[512];
    vehicle_state_build(&s_eff, &vs);
    snprintf(text, sizeof(text),
             "acc=%d door_drv=%d door_other=%d lights=%d locked=%d hazard=%d hood=%d handbrake=%d high=%d "
             "brake=%d ss=%d rpm=%u rpm_in=%u speed=%u fuel=%u odo=%lu temp=%d btn=%03X",
             vs.acc, vs.door_drv, vs.door_other, vs.lights, vs.locked, vs.hazard, vs.hood, vs.handbrake,
             vs.light_high, vs.brake_pedal, vs.ss_button, (unsigned)vs.rpm, (unsigned)vs.rpm_in,
             (unsigned)vs.speed_kmh, (unsigned)vs.fuel_pct, (unsigned long)vs.odometer_m, (int)vs.engine_temp_c,
             (unsigned)vs.btn);
    out(text);
}

static void step_sim(const char *key)
{
    const car_sim_profile_t *p = harness_profile(key);
    if (p == NULL)
    {
        p = car_sim_get(car_sim_find_by_key(key));
    }
    if (p == NULL)
    {
        out("NOT_FOUND");
        return;
    }

    char text[256];
    s_sim = p;
    if (p->on_select != NULL)
    {
        p->on_select();
    }
    car_sim_schedule_init(&s_sched, p->tx, p->num_tx);
    if (p->fob != NULL)
    {
        s_pin = p->fob->default_pin;
    }
    snprintf(text, sizeof(text), "SIM %s \"%s\" tx=%u rx=%u fob=%d rpm=%u/%u/%u vsig=%d", p->key, p->name,
             (unsigned)p->num_tx, (unsigned)p->num_rx, p->fob != NULL, (unsigned)p->rpm_max,
             (unsigned)p->rpm_step, (unsigned)p->idle_rpm, car_sim_profile_uses_vehicle_signals(p));
    out(text);
}

static void step_encode(void)
{
    vehicle_state_t vs;
    vehicle_state_build(&s_eff, &vs);
    for (uint8_t i = 0U; i < s_sim->num_tx; i++)
    {
        CAN_msg_t msg;
        char text[64];
        car_sim_encode_frame(&s_sim->tx[i], &vs, &msg);
        format_frame(&msg, text, sizeof(text));
        out(text);
    }
}

static void step_rx(char *args)
{
    CAN_msg_t msg;
    char *id_text = strtok(args, " ");

    if (id_text == NULL)
    {
        out("NO_ID");
        return;
    }
    memset(&msg, 0, sizeof(msg));
    const unsigned long id = strtoul(id_text, NULL, 16);
    msg.ident = (strlen(id_text) == 8U) ? CAN_msg_set_extended_id((uint32_t)id) : CAN_msg_set_standard_id((uint32_t)id);
    for (char *tok = strtok(NULL, " "); tok != NULL && msg.len < CAR_SIM_DLC_MAX; tok = strtok(NULL, " "))
    {
        msg.data[msg.len++] = (uint8_t)strtoul(tok, NULL, 16);
    }

    sim_match_t m;
    char text[128];
    if (!car_sim_match_cmd(s_sim->rx, s_sim->num_rx, &msg, s_pin, &m))
    {
        out("NO_MATCH");
        return;
    }
    int pos = snprintf(text, sizeof(text), "MATCH act=%s", action_name(m.action));
    if (m.has_pin)
    {
        snprintf(&text[pos], sizeof(text) - (size_t)pos, " pin=%04X%s", (unsigned)m.pin_rx,
                 m.pin_mismatch ? " MISMATCH" : "");
    }
    out(text);
}

static void step_fob(char *args)
{
    char *dir = strtok(args, " ");
    char *pin = strtok(NULL, " ");
    CAN_msg_t msg;
    char text[64];

    if (dir == NULL || pin == NULL || !car_sim_build_fob(s_sim->fob, strcmp(dir, "OTWORZ") == 0,
                                                         (uint16_t)strtoul(pin, NULL, 16), &msg))
    {
        out("NO_FOB");
        return;
    }
    format_frame(&msg, text, sizeof(text));
    out(text);
}

static void step_sched(uint32_t tick_ms)
{
    uint8_t due[CAR_SIM_TX_MAX];
    const uint8_t n = car_sim_schedule_due(&s_sched, tick_ms, due, CAR_SIM_TX_MAX);
    for (uint8_t i = 0U; i < n; i++)
    {
        char text[16];
        const sim_frame_t *f = &s_sim->tx[due[i]];
        snprintf(text, sizeof(text), f->ext ? "%08lX" : "%03lX", (unsigned long)f->id);
        out(text);
    }
}

static void step_schedstat(uint32_t total_ms)
{
    unsigned counts[CAR_SIM_TX_MAX] = { 0 };
    unsigned max_tick = 0U;
    uint8_t due[CAR_SIM_TX_MAX];
    char text[512];

    for (uint32_t t = 0U; t < total_ms; t += CAR_SIM_TICK_MS)
    {
        const uint8_t n = car_sim_schedule_due(&s_sched, t, due, CAR_SIM_TX_MAX);
        max_tick = (n > max_tick) ? n : max_tick;
        for (uint8_t i = 0U; i < n; i++)
        {
            counts[due[i]]++;
        }
    }
    int pos = snprintf(text, sizeof(text), "frames=%u max_per_tick=%u counts=", (unsigned)s_sched.num, max_tick);
    for (uint8_t i = 0U; i < s_sched.num && pos > 0 && (size_t)pos + 8U < sizeof(text); i++)
    {
        pos += snprintf(&text[pos], sizeof(text) - (size_t)pos, i == 0U ? "%u" : ",%u", counts[i]);
    }
    out(text);
}

int main(void)
{
    static char line[1024];

    s_eff.odometer_m = IO_ODOMETER_DEFAULT_KM * 1000U;
    s_eff.engine_temp_c = IO_ENGINE_TEMP_DEFAULT;

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';
        s_out[0] = '\0';

        if (strncmp(line, "EFF", 3) == 0)
        {
            step_eff(line + 3);
        }
        else if (strncmp(line, "SIM ", 4) == 0)
        {
            step_sim(line + 4);
        }
        else if (strcmp(line, "CHECK") == 0 || strcmp(line, "CHECKP") == 0)
        {
            char err[160];
            const bool ok = (line[5] == 'P') ? car_sim_profile_check(s_sim, err, sizeof(err))
                                             : car_sim_registry_check(err, sizeof(err));
            out(ok ? "OK" : "ERR");
            if (!ok)
            {
                out(err);
            }
        }
        else if (strncmp(line, "MAP ", 4) == 0)
        {
            const car_sim_profile_t *p = car_sim_get(car_sim_find_by_dev_parser((uint16_t)strtoul(line + 4, NULL, 10)));
            out(p != NULL ? p->key : "NULL");
        }
        else if (strncmp(line, "KEY ", 4) == 0)
        {
            const car_sim_profile_t *p = car_sim_get(car_sim_find_by_key(line + 4));
            out(p != NULL ? p->key : "NOT_FOUND");
        }
        else if (strncmp(line, "CMD ", 4) == 0)
        {
            step_cmd(line + 4);
        }
        else if (strncmp(line, "ENGINE", 6) == 0)
        {
            step_engine(line + 6);
        }
        else if (s_sim == NULL)
        {
            out("NO_SIM");
        }
        else if (strcmp(line, "ENCODE") == 0)
        {
            step_encode();
        }
        else if (strncmp(line, "PIN ", 4) == 0)
        {
            s_pin = (uint16_t)strtoul(line + 4, NULL, 16);
            out("PIN");
        }
        else if (strncmp(line, "RX ", 3) == 0)
        {
            step_rx(line + 3);
        }
        else if (strncmp(line, "FOB ", 4) == 0)
        {
            step_fob(line + 4);
        }
        else if (strncmp(line, "SCHED ", 6) == 0)
        {
            step_sched((uint32_t)strtoul(line + 6, NULL, 10));
        }
        else if (strncmp(line, "SCHEDSTAT ", 10) == 0)
        {
            step_schedstat((uint32_t)strtoul(line + 10, NULL, 10));
        }
        else
        {
            out("UNKNOWN");
        }

        printf("%s\n", s_out[0] != '\0' ? s_out : "-");
    }
    return 0;
}
