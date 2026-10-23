/**
 * @file can_fd_test.c
 * @brief Implementation of the CAN FD bus test module (see can_fd_test.h for the protocol).
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-22
 */

#include "can_fd_test.h"
#include "can_parser.h"
#include "cp_fd_test.h"
#include "tasks_config.h"
#include "safe_printf.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stddef.h>
#include <string.h>

/** @brief Logging tag for the CAN FD test module. */
#define TAG "CAN_FD_TEST"

/** @brief Depth of the command queue between parser callbacks and the test task. */
#define CAN_FD_TEST_CMD_QUEUE_LEN     8U

/** @brief Maximum time to wait for the TX queue to drain. */
#define CAN_FD_TEST_TX_IDLE_TIMEOUT_MS 200U

/** @brief Maximum time a burst may wait for free TX queue space per frame. */
#define CAN_FD_TEST_BURST_FRAME_TIMEOUT_MS 100U

/** @brief Command task polling period (fallback supervision granularity). */
#define CAN_FD_TEST_POLL_MS           100U

/** @brief Maximum time to wait for loop responses after the last loop request. */
#define CAN_FD_TEST_LOOP_SETTLE_MS    200U

/** @brief Maximum number of loop requests in flight (keeps the partner's TX queue from overflowing). */
#define CAN_FD_TEST_LOOP_IN_FLIGHT    4U

/** @brief CAN1 and CAN2 instances (index = bus number - 1), CAN2 may be NULL. */
static can_instance_t *s_bus_inst[2] = { NULL, NULL };

/** @brief CAN instance under test (CAN1 after reset, changed by SELECT_BUS, read by the RX task). */
static can_instance_t *volatile s_inst = NULL;

/** @brief Optional loop partner on the same bus, follows every configuration of s_inst. */
static can_instance_t *volatile s_loop_inst = NULL;

/** @brief Bus number of s_inst (1 = CAN1, 2 = CAN2). */
static uint8_t s_bus = 1U;

/** @brief Loop test results (responses counted in CAN RX task context). */
static volatile uint16_t s_loop_sent = 0U;
static volatile uint16_t s_loop_ok = 0U;
static volatile uint16_t s_loop_bad = 0U;

/** @brief Commands forwarded from the RX task to the test task. */
static QueueHandle_t s_cmd_queue = NULL;

/** @brief Safe fallback configuration (Classic CAN 500 kbps, same as the production default). */
static const can_config_t s_default_config = {
    .mode            = CAN_MODE_CLASSIC,
    .baudrate_normal = CAN_BAUDRATE_500K,
    .baudrate_FD     = CAN_BAUDRATE_CUSTOM
};

/** @brief Set by parser callbacks whenever a test frame is received (confirms a new configuration). */
static volatile bool s_rx_seen = false;

/** @brief True while a switched configuration awaits confirmation by the PC. */
static bool s_pending_confirm = false;

/** @brief Tick at which an unconfirmed configuration falls back to the default. */
static TickType_t s_confirm_deadline = 0U;

/** @brief Echo statistics (updated in CAN RX task context). */
static volatile uint32_t s_echo_rx = 0U;
static volatile uint32_t s_echo_drop = 0U;

/** @brief TX error counter value at the last RESET_STATS. */
static uint32_t s_tx_error_base = 0U;

/** @brief Bus status (cumulative error counters and histograms) at the last RESET_STATS. */
static can_bus_status_t s_status_base;

/* --- Private helpers -------------------------------------------------------*/

/**
 * @brief Sends a Classic CAN response frame on the response ID.
 * @param[in] data Payload (up to 8 bytes).
 * @param[in] len Payload length.
 * @return true if queued.
 */
static bool can_fd_test_send_resp(const uint8_t *data, uint8_t len)
{
    CAN_msg_t resp = {0};
    resp.ident = CAN_msg_set_extended_id(CAN_FD_TEST_ID_RESP);
    resp.format = CAN_MSG_FRAME_FORMAT_CAN;
    resp.len = (len > 8U) ? 8U : len;
    memcpy(resp.data, data, resp.len);

    return can_manager_send(s_inst, &resp);
}

/**
 * @brief Sends a two byte status response [cmd | 0x80, status].
 */
static void can_fd_test_send_status(uint8_t cmd, uint8_t status)
{
    uint8_t data[2] = { (uint8_t)(cmd | CAN_FD_TEST_RESP_FLAG), status };
    (void)can_fd_test_send_resp(data, sizeof(data));
}

/**
 * @brief Returns printable name of a bus mode.
 */
static const char *can_fd_test_mode_name(can_frame_mode_t mode)
{
    switch (mode)
    {
        case CAN_MODE_CLASSIC:   return "Classic";
        case CAN_MODE_FD_NO_BRS: return "FD";
        case CAN_MODE_FD_BRS:    return "FD+BRS";
        default:                 return "?";
    }
}

/**
 * @brief Applies a bus configuration through the parser layer and re-attaches BOOT_OTA filters.
 * @param[in] config Desired configuration.
 * @return true on success; on failure the previous configuration stays active.
 */
static bool can_fd_test_apply_config(const can_config_t *config)
{
    can_config_t previous = s_inst->can_config;

    cp_fd_test_bus_config_set(config);
    if (!can_parser_select(s_inst, CAN_PARSER_ID_FD_TEST))
    {
        cp_fd_test_bus_config_set(&previous);
        return false;
    }

    /* The loop partner shares the physical bus, so it must always run the same configuration */
    if (s_loop_inst != NULL && !can_parser_select(s_loop_inst, CAN_PARSER_ID_FD_TEST))
    {
        safe_printf(TAG, "WARNING: loop partner %s not reconfigured\r\n", s_loop_inst->name);
    }

    /* OTA manager runs on CAN1, whichever bus is under test */
    if (!can_parser_attach_boot_ota(s_bus_inst[0]))
    {
        safe_printf(TAG, "WARNING: BOOT_OTA filters not attached\r\n");
    }

    return true;
}

/**
 * @brief Handles SET_CONFIG: acknowledge in the old configuration, switch, report rejection.
 */
static void can_fd_test_handle_set_config(const CAN_msg_t *msg)
{
    if (msg->len < 6U || msg->data[1] > (uint8_t)CAN_MODE_FD_BRS)
    {
        can_fd_test_send_status(CAN_FD_TEST_CMD_SET_CONFIG, CAN_FD_TEST_STATUS_BAD_ARG);
        return;
    }

    uint32_t nominal_kbps = (uint32_t)msg->data[2] | ((uint32_t)msg->data[3] << 8);
    uint32_t data_kbps = (uint32_t)msg->data[4] | ((uint32_t)msg->data[5] << 8);

    can_config_t config = {
        .mode            = (can_frame_mode_t)msg->data[1],
        .baudrate_normal = (can_baudrate_t)(nominal_kbps * 1000U),
        .baudrate_FD     = (can_baudrate_t)(data_kbps * 1000U)
    };

    safe_printf(TAG, "SET_CONFIG %s nominal=%lu kbps data=%lu kbps\r\n",
                can_fd_test_mode_name(config.mode), (unsigned long)nominal_kbps, (unsigned long)data_kbps);

    can_fd_test_send_status(CAN_FD_TEST_CMD_SET_CONFIG, CAN_FD_TEST_STATUS_OK);
    (void)can_manager_waitTxIdle(s_inst, CAN_FD_TEST_TX_IDLE_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(CAN_FD_TEST_SWITCH_DELAY_MS));

    if (!can_fd_test_apply_config(&config))
    {
        safe_printf(TAG, "SET_CONFIG rejected, previous configuration kept\r\n");
        can_fd_test_send_status(CAN_FD_TEST_CMD_SET_CONFIG, CAN_FD_TEST_STATUS_CONFIG_REJECTED);
        return;
    }

    /* Any configuration must be confirmed by the PC, otherwise fall back to the default */
    s_rx_seen = false;
    s_pending_confirm = true;
    s_confirm_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(CAN_FD_TEST_CONFIRM_TIMEOUT_MS);
}

/**
 * @brief Handles GET_STATS: bus health and echo counters in three Classic CAN frames.
 */
static void can_fd_test_handle_get_stats(void)
{
    can_bus_status_t status = {0};
    bool ok = can_manager_getStatus(s_inst, &status);

    uint8_t frame0[8] = {
        (uint8_t)(CAN_FD_TEST_CMD_GET_STATS | CAN_FD_TEST_RESP_FLAG),
        0U,
        status.tx_error_cnt,
        status.rx_error_cnt,
        (uint8_t)status.last_error,
        (uint8_t)status.data_last_error,
        status.tdc_value,
        (uint8_t)((status.error_passive ? 0x01U : 0x00U) | (status.bus_off ? 0x02U : 0x00U) | (ok ? 0x00U : 0x80U))
    };

    uint32_t echo_rx = s_echo_rx;
    uint32_t echo_drop = s_echo_drop;
    uint8_t frame1[8] = {
        (uint8_t)(CAN_FD_TEST_CMD_GET_STATS | CAN_FD_TEST_RESP_FLAG),
        1U,
        (uint8_t)echo_rx, (uint8_t)(echo_rx >> 8), (uint8_t)(echo_rx >> 16), (uint8_t)(echo_rx >> 24),
        (uint8_t)echo_drop, (uint8_t)(echo_drop >> 8)
    };

    uint32_t tx_error = s_inst->tx_error_cnt - s_tx_error_base;
    uint32_t bus_errors = ok ? (status.error_log_cnt - s_status_base.error_log_cnt) : 0U;
    uint8_t frame5[8] = {
        (uint8_t)(CAN_FD_TEST_CMD_GET_STATS | CAN_FD_TEST_RESP_FLAG),
        5U,
        (uint8_t)bus_errors, (uint8_t)(bus_errors >> 8), (uint8_t)(bus_errors >> 16), (uint8_t)(bus_errors >> 24),
        0U, 0U
    };
    uint8_t frame2[8] = {
        (uint8_t)(CAN_FD_TEST_CMD_GET_STATS | CAN_FD_TEST_RESP_FLAG),
        2U,
        (uint8_t)tx_error, (uint8_t)(tx_error >> 8), (uint8_t)(tx_error >> 16), (uint8_t)(tx_error >> 24),
        (uint8_t)s_inst->bus_off_cnt, (uint8_t)(s_inst->bus_off_cnt >> 8)
    };

    (void)can_fd_test_send_resp(frame0, sizeof(frame0));
    (void)can_fd_test_send_resp(frame1, sizeof(frame1));
    (void)can_fd_test_send_resp(frame2, sizeof(frame2));

    /* Frames 3/4: protocol errors per type (STUFF, FORM, ACK, BIT1, BIT0, CRC) since RESET_STATS, saturated */
    for (uint8_t phase = 0U; phase < 2U; phase++)
    {
        const uint16_t *now = (phase == 0U) ? status.arb_errors : status.data_errors;
        const uint16_t *base = (phase == 0U) ? s_status_base.arb_errors : s_status_base.data_errors;
        uint8_t frame[8] = { (uint8_t)(CAN_FD_TEST_CMD_GET_STATS | CAN_FD_TEST_RESP_FLAG), (uint8_t)(3U + phase) };
        for (uint8_t i = 0U; i < CAN_ERROR_TYPES; i++)
        {
            uint16_t delta = (uint16_t)(now[i] - base[i]);
            frame[2U + i] = (delta > 255U) ? 255U : (uint8_t)delta;
        }
        (void)can_fd_test_send_resp(frame, sizeof(frame));
    }
    (void)can_fd_test_send_resp(frame5, sizeof(frame5));

    uint16_t loop_sent = s_loop_sent;
    uint16_t loop_ok = s_loop_ok;
    uint16_t loop_bad = s_loop_bad;
    uint8_t frame6[8] = {
        (uint8_t)(CAN_FD_TEST_CMD_GET_STATS | CAN_FD_TEST_RESP_FLAG),
        6U,
        (uint8_t)loop_sent, (uint8_t)(loop_sent >> 8),
        (uint8_t)loop_ok, (uint8_t)(loop_ok >> 8),
        (uint8_t)loop_bad, (uint8_t)(loop_bad >> 8)
    };
    (void)can_fd_test_send_resp(frame6, sizeof(frame6));
}

/**
 * @brief Handles RESET_STATS: takes a new baseline of the cumulative error counters.
 */
static void can_fd_test_reset_counters(void)
{
    memset(&s_status_base, 0, sizeof(s_status_base));
    (void)can_manager_getStatus(s_inst, &s_status_base);

    s_echo_rx = 0U;
    s_echo_drop = 0U;
    s_tx_error_base = s_inst->tx_error_cnt;
}

/**
 * @brief Handles RESET_STATS: takes a new baseline of the cumulative error counters.
 */
static void can_fd_test_handle_reset_stats(void)
{
    can_fd_test_reset_counters();
    can_fd_test_send_status(CAN_FD_TEST_CMD_RESET_STATS, CAN_FD_TEST_STATUS_OK);
}

/**
 * @brief Handles SELECT_BUS: swaps the bus under test and the loop partner, answers from the new bus.
 */
static void can_fd_test_handle_select_bus(const CAN_msg_t *msg)
{
    uint8_t bus = (msg->len >= 2U) ? msg->data[1] : 0U;
    if (bus < 1U || bus > 2U || s_bus_inst[bus - 1U] == NULL)
    {
        can_fd_test_send_status(CAN_FD_TEST_CMD_SELECT_BUS, CAN_FD_TEST_STATUS_BAD_ARG);
        return;
    }

    if (bus != s_bus)
    {
        /* Both buses run the same configuration (profile selected on both), only the roles change */
        s_loop_inst = s_bus_inst[(bus == 1U) ? 1U : 0U];
        s_inst = s_bus_inst[bus - 1U];
        s_bus = bus;
        s_loop_sent = 0U;
        s_loop_ok = 0U;
        s_loop_bad = 0U;
        can_fd_test_reset_counters();
        safe_printf(TAG, "bus under test: %s (loop partner: %s)\r\n",
                    s_inst->name, (s_loop_inst != NULL) ? s_loop_inst->name : "none");
    }

    can_fd_test_send_status(CAN_FD_TEST_CMD_SELECT_BUS, CAN_FD_TEST_STATUS_OK);
}

/**
 * @brief Handles BURST: node transmits a numbered sequence of frames.
 */
static void can_fd_test_handle_burst(const CAN_msg_t *msg)
{
    if (msg->len < 5U)
    {
        can_fd_test_send_status(CAN_FD_TEST_CMD_BURST, CAN_FD_TEST_STATUS_BAD_ARG);
        return;
    }

    uint16_t count = (uint16_t)(msg->data[1] | ((uint16_t)msg->data[2] << 8));
    uint8_t len = msg->data[3];
    uint8_t format = msg->data[4];
    uint8_t max_len = (format == CAN_MSG_FRAME_FORMAT_FD_CAN) ? 64U : 8U;

    if (len < 2U || len > max_len)
    {
        can_fd_test_send_status(CAN_FD_TEST_CMD_BURST, CAN_FD_TEST_STATUS_BAD_ARG);
        return;
    }

    uint16_t sent = 0U;
    uint16_t failed = 0U;

    for (uint16_t seq = 0U; seq < count; seq++)
    {
        CAN_msg_t frame = {0};
        frame.ident = CAN_msg_set_extended_id(CAN_FD_TEST_ID_BURST);
        frame.format = format;
        frame.len = len;
        frame.data[0] = (uint8_t)seq;
        frame.data[1] = (uint8_t)(seq >> 8);
        for (uint8_t i = 2U; i < len; i++)
        {
            frame.data[i] = (uint8_t)(i ^ (uint8_t)seq);
        }

        TickType_t start = xTaskGetTickCount();
        bool queued = can_manager_send(s_inst, &frame);
        while (!queued && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(CAN_FD_TEST_BURST_FRAME_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(1));
            queued = can_manager_send(s_inst, &frame);
        }

        if (queued)
        {
            sent++;
        }
        else
        {
            failed++;
        }
    }

    (void)can_manager_waitTxIdle(s_inst, CAN_FD_TEST_TX_IDLE_TIMEOUT_MS);

    uint8_t resp[6] = {
        (uint8_t)(CAN_FD_TEST_CMD_BURST | CAN_FD_TEST_RESP_FLAG),
        CAN_FD_TEST_STATUS_OK,
        (uint8_t)sent, (uint8_t)(sent >> 8),
        (uint8_t)failed, (uint8_t)(failed >> 8)
    };
    (void)can_fd_test_send_resp(resp, sizeof(resp));
}

/**
 * @brief Handles TX_PROBE: transmits one frame of the requested format and reports whether
 *        the port accepted it (an FD frame must be rejected on a Classic CAN bus).
 */
static void can_fd_test_handle_tx_probe(const CAN_msg_t *msg)
{
    if (msg->len < 3U)
    {
        can_fd_test_send_status(CAN_FD_TEST_CMD_TX_PROBE, CAN_FD_TEST_STATUS_BAD_ARG);
        return;
    }

    CAN_msg_t frame = {0};
    frame.ident = CAN_msg_set_extended_id(CAN_FD_TEST_ID_BURST);
    frame.format = msg->data[1];
    frame.len = (msg->data[2] > 64U) ? 64U : msg->data[2];
    memset(frame.data, 0xA5, frame.len);

    uint32_t errors_before = s_inst->tx_error_cnt;
    bool queued = can_manager_send(s_inst, &frame);
    (void)can_manager_waitTxIdle(s_inst, CAN_FD_TEST_TX_IDLE_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(20));

    bool rejected = !queued || (s_inst->tx_error_cnt != errors_before);
    can_fd_test_send_status(CAN_FD_TEST_CMD_TX_PROBE,
                            rejected ? CAN_FD_TEST_STATUS_TX_FAILED : CAN_FD_TEST_STATUS_OK);
}

/**
 * @brief Fills a numbered test frame: data[0..1] = sequence, data[i] = i ^ sequence.
 */
static void can_fd_test_fill_seq_frame(CAN_msg_t *frame, uint32_t id, uint16_t seq, uint8_t len, uint8_t format)
{
    memset(frame, 0, sizeof(*frame));
    frame->ident = CAN_msg_set_extended_id(id);
    frame->format = format;
    frame->len = len;
    frame->data[0] = (uint8_t)seq;
    frame->data[1] = (uint8_t)(seq >> 8);
    for (uint8_t i = 2U; i < len; i++)
    {
        frame->data[i] = (uint8_t)(i ^ (uint8_t)seq);
    }
}

/**
 * @brief Handles LOOP: the bus under test sends numbered frames to the partner, which returns them (no PC involved).
 */
static void can_fd_test_handle_loop(const CAN_msg_t *msg)
{
    uint8_t format = (msg->len >= 5U) ? msg->data[4] : CAN_MSG_FRAME_FORMAT_CAN;
    uint8_t max_len = (format == CAN_MSG_FRAME_FORMAT_FD_CAN) ? 64U : 8U;

    if (s_loop_inst == NULL || msg->len < 5U || msg->data[3] < 2U || msg->data[3] > max_len)
    {
        can_fd_test_send_status(CAN_FD_TEST_CMD_LOOP, CAN_FD_TEST_STATUS_BAD_ARG);
        return;
    }

    uint16_t count = (uint16_t)(msg->data[1] | ((uint16_t)msg->data[2] << 8));
    uint8_t len = msg->data[3];

    s_loop_sent = 0U;
    s_loop_ok = 0U;
    s_loop_bad = 0U;

    /* Acknowledge, then give the PC time to close its adapter so only the node's buses are on the line */
    can_fd_test_send_status(CAN_FD_TEST_CMD_LOOP, CAN_FD_TEST_STATUS_OK);
    (void)can_manager_waitTxIdle(s_inst, CAN_FD_TEST_TX_IDLE_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(CAN_FD_TEST_SWITCH_DELAY_MS));

    for (uint16_t seq = 0U; seq < count; seq++)
    {
        CAN_msg_t frame;
        can_fd_test_fill_seq_frame(&frame, CAN_FD_TEST_ID_LOOP_REQ, seq, len, format);

        TickType_t start = xTaskGetTickCount();
        bool queued = can_manager_send(s_inst, &frame);
        while (!queued && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(CAN_FD_TEST_BURST_FRAME_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(1));
            queued = can_manager_send(s_inst, &frame);
        }
        if (queued)
        {
            s_loop_sent++;
        }

        while ((uint16_t)(s_loop_sent - s_loop_ok - s_loop_bad) > CAN_FD_TEST_LOOP_IN_FLIGHT &&
               (xTaskGetTickCount() - start) < pdMS_TO_TICKS(CAN_FD_TEST_BURST_FRAME_TIMEOUT_MS))
        {
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        /* Partner not on the same physical bus: do not block the command task for count x timeout */
        if (s_loop_sent >= (2U * CAN_FD_TEST_LOOP_IN_FLIGHT) && (s_loop_ok + s_loop_bad) == 0U)
        {
            safe_printf(TAG, "LOOP aborted: no response from %s (not on the same bus?)\r\n", s_loop_inst->name);
            break;
        }
    }

    (void)can_manager_waitTxIdle(s_inst, CAN_FD_TEST_TX_IDLE_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(CAN_FD_TEST_LOOP_SETTLE_MS));

    s_rx_seen = true;
    safe_printf(TAG, "LOOP %s len=%u: sent=%u ok=%u bad=%u\r\n",
                (format == CAN_MSG_FRAME_FORMAT_FD_CAN) ? "FD" : "CAN", (unsigned int)len,
                (unsigned int)s_loop_sent, (unsigned int)s_loop_ok, (unsigned int)s_loop_bad);
}

/**
 * @brief Handles PING: reports the active bus configuration.
 */
static void can_fd_test_handle_ping(void)
{
    const can_config_t *cfg = &s_inst->can_config;
    uint32_t nominal_kbps = (uint32_t)cfg->baudrate_normal / 1000U;
    uint32_t data_kbps = (uint32_t)cfg->baudrate_FD / 1000U;

    uint8_t resp[8] = {
        (uint8_t)(CAN_FD_TEST_CMD_PING | CAN_FD_TEST_RESP_FLAG),
        CAN_FD_TEST_STATUS_OK,
        (uint8_t)cfg->mode,
        (uint8_t)nominal_kbps, (uint8_t)(nominal_kbps >> 8),
        (uint8_t)data_kbps, (uint8_t)(data_kbps >> 8),
        s_bus
    };
    (void)can_fd_test_send_resp(resp, sizeof(resp));
}

/**
 * @brief Falls back to the default configuration if a switched configuration was not confirmed.
 */
static void can_fd_test_supervise_confirm(void)
{
    if (!s_pending_confirm)
    {
        return;
    }

    if (s_rx_seen)
    {
        s_pending_confirm = false;
        safe_printf(TAG, "configuration confirmed by PC\r\n");
        return;
    }

    if ((int32_t)(xTaskGetTickCount() - s_confirm_deadline) >= 0)
    {
        s_pending_confirm = false;
        safe_printf(TAG, "no test traffic in new configuration, falling back to Classic 500 kbps\r\n");
        (void)can_fd_test_apply_config(&s_default_config);
    }
}

/**
 * @brief Test worker task: executes commands outside the CAN RX task and supervises fallback.
 * @param[in] pvParameters Unused.
 */
static void can_fd_test_task(void *pvParameters)
{
    (void)pvParameters;
    CAN_msg_t cmd;

    for (;;)
    {
        if (xQueueReceive(s_cmd_queue, &cmd, pdMS_TO_TICKS(CAN_FD_TEST_POLL_MS)) == pdTRUE)
        {
            switch (cmd.data[0])
            {
                case CAN_FD_TEST_CMD_PING:        can_fd_test_handle_ping();        break;
                case CAN_FD_TEST_CMD_SET_CONFIG:  can_fd_test_handle_set_config(&cmd); break;
                case CAN_FD_TEST_CMD_GET_STATS:   can_fd_test_handle_get_stats();   break;
                case CAN_FD_TEST_CMD_RESET_STATS: can_fd_test_handle_reset_stats(); break;
                case CAN_FD_TEST_CMD_BURST:       can_fd_test_handle_burst(&cmd);   break;
                case CAN_FD_TEST_CMD_TX_PROBE:    can_fd_test_handle_tx_probe(&cmd); break;
                case CAN_FD_TEST_CMD_LOOP:        can_fd_test_handle_loop(&cmd);    break;
                case CAN_FD_TEST_CMD_SELECT_BUS:  can_fd_test_handle_select_bus(&cmd); break;
                default:
                    can_fd_test_send_status(cmd.data[0], CAN_FD_TEST_STATUS_BAD_ARG);
                    break;
            }
        }

        can_fd_test_supervise_confirm();
    }
}

/* --- Public API ------------------------------------------------------------*/

bool can_fd_test_init(can_instance_t *can1, can_instance_t *can2)
{
    if (can1 == NULL)
    {
        return false;
    }

    s_bus_inst[0] = can1;
    s_bus_inst[1] = can2;
    s_inst = can1;
    s_loop_inst = can2;
    s_bus = 1U;

    s_cmd_queue = xQueueCreate(CAN_FD_TEST_CMD_QUEUE_LEN, sizeof(CAN_msg_t));
    if (s_cmd_queue == NULL)
    {
        return false;
    }

    if (!can_fd_test_apply_config(&s_default_config))
    {
        return false;
    }

    BaseType_t ret = xTaskCreate(can_fd_test_task, "can_fd_test", TASK_STACK_CAN_FD_TEST,
                                 NULL, TASK_PRIO_CAN_FD_TEST, NULL);
    if (ret != pdPASS)
    {
        return false;
    }

    safe_printf(TAG, "CAN FD test module active on %s (loop partner: %s)\r\n",
                can1->name, (can2 != NULL) ? can2->name : "none");
    return true;
}

void can_fd_test_on_command(const CAN_msg_t *msg)
{
    if (msg == NULL || msg->len == 0U || s_cmd_queue == NULL)
    {
        return;
    }

    /* Commands are served by the instance under test only (the loop partner hears them too). SELECT_BUS of an
     * existing bus is served by the selected bus, so it also works when the PC adapter is wired to that bus only. */
    can_instance_t *active = can_manager_get_active_rx_instance();
    bool accept = (active == NULL || active == s_inst);
    if (msg->data[0] == CAN_FD_TEST_CMD_SELECT_BUS && msg->len >= 2U &&
        msg->data[1] >= 1U && msg->data[1] <= 2U && s_bus_inst[msg->data[1] - 1U] != NULL)
    {
        accept = (active == NULL || active == s_bus_inst[msg->data[1] - 1U]);
    }
    if (!accept)
    {
        return;
    }

    s_rx_seen = true;
    (void)xQueueSend(s_cmd_queue, msg, 0);
}

void can_fd_test_on_echo(const CAN_msg_t *msg)
{
    can_instance_t *inst = can_manager_get_active_rx_instance();

    /* Only the instance under test echoes PC frames (the loop partner hears them too) */
    if (msg == NULL || (inst != NULL && inst != s_inst))
    {
        return;
    }

    s_rx_seen = true;
    s_echo_rx++;

    if (inst == NULL)
    {
        inst = s_inst;
    }

    /* Response ID = request ID + 1, identical format, length and payload */
    CAN_msg_t reply = *msg;
    reply.ident = msg->ident + 1U;

    if (!can_manager_send(inst, &reply))
    {
        s_echo_drop++;
    }
}

void can_fd_test_on_loop_request(const CAN_msg_t *msg)
{
    if (msg == NULL || s_loop_inst == NULL || can_manager_get_active_rx_instance() != s_loop_inst)
    {
        return;
    }

    CAN_msg_t reply = *msg;
    reply.ident = CAN_msg_set_extended_id(CAN_FD_TEST_ID_LOOP_RESP);
    (void)can_manager_send(s_loop_inst, &reply);
}

void can_fd_test_on_loop_response(const CAN_msg_t *msg)
{
    if (msg == NULL || can_manager_get_active_rx_instance() != s_inst)
    {
        return;
    }

    uint16_t seq = (uint16_t)(msg->data[0] | ((uint16_t)msg->data[1] << 8));
    CAN_msg_t expected;
    can_fd_test_fill_seq_frame(&expected, CAN_FD_TEST_ID_LOOP_REQ, seq, msg->len, msg->format);

    if (msg->len >= 2U && memcmp(msg->data, expected.data, msg->len) == 0)
    {
        s_loop_ok++;
    }
    else
    {
        s_loop_bad++;
    }
}
