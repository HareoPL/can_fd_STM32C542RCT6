/**
 * @file can_fd_test.h
 * @brief CAN FD bus test module: runtime switching of bus mode / bit rates and echo service
 *        driven from a PC test tool (tools/can_fd_test.py).
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-22
 *
 * Built only with CMake option CAN_FD_TEST=ON. Uses the CAN parser / manager layers only
 * (no direct HAL access): every configuration change goes through can_parser_select()
 * with the CP_FD_TEST profile, exactly like a production parser.
 *
 * Protocol (all IDs 29-bit extended unless stated otherwise):
 *  - 0x01F2F400 PC -> node  Command, Classic CAN, data[0] = command code
 *  - 0x01F2F401 node -> PC  Response, Classic CAN, data[0] = command | 0x80, data[1] = status
 *  - 0x01F2F410 PC -> node  Echo request (any format / length), extended ID
 *  - 0x00000410 PC -> node  Echo request, standard 11-bit ID
 *  - echo response ID = request ID + 1 (0x01F2F411 / 0x411), same format, length and payload
 *  - 0x01F2F412 node -> PC  Burst frames: data[0..1] = sequence (LE), data[i] = (uint8_t)(i ^ seq)
 *  - 0x01F2F420 CAN1 -> CAN2 Loop request (node internal, same payload rule as burst)
 *  - 0x01F2F421 CAN2 -> CAN1 Loop response (copy of the request)
 *
 * Commands:
 *  - PING        []                                       -> [0x81, 0, mode, nominal_kbps LE16, data_kbps LE16]
 *  - SET_CONFIG  [mode, nominal_kbps LE16, data_kbps LE16] -> [0x82, 0] in the old configuration, then the
 *                node switches after CAN_FD_TEST_SWITCH_DELAY_MS (the PC closes its adapter meanwhile);
 *                on failure the old configuration stays active (PING reports it) and [0x82, CONFIG_REJECTED] is sent.
 *                Without any test frame received within CAN_FD_TEST_CONFIRM_TIMEOUT_MS after the switch
 *                the node falls back to Classic CAN 500 kbps.
 *  - GET_STATS   []  -> [0x83, 0, TEC, REC, LEC, DLEC, TDCV, flags(bit0 error passive, bit1 bus-off)]
 *                       [0x83, 1, echo_rx LE32, echo_drop LE16]
 *                       [0x83, 2, tx_error LE32, bus_off_recoveries LE16 (since boot)]
 *                       [0x83, 3, arbitration phase errors: STUFF, FORM, ACK, BIT1, BIT0, CRC]
 *                       [0x83, 4, data phase errors: STUFF, FORM, ACK, BIT1, BIT0, CRC] (PEA/PED interrupt)
 *                       [0x83, 5, bus_errors LE32, 0, 0] protocol errors (TEC/REC increments, ECR.CEL)
 *  - RESET_STATS []  -> [0x84, 0] (new baseline for all counters and error histograms)
 *  - BURST       [count LE16, len, format] -> burst frames, then [0x85, 0, sent LE16, failed LE16]
 *  - TX_PROBE    [format, len] -> [0x86, 0] if the frame was transmitted, [0x86, TX_FAILED] if rejected
 *  - LOOP        [count LE16, len, format] -> [0x87, 0] immediately; after CAN_FD_TEST_SWITCH_DELAY_MS
 *                (PC closes its adapter) CAN1 sends count frames to CAN2, which loops them back.
 *                Result in GET_STATS frame [0x83, 6, sent LE16, ok LE16, bad LE16]. Needs a second
 *                FDCAN (CAN2) on the same physical bus, it follows every configuration of CAN1.
 */

#ifndef CAN_FD_TEST_H
#define CAN_FD_TEST_H

#include <stdbool.h>
#include "can_manager.h"
#include "can_msg_types.h"

/* --- Protocol identifiers --------------------------------------------------*/
#define CAN_FD_TEST_ID_CMD           0x01F2F400U /**< PC -> node command (extended) */
#define CAN_FD_TEST_ID_RESP          0x01F2F401U /**< node -> PC response (extended) */
#define CAN_FD_TEST_ID_ECHO_EXT      0x01F2F410U /**< PC -> node echo request (extended) */
#define CAN_FD_TEST_ID_ECHO_STD      0x00000410U /**< PC -> node echo request (standard) */
#define CAN_FD_TEST_ID_BURST         0x01F2F412U /**< node -> PC burst frames (extended) */
#define CAN_FD_TEST_ID_LOOP_REQ      0x01F2F420U /**< CAN1 -> CAN2 loop request (extended) */
#define CAN_FD_TEST_ID_LOOP_RESP     0x01F2F421U /**< CAN2 -> CAN1 loop response (extended) */

/* --- Command codes ---------------------------------------------------------*/
#define CAN_FD_TEST_CMD_PING         0x01U
#define CAN_FD_TEST_CMD_SET_CONFIG   0x02U
#define CAN_FD_TEST_CMD_GET_STATS    0x03U
#define CAN_FD_TEST_CMD_RESET_STATS  0x04U
#define CAN_FD_TEST_CMD_BURST        0x05U
#define CAN_FD_TEST_CMD_TX_PROBE     0x06U
#define CAN_FD_TEST_CMD_LOOP         0x07U
#define CAN_FD_TEST_RESP_FLAG        0x80U

/* --- Response status codes -------------------------------------------------*/
#define CAN_FD_TEST_STATUS_OK              0x00U
#define CAN_FD_TEST_STATUS_BAD_ARG         0x01U
#define CAN_FD_TEST_STATUS_CONFIG_REJECTED 0x02U
#define CAN_FD_TEST_STATUS_TX_FAILED       0x03U

/** @brief Delay between the SET_CONFIG acknowledge and the switch (PC closes its adapter meanwhile). */
#define CAN_FD_TEST_SWITCH_DELAY_MS    200U

/** @brief Time after a configuration switch within which a test frame must arrive, otherwise fallback. */
#define CAN_FD_TEST_CONFIRM_TIMEOUT_MS 5000U

/**
 * @brief Initializes the test module on a CAN instance: selects the CP_FD_TEST parser
 *        (Classic CAN 500 kbps), attaches BOOT_OTA filters and starts the test task.
 * @param[in,out] inst CAN instance under test (already initialized by can_manager_init).
 * @param[in,out] loop_inst Optional second CAN instance on the same bus used as loop partner
 *                (follows every configuration of inst), NULL if not available.
 * @return true on success, false on error.
 */
bool can_fd_test_init(can_instance_t *inst, can_instance_t *loop_inst);

/**
 * @brief Command frame entry point called from the CP_FD_TEST parser (CAN RX task context, non-blocking).
 * @param[in] msg Received command frame.
 */
void can_fd_test_on_command(const CAN_msg_t *msg);

/**
 * @brief Echo frame entry point called from the CP_FD_TEST parser (CAN RX task context, non-blocking).
 * @param[in] msg Received echo request frame.
 */
void can_fd_test_on_echo(const CAN_msg_t *msg);

/**
 * @brief Loop request entry point (CAN2 side), called from the CP_FD_TEST parser (non-blocking).
 * @param[in] msg Received loop request frame.
 */
void can_fd_test_on_loop_request(const CAN_msg_t *msg);

/**
 * @brief Loop response entry point (CAN1 side), called from the CP_FD_TEST parser (non-blocking).
 * @param[in] msg Received loop response frame.
 */
void can_fd_test_on_loop_response(const CAN_msg_t *msg);

#endif /* CAN_FD_TEST_H */
