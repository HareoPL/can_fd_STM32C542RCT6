/**
 * @file sim_renault_arkana.c
 * @brief Car simulator Renault Arkana (device parser 196): stage 1 frames read by the device
 *        parser, carsharing open / close commands with the v-box PIN and the key fob frame
 *        (docs/car_sim/car_sim_tasks.md, sections 3.1 and 3.3).
 *
 * Only IDs, bit masks and formulas of the device parser are used. Every frame is 11-bit, DLC 8,
 * every 100 ms; unknown bytes are 0. Rules kept by the data below:
 * - 0x5BB: only the low beam bit [4] & 0x10 (D6); [4] & 0x08 stays 0, because the device parser
 *   reads both bits set at once as "both lights off",
 * - locked = 0x418 [2] & 0x04 and 0x46B [2] & 0x01, [2] & 0x02, [7] & 0x80, all from SW4 (D7),
 * - odometer 0x438: 28-bit field of 10 m units above the low nibble of [3], never FF FF FF F0
 *   (the "no data" code),
 * - speed 0x41C: never [0] = 0xFF (the "no data" code),
 * - RPM 0 with the ignition off (D13, vehicle_state_t::rpm).
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#include "sim_renault_arkana.h"
#include <stddef.h>

/** @brief CAN ID of the carsharing commands and of the key fob frame. */
#define ARKANA_CMD_ID          0x46FU
/** @brief Command byte [3] of "open" (cmd_open, also teaches the device the PIN). */
#define ARKANA_CMD_OPEN        0x18U
/** @brief Command byte [3] of "close" (cmd_close, emergency close). */
#define ARKANA_CMD_CLOSE       0x10U
/** @brief First byte of the PIN in the command frames ([1] = PIN_H, [2] = PIN_L). */
#define ARKANA_PIN_BYTE        1
/** @brief Default v-box PIN of the key fob (D8). */
#define ARKANA_DEFAULT_PIN     0x1234U
/** @brief Largest raw odometer value: 0x0FFFFFFF << 4 = FF FF FF F0 is the "no data" code. */
#define ARKANA_ODOMETER_RAW_MAX 0x0FFFFFFEU
/** @brief Largest raw speed value: [0] = 0xFF is the "no data" code. */
#define ARKANA_SPEED_RAW_MAX   0xFEFFU
/** @brief Brake pedal pressure sent when pressed (the device parser reads > 5 as pressed). */
#define ARKANA_BRAKE_PRESSED   100U

/* --- Signals --------------------------------------------------------------- */

/** @brief 0x46C: ignition [6] & 0x80. */
static const sim_signal_t s_sig_46c[] = {
    { .sig = SIG_ACC, .enc = SIM_ENC_BIT, .byte = 6, .mask = 0x80 },
};

/** @brief 0x0A1: RPM [5..6] BE, 0.125 rpm per bit. */
static const sim_signal_t s_sig_0a1[] = {
    { .sig = SIG_RPM, .enc = SIM_ENC_UINT_BE, .byte = 5, .len = 2, .mul = 8 },
};

/** @brief 0x41C: speed [0..1] BE, 0.00625 km/h per bit. */
static const sim_signal_t s_sig_41c[] = {
    { .sig = SIG_SPEED, .enc = SIM_ENC_UINT_BE, .byte = 0, .len = 2, .mul = 160, .max = ARKANA_SPEED_RAW_MAX },
};

/** @brief 0x46A: driver door [1] & 0x04, other doors [1] & 0x01, high beam [3] & 0x40. */
static const sim_signal_t s_sig_46a[] = {
    { .sig = SIG_DOOR_DRV,   .enc = SIM_ENC_BIT, .byte = 1, .mask = 0x04 },
    { .sig = SIG_DOOR_OTHER, .enc = SIM_ENC_BIT, .byte = 1, .mask = 0x01 },
    { .sig = SIG_LIGHT_HIGH, .enc = SIM_ENC_BIT, .byte = 3, .mask = 0x40 },
};

/** @brief 0x5BB: low beam [4] & 0x10 (SW3, D6); the position light bit 0x08 is never set. */
static const sim_signal_t s_sig_5bb[] = {
    { .sig = SIG_LIGHTS, .enc = SIM_ENC_BIT, .byte = 4, .mask = 0x10 },
};

/** @brief 0x418: lock 1 [2] & 0x04. */
static const sim_signal_t s_sig_418[] = {
    { .sig = SIG_LOCKED, .enc = SIM_ENC_BIT, .byte = 2, .mask = 0x04 },
};

/** @brief 0x46B: lock 2 = [2] & 0x01, [2] & 0x02 and [7] & 0x80 at once. */
static const sim_signal_t s_sig_46b[] = {
    { .sig = SIG_LOCKED, .enc = SIM_ENC_BIT, .byte = 2, .mask = 0x01 },
    { .sig = SIG_LOCKED, .enc = SIM_ENC_BIT, .byte = 2, .mask = 0x02 },
    { .sig = SIG_LOCKED, .enc = SIM_ENC_BIT, .byte = 7, .mask = 0x80 },
};

/** @brief 0x47F: fuel level [3] (assumed %, to be verified in T12), handbrake [4] & 0x02. */
static const sim_signal_t s_sig_47f[] = {
    { .sig = SIG_FUEL,      .enc = SIM_ENC_UINT_BE, .byte = 3, .len = 1 },
    { .sig = SIG_HANDBRAKE, .enc = SIM_ENC_BIT,     .byte = 4, .mask = 0x02 },
};

/** @brief 0x47C: hood [4] & 0x40. */
static const sim_signal_t s_sig_47c[] = {
    { .sig = SIG_HOOD, .enc = SIM_ENC_BIT, .byte = 4, .mask = 0x40 },
};

/** @brief 0x206: brake pedal pressure [6..7] BE (> 5 = pressed). */
static const sim_signal_t s_sig_206[] = {
    { .sig = SIG_BRAKE_PEDAL, .enc = SIM_ENC_UINT_BE, .byte = 6, .len = 2, .mul = ARKANA_BRAKE_PRESSED },
};

/** @brief 0x438: odometer [0..3] BE, 28 bits of 10 m above the low nibble of [3]. */
static const sim_signal_t s_sig_438[] = {
    { .sig = SIG_ODOMETER, .enc = SIM_ENC_UINT_BE, .byte = 0, .len = 4, .shift = 4, .div = 10,
      .max = ARKANA_ODOMETER_RAW_MAX },
};

/** @brief 0x417: engine temperature [0] - 40 degC. */
static const sim_signal_t s_sig_417[] = {
    { .sig = SIG_ENGINE_TEMP, .enc = SIM_ENC_UINT_BE, .byte = 0, .len = 1, .offset = 40 },
};

/* --- Frames ---------------------------------------------------------------- */

/** @brief Helper: one 11-bit, 8-byte, 100 ms frame with a signal table. */
#define ARKANA_FRAME(frame_id, sigs, ...)                                                            \
    {                                                                                                \
        .id = (frame_id), .ext = false, .dlc = 8U, .period_ms = 100U, .init = { __VA_ARGS__ },       \
        .signals = (sigs), .num_signals = (uint8_t)(sizeof(sigs) / sizeof((sigs)[0]))                \
    }

/** @brief Stage 1 frames (section 3.1). */
static const sim_frame_t s_tx[] = {
    ARKANA_FRAME(0x46C, s_sig_46c, 0),
    ARKANA_FRAME(0x0A1, s_sig_0a1, 0),
    ARKANA_FRAME(0x41C, s_sig_41c, 0),
    ARKANA_FRAME(0x46A, s_sig_46a, 0),
    ARKANA_FRAME(0x5BB, s_sig_5bb, 0),
    ARKANA_FRAME(0x418, s_sig_418, 0),
    ARKANA_FRAME(0x46B, s_sig_46b, 0),
    ARKANA_FRAME(0x47F, s_sig_47f, 0),
    ARKANA_FRAME(0x47C, s_sig_47c, 0),
    /* [4] & 0x04 set: no ESP event in the device parser */
    ARKANA_FRAME(0x206, s_sig_206, 0, 0, 0, 0, 0x04),
    ARKANA_FRAME(0x438, s_sig_438, 0),
    ARKANA_FRAME(0x417, s_sig_417, 0),
    /* Check engine [2] & 0x10, oil [3] & 0x40, charging [2] & 0x02: always 0 */
    { .id = 0x3AA, .ext = false, .dlc = 8U, .period_ms = 100U },
};

/* --- LX commands and key fob ----------------------------------------------- */

/** @brief Carsharing commands 0x46F 32 PIN_H PIN_L <18|10> 04 84 07 30 (section 3.3). */
static const sim_cmd_t s_rx[] = {
    {
        .id         = ARKANA_CMD_ID,
        .ext        = false,
        .match      = { 0x32U, 0x00U, 0x00U, ARKANA_CMD_OPEN, 0x04U, 0x84U, 0x07U, 0x30U },
        .match_mask = { 0xFFU, 0x00U, 0x00U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU },
        .pin_byte   = ARKANA_PIN_BYTE,
        .action     = SIM_ACT_UNLOCK,
    },
    {
        .id         = ARKANA_CMD_ID,
        .ext        = false,
        .match      = { 0x32U, 0x00U, 0x00U, ARKANA_CMD_CLOSE, 0x04U, 0x84U, 0x07U, 0x30U },
        .match_mask = { 0xFFU, 0x00U, 0x00U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU },
        .pin_byte   = ARKANA_PIN_BYTE,
        .action     = SIM_ACT_LOCK,
    },
};

/** @brief Key fob: the same frames as the commands, with the v-box PIN (PILOT#, D8). */
static const sim_fob_t s_fob = {
    .id          = ARKANA_CMD_ID,
    .ext         = false,
    .dlc         = 8U,
    .unlock      = { 0x32U, 0x00U, 0x00U, ARKANA_CMD_OPEN, 0x04U, 0x84U, 0x07U, 0x30U },
    .lock        = { 0x32U, 0x00U, 0x00U, ARKANA_CMD_CLOSE, 0x04U, 0x84U, 0x07U, 0x30U },
    .pin_byte    = (uint8_t)ARKANA_PIN_BYTE,
    .default_pin = ARKANA_DEFAULT_PIN,
};

/* --- Profile --------------------------------------------------------------- */

/** @brief Device parser numbers. */
static const uint16_t s_dev_parsers[] = { 196U };

/** @brief Bus configuration: CAN1 Classic 500 kbps. */
static const can_config_t s_bus_config = {
    .mode            = CAN_MODE_CLASSIC,
    .baudrate_normal = CAN_BAUDRATE_500K,
    .baudrate_FD     = CAN_BAUDRATE_CUSTOM
};

/** @brief Immutable Renault Arkana profile. */
static const car_sim_profile_t s_profile = {
    .key             = "ARKANA",
    .name            = "Renault Arkana",
    .dev_parsers     = s_dev_parsers,
    .num_dev_parsers = (uint8_t)(sizeof(s_dev_parsers) / sizeof(s_dev_parsers[0])),
    .bus_config      = &s_bus_config,
    .tx              = s_tx,
    .num_tx          = (uint8_t)(sizeof(s_tx) / sizeof(s_tx[0])),
    .rx              = s_rx,
    .num_rx          = (uint8_t)(sizeof(s_rx) / sizeof(s_rx[0])),
    .rpm_max         = 8000U,
    .rpm_step        = 1U,
    .idle_rpm        = 800U,
    .fob             = &s_fob,
    .on_select       = NULL
};

const car_sim_profile_t *sim_renault_arkana_profile_get(void)
{
    return &s_profile;
}
