/**
 * @file car_sim.c
 * @brief Car simulator engine: car_sim task (vehicle state, cyclic CAN1 frames), LX commands
 *        received on CAN1, automatic and manual selection of the car simulator.
 *
 * Every 10 ms the task resolves the effective inputs (io_manager + io_sim), advances the odometer
 * with the speed, builds the vehicle state and queues the frames due in this tick. Requests from
 * other tasks (selection, device parser number, mode, key fob) are stored in variables and
 * executed in the next tick, so the simulator and the CAN1 configuration are changed by this
 * task only.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-27
 */

#include "car_sim.h"
#include "car_sim_codec.h"
#include "can_parser.h"
#include "cp_boot_ota.h"
#include "cp_car_sim.h"
#include "io_manager.h"
#include "io_sim.h"
#include "safe_printf.h"
#include "tasks_config.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/** @brief Logging tag. */
#define TAG "CAR_SIM"

/** @brief Maximum wait for the CAN1 TX queue to drain before the bus is reconfigured. */
#define CAR_SIM_TX_IDLE_TIMEOUT_MS 50U
/** @brief Minimum interval between two logs of frames dropped by a full TX queue. */
#define CAR_SIM_DROP_LOG_PERIOD_MS 5000U
/** @brief The scheduler time wraps here (1 h, a multiple of every usual frame period). */
#define CAR_SIM_SCHED_WRAP_MS      3600000U
/** @brief Odometer accumulator units per metre: km/h * 10 ms = 25/9 mm, so 1 unit = 1/9 mm. */
#define CAR_SIM_ODO_UNITS_PER_M    9000U
/** @brief Odometer accumulator units per km/h and tick. */
#define CAR_SIM_ODO_UNITS_PER_TICK 25U

/** @brief No pending request (selection: CAR_SIM_COUNT). */
#define CAR_SIM_REQ_NONE           0xFFFFFFFFU
/** @brief Flag of an overridden PIN in s_pin_override (PIN in the low 16 bits, one atomic word). */
#define CAR_SIM_PIN_OVERRIDE_FLAG  0x00010000U

/** @brief Key fob request. */
typedef enum
{
    FOB_REQ_NONE = 0, /**< No request */
    FOB_REQ_UNLOCK,   /**< PILOT#OTWORZ */
    FOB_REQ_LOCK      /**< PILOT#ZAMKNIJ */
} fob_req_t;

static can_instance_t *s_can1 = NULL;
static bool s_own_can1 = true;
static TaskHandle_t s_task = NULL;

/** @brief Active profile, read by can_rx_task (profiles are static const, a stale pointer stays valid). */
static const car_sim_profile_t *volatile s_active = NULL;
static volatile car_sim_id_t s_active_id = CAR_SIM_ID_V_BOX;
static volatile car_sim_mode_t s_mode = CAR_SIM_MODE_AUTO;
static volatile uint16_t s_last_dev_parser = CAR_SIM_DEV_PARSER_UNKNOWN;
/** @brief PIN override: bit 16 = overridden, bits 0..15 = PIN (written by UART commands, read by can_rx_task). */
static volatile uint32_t s_pin_override = 0U;

/* Requests from other tasks, executed by the car_sim task */
static volatile uint32_t s_req_select = CAR_SIM_REQ_NONE;
static volatile uint32_t s_req_dev_parser = CAR_SIM_REQ_NONE;
static volatile uint32_t s_req_mode = CAR_SIM_REQ_NONE;
static volatile fob_req_t s_req_fob = FOB_REQ_NONE;

/* Counters */
static volatile uint32_t s_tx_sent = 0U;
static volatile uint32_t s_tx_dropped = 0U;
static volatile uint32_t s_rx_cmds = 0U;
static volatile uint32_t s_rx_pin_rejected = 0U;

/* car_sim task state */
static car_sim_schedule_t s_sched;
static uint32_t s_tick_ms = 0U;
static uint32_t s_odo_acc = 0U;

/* --- Helpers --------------------------------------------------------------- */

/** @brief Takes a pending request word and clears it. */
static uint32_t take_request(volatile uint32_t *req)
{
    taskENTER_CRITICAL();
    const uint32_t value = *req;
    *req = CAR_SIM_REQ_NONE;
    taskEXIT_CRITICAL();
    return value;
}

/** @brief Returns the effective v-box PIN for a profile. */
static uint16_t effective_pin(const car_sim_profile_t *p, uint32_t pin_override)
{
    if ((pin_override & CAR_SIM_PIN_OVERRIDE_FLAG) != 0U)
    {
        return (uint16_t)(pin_override & 0xFFFFU);
    }
    return (p != NULL && p->fob != NULL) ? p->fob->default_pin : 0U;
}

/** @brief Returns the CAN ID of a received frame without the format flag. */
static uint32_t rx_id(const CAN_msg_t *msg, bool *ext)
{
    *ext = (msg->ident & CAN_EFF_FLAG) != 0U;
    return msg->ident & (*ext ? CAN_EFF_MASK : CAN_SFF_MASK);
}

/** @brief Logs the active simulator. */
static void log_selected(const car_sim_profile_t *p)
{
    char parsers[40];
    size_t pos = 0U;

    parsers[0] = '\0';
    for (uint8_t i = 0U; (p->dev_parsers != NULL) && (i < p->num_dev_parsers) && (pos + 8U < sizeof(parsers)); i++)
    {
        pos += (size_t)snprintf(&parsers[pos], sizeof(parsers) - pos, (i == 0U) ? "%u" : ", %u",
                                (unsigned)p->dev_parsers[i]);
    }

    safe_printf(TAG, "symulator: %s (%s%s, CAN1 %luk%s)\r\n", p->name,
                (pos > 0U) ? "parser urządzenia " : "bez parsera urządzenia", parsers,
                (unsigned long)p->bus_config->baudrate_normal / 1000UL,
                (p->bus_config->mode == CAN_MODE_CLASSIC) ? "" : " FD");
}

/**
 * @brief Applies a profile to CAN1: bus configuration and filters (cp_car_sim), BOOT_OTA filters (D9).
 * @return true on success.
 */
static bool apply_can1(const car_sim_profile_t *p)
{
    return cp_car_sim_set_profile(p) &&
           can_parser_select(s_can1, CAN_PARSER_ID_CAR_SIM) &&
           can_parser_attach_boot_ota(s_can1);
}

/**
 * @brief Selects a simulator (car_sim task or init only). On a CAN1 configuration error the
 *        previous simulator is restored.
 * @param[in] id Simulator.
 * @param[in] wait_tx Drain the CAN1 TX queue first (false before the scheduler starts).
 * @return true on success.
 */
static bool select_sim(car_sim_id_t id, bool wait_tx)
{
    const car_sim_profile_t *p = car_sim_get(id);
    const car_sim_profile_t *prev = s_active;

    if (p == NULL)
    {
        return false;
    }

    if (s_own_can1)
    {
        if (wait_tx)
        {
            (void)can_manager_waitTxIdle(s_can1, CAR_SIM_TX_IDLE_TIMEOUT_MS);
        }
        /* Section 4.3: the active profile changes before can_parser_select() */
        s_active = p;
        if (!apply_can1(p))
        {
            s_active = prev;
            safe_printf(TAG, "ERROR: konfiguracja CAN1 dla symulatora %s nieudana, powrót do %s\r\n", p->key,
                        (prev != NULL) ? prev->key : "-");
            if (prev != NULL && !apply_can1(prev))
            {
                safe_printf(TAG, "ERROR: przywrócenie konfiguracji CAN1 nieudane\r\n");
            }
            return false;
        }
    }
    else
    {
        s_active = p;
    }

    s_active_id = id;
    io_sim_setRpmLimits(p->rpm_max, p->rpm_step, p->idle_rpm);
    io_sim_setVehicleSignalsUsed(car_sim_profile_uses_vehicle_signals(p));
    if (p->on_select != NULL)
    {
        p->on_select();
    }
    car_sim_schedule_init(&s_sched, p->tx, p->num_tx);
    s_tick_ms = 0U;
    log_selected(p);
    return true;
}

/** @brief Selection requested by the device parser number or by SIM#AUTO (car_sim task). */
static void apply_dev_parser(uint16_t no)
{
    const car_sim_id_t id = car_sim_find_by_dev_parser(no);

    if (id == CAR_SIM_ID_NONE)
    {
        safe_printf(TAG, "WARN: brak symulatora dla parsera urządzenia %u, nadaję ramkę CA FE 00 BA BE 00\r\n",
                    (unsigned)no);
    }
    if (id != s_active_id)
    {
        (void)select_sim(id, true);
    }
}

/** @brief Executes the pending requests (car_sim task). */
static void handle_requests(void)
{
    const uint32_t mode = take_request(&s_req_mode);
    const uint32_t sel = take_request(&s_req_select);
    const uint32_t dev = take_request(&s_req_dev_parser);

    if (mode != CAR_SIM_REQ_NONE)
    {
        s_mode = (car_sim_mode_t)mode;
        safe_printf(TAG, "tryb %s\r\n", (s_mode == CAR_SIM_MODE_AUTO) ? "AUTO" : "RECZNY");
        if (s_mode == CAR_SIM_MODE_AUTO && s_last_dev_parser != CAR_SIM_DEV_PARSER_UNKNOWN && s_own_can1)
        {
            apply_dev_parser(s_last_dev_parser);
        }
    }

    if (sel != CAR_SIM_REQ_NONE)
    {
        if (!s_own_can1)
        {
            safe_printf(TAG, "WARN: CAN1 zajęty przez test CAN FD, wybór symulatora pominięty\r\n");
        }
        else
        {
            (void)select_sim((car_sim_id_t)sel, true);
        }
    }

    if (dev != CAR_SIM_REQ_NONE)
    {
        s_last_dev_parser = (uint16_t)dev;
        if (s_mode == CAR_SIM_MODE_MANUAL)
        {
            safe_printf(TAG, "tryb RECZNY: parser urządzenia %u zapamiętany\r\n", (unsigned)dev);
        }
        else if (!s_own_can1)
        {
            safe_printf(TAG, "CAN1 zajęty przez test CAN FD: parser urządzenia %u zapamiętany\r\n", (unsigned)dev);
        }
        else
        {
            apply_dev_parser((uint16_t)dev);
        }
    }

    taskENTER_CRITICAL();
    const fob_req_t fob = s_req_fob;
    s_req_fob = FOB_REQ_NONE;
    taskEXIT_CRITICAL();

    if (fob != FOB_REQ_NONE)
    {
        const car_sim_profile_t *p = s_active;
        const bool unlock = (fob == FOB_REQ_UNLOCK);
        const uint16_t pin = effective_pin(p, s_pin_override);
        CAN_msg_t msg;

        if (p->fob != NULL && car_sim_build_fob(p->fob, unlock, pin, &msg))
        {
            const bool sent = can_manager_send(s_can1, &msg);
            safe_printf(TAG, "PILOT %s: ramka 0x%03lX z PIN-em %04X %s\r\n", unlock ? "OTWORZ" : "ZAMKNIJ",
                        (unsigned long)p->fob->id, (unsigned)pin, sent ? "nadana" : "NIE nadana (kolejka TX pełna)");
        }
        /* Like a real key fob, the fob also moves the lock actuator */
        const uint8_t target = unlock ? 0U : 1U;
        const bool queued = io_manager_setRelayStateAsync(target);
        safe_printf(TAG, "PILOT %s: przekaźnik target=%u, %s\r\n", unlock ? "OTWORZ" : "ZAMKNIJ", (unsigned)target,
                    queued ? "QUEUED" : "QUEUE FULL");
    }
}

/* --- Task ------------------------------------------------------------------ */

/**
 * @brief car_sim task: requests, vehicle state, odometer, cyclic frames (every CAR_SIM_TICK_MS).
 * @param[in] pvParameters Unused.
 */
static void car_sim_task(void *pvParameters)
{
    (void)pvParameters;

    TickType_t last_wake = xTaskGetTickCount();
    TickType_t last_drop_log = last_wake;
    uint32_t dropped_logged = 0U;
    io_sim_values_t eff;
    vehicle_state_t vs;
    uint8_t due[CAR_SIM_TX_MAX];

    safe_printf(TAG, "sterta FreeRTOS: wolne %u B, minimum od startu %u B\r\n",
                (unsigned)xPortGetFreeHeapSize(), (unsigned)xPortGetMinimumEverFreeHeapSize());

    for (;;)
    {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CAR_SIM_TICK_MS));

        handle_requests();

        uint16_t phys_btn;
        uint8_t phys_fuel;
        io_manager_getPhysInputs(&phys_btn, &phys_fuel);
        io_sim_resolve(phys_btn, phys_fuel, &eff);

        /* The odometer advances with the speed (UART mode only, IO mode has speed 0) */
        s_odo_acc += (uint32_t)eff.speed_kmh * CAR_SIM_ODO_UNITS_PER_TICK;
        if (s_odo_acc >= CAR_SIM_ODO_UNITS_PER_M)
        {
            io_sim_advanceOdometer(s_odo_acc / CAR_SIM_ODO_UNITS_PER_M);
            s_odo_acc %= CAR_SIM_ODO_UNITS_PER_M;
        }

        vehicle_state_build(&eff, &vs);

        const car_sim_profile_t *p = s_active;
        const uint8_t n = car_sim_schedule_due(&s_sched, s_tick_ms, due, CAR_SIM_TX_MAX);
        for (uint8_t i = 0U; i < n; i++)
        {
            CAN_msg_t msg;
            car_sim_encode_frame(&p->tx[due[i]], &vs, &msg);
            if (can_manager_send(s_can1, &msg))
            {
                s_tx_sent++;
            }
            else
            {
                s_tx_dropped++;
            }
        }

        s_tick_ms += CAR_SIM_TICK_MS;
        if (s_tick_ms >= CAR_SIM_SCHED_WRAP_MS)
        {
            s_tick_ms = 0U;
        }

        /* Full TX queue: one log per CAR_SIM_DROP_LOG_PERIOD_MS, not one per frame */
        if (s_tx_dropped != dropped_logged &&
            (xTaskGetTickCount() - last_drop_log) >= pdMS_TO_TICKS(CAR_SIM_DROP_LOG_PERIOD_MS))
        {
            safe_printf(TAG, "WARN: kolejka TX CAN1 pełna, odrzucone ramki: %lu (razem %lu)\r\n",
                        (unsigned long)(s_tx_dropped - dropped_logged), (unsigned long)s_tx_dropped);
            dropped_logged = s_tx_dropped;
            last_drop_log = xTaskGetTickCount();
        }
    }
}

/* --- Public API ------------------------------------------------------------ */

bool car_sim_init(can_instance_t *can1, bool own_can1)
{
    char err[128];

    if (can1 == NULL)
    {
        return false;
    }
    if (!car_sim_registry_check(err, sizeof(err)))
    {
        safe_printf(TAG, "ERROR: rejestr symulatorów: %s\r\n", err);
        return false;
    }

    s_can1 = can1;
    s_own_can1 = own_can1;

    if (own_can1)
    {
        /* Every simulator must fit into the CAN1 filters together with BOOT_OTA (no silent loss of OTA) */
        const can_parser_profile_t *ota = cp_boot_ota_profile_get();
        for (unsigned id = 0U; id < (unsigned)CAR_SIM_COUNT; id++)
        {
            const car_sim_profile_t *p = car_sim_get((car_sim_id_t)id);
            const uint8_t n = cp_car_sim_filter_count(p);
            if ((uint32_t)n + ota->num_filters > CAN_MAX_FILTERS)
            {
                safe_printf(TAG, "ERROR: symulator %s: %u filtrów + %u BOOT_OTA > %u\r\n", p->key, (unsigned)n,
                            (unsigned)ota->num_filters, (unsigned)CAN_MAX_FILTERS);
                return false;
            }
        }
    }

    if (!select_sim(CAR_SIM_ID_V_BOX, false))
    {
        return false;
    }

    return xTaskCreate(car_sim_task, "car_sim", TASK_STACK_CAR_SIM, NULL, TASK_PRIO_CAR_SIM, &s_task) == pdPASS;
}

void car_sim_request_select(car_sim_id_t id)
{
    if ((unsigned)id >= (unsigned)CAR_SIM_COUNT)
    {
        return;
    }
    taskENTER_CRITICAL();
    s_req_mode = (uint32_t)CAR_SIM_MODE_MANUAL;
    s_req_select = (uint32_t)id;
    taskEXIT_CRITICAL();
}

void car_sim_request_dev_parser(uint16_t no)
{
    s_req_dev_parser = no;
}

void car_sim_request_fob(bool unlock)
{
    s_req_fob = unlock ? FOB_REQ_UNLOCK : FOB_REQ_LOCK;
}

void car_sim_set_mode(car_sim_mode_t mode)
{
    s_req_mode = (uint32_t)((mode == CAR_SIM_MODE_MANUAL) ? CAR_SIM_MODE_MANUAL : CAR_SIM_MODE_AUTO);
}

void car_sim_set_pin(uint16_t pin)
{
    s_pin_override = CAR_SIM_PIN_OVERRIDE_FLAG | pin;
}

void car_sim_reset_pin(void)
{
    s_pin_override = 0U;
}

void car_sim_get_info(car_sim_info_t *info)
{
    const uint32_t pin_override = s_pin_override;

    memset(info, 0, sizeof(*info));
    info->id = s_active_id;
    info->profile = s_active;
    info->mode = s_mode;
    info->last_dev_parser = s_last_dev_parser;
    info->pin = effective_pin(info->profile, pin_override);
    info->pin_overridden = (pin_override & CAR_SIM_PIN_OVERRIDE_FLAG) != 0U;
    info->owns_can1 = s_own_can1;
    info->tx_sent = s_tx_sent;
    info->tx_dropped = s_tx_dropped;
    info->rx_cmds = s_rx_cmds;
    info->rx_pin_rejected = s_rx_pin_rejected;
}

void car_sim_on_rx(const CAN_msg_t *msg)
{
    const car_sim_profile_t *p = s_active;
    sim_match_t m;
    bool ext;

    if (msg == NULL || p == NULL || !car_sim_match_cmd(p->rx, p->num_rx, msg, effective_pin(p, s_pin_override), &m))
    {
        return;
    }

    const uint32_t id = rx_id(msg, &ext);
    s_rx_cmds++;

    if (m.pin_mismatch)
    {
        /* D8: like the car, a wrong PIN gets no reaction */
        s_rx_pin_rejected++;
        safe_printf(TAG, ext ? "WARN: 0x%08lX PIN %04X != %04X, komenda zignorowana (SIM_PIN#%04X)\r\n"
                             : "WARN: 0x%03lX PIN %04X != %04X, komenda zignorowana (SIM_PIN#%04X)\r\n",
                    (unsigned long)id, (unsigned)m.pin_rx, (unsigned)effective_pin(p, s_pin_override),
                    (unsigned)m.pin_rx);
        return;
    }

    switch (m.action)
    {
        case SIM_ACT_UNLOCK:
        case SIM_ACT_LOCK:
        {
            const uint8_t target = (m.action == SIM_ACT_LOCK) ? 1U : 0U;
            const bool ok = io_manager_setRelayStateAsync(target);
            safe_printf(TAG, ext ? "RELAY CMD (ID=0x%08lX, len=%u): target=%u%s, %s\r\n"
                                 : "RELAY CMD (ID=0x%03lX, len=%u): target=%u%s, %s\r\n",
                        (unsigned long)id, (unsigned)msg->len, (unsigned)target, m.has_pin ? ", PIN OK" : "",
                        ok ? "QUEUED" : "QUEUE FULL");
            break;
        }
        case SIM_ACT_LOG:
            safe_printf(TAG, ext ? "RX 0x%08lX len=%u\r\n" : "RX 0x%03lX len=%u\r\n", (unsigned long)id,
                        (unsigned)msg->len);
            break;
        case SIM_ACT_NONE:
        case SIM_ACT_CUSTOM:
        default:
            break;
    }
}
