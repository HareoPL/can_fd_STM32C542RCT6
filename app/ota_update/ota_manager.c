/**
 * @file ota_manager.c
 * @brief Implementation of OTA update manager and dummy verification engine for FDCAN2.
 */

#include "ota_manager.h"
#include "ota_protocol.h"
#include "isotp.h"
#include "crc32.h"
#include "tasks_config.h"
#include "safe_printf.h"
#include "version.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <string.h>

#define TAG "OTA_MGR"

/** @brief Length of the incoming frame queue. */
#define OTA_QUEUE_LENGTH                16U

/** @brief Static storage for cached 96-bit MCU UID. */
static uint8_t s_mcu_uid[STM32C5_UID_LEN_BYTES];

/** @brief Target selection flag (true if this node's UID matched CMD_SELECT_TARGET). */
static bool s_target_selected = false;

/** @brief Pointer to CAN2 instance used for transmission. */
static can_instance_t *s_can2_inst = NULL;

/** @brief FreeRTOS queue for incoming CAN messages dispatched from cp_boot_ota. */
static QueueHandle_t s_ota_rx_queue = NULL;

/** @brief ISO-TP transport layer link descriptor. */
static isotp_link_t s_isotp_link;

/** @brief Running cumulative CRC32 accumulator across all received write blocks. */
static uint32_t s_running_crc32 = 0U;

/** @brief Cumulative count of firmware payload bytes received in current session. */
static uint32_t s_total_bytes_received = 0U;

/** @brief Block sequence counter for ACK tagging. */
static uint32_t s_block_count = 0U;

/* --- Internal Helpers ------------------------------------------------------*/

/**
 * @brief Helper callback for ISO-TP to transmit a raw CAN frame over FDCAN2.
 */
static bool ota_can_send_frame(uint32_t can_id, const uint8_t *data, uint8_t len, void *user_ctx)
{
    can_instance_t *inst = (can_instance_t *)user_ctx;
    if (inst == NULL || data == NULL || len == 0U)
    {
        return false;
    }

    CAN_msg_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.ident = CAN_msg_set_standard_id(can_id);
    frame.format = CAN_MSG_FRAME_FORMAT_CAN;
    frame.len = len;
    memcpy(frame.data, data, len);

    return can_manager_send(inst, &frame);
}

/**
 * @brief Handles Discovery command received via Broadcast ID 0x7DF.
 */
static void handle_discovery_cmd(void)
{
    /* Calculate 32-bit CRC32 from our 12-byte UID */
    uint32_t uid_crc = crc32_calculate(s_mcu_uid, STM32C5_UID_LEN_BYTES);

    /* Anti-collision slot delay: (CRC32 & 0x1F) * 3 ms -> 0 to 93 ms */
    uint32_t slot_delay_ms = (uid_crc & 0x1FU) * 3U;
    vTaskDelay(pdMS_TO_TICKS(slot_delay_ms));

    const app_version_info_t *ver = version_getInfo();
    uint8_t packed_ver = (uint8_t)(((ver->fw_major & 0x0FU) << 4) | (ver->fw_minor & 0x0FU));

    /* Frame 1 (0x7E9): [0x01, UID[0..6]] (8 bytes total) */
    uint8_t frame1[8];
    frame1[0] = 0x01U;
    memcpy(&frame1[1], &s_mcu_uid[0], 7U);
    (void)ota_can_send_frame(CAN_ID_OTA_DISCOVERY_RESP, frame1, sizeof(frame1), s_can2_inst);

    /* Frame 2 (0x7E9): [0x02, UID[7..11], Mode=1 (APP), PackedVersion] (8 bytes total) */
    uint8_t frame2[8];
    frame2[0] = 0x02U;
    memcpy(&frame2[1], &s_mcu_uid[7], 5U);
    frame2[6] = OTA_MODE_APPLICATION;
    frame2[7] = packed_ver;
    (void)ota_can_send_frame(CAN_ID_OTA_DISCOVERY_RESP, frame2, sizeof(frame2), s_can2_inst);

    safe_printf(TAG, "[DISCOVERY] Responded on 0x7E9 after %lu ms slot (UID: %02X%02X%02X%02X...)\r\n",
                slot_delay_ms, s_mcu_uid[0], s_mcu_uid[1], s_mcu_uid[2], s_mcu_uid[3]);
}

/**
 * @brief Callback invoked when a full ISO-TP PDU has been assembled from 0x7E0.
 */
static void handle_isotp_pdu(const uint8_t *payload, uint16_t length, void *user_ctx)
{
    (void)user_ctx;

    if (payload == NULL || length == 0U)
    {
        return;
    }

    uint8_t cmd = payload[0];

    switch (cmd)
    {
        case CMD_SELECT_TARGET:
        {
            if (length < 13U)
            {
                return;
            }

            if (memcmp(s_mcu_uid, &payload[1], STM32C5_UID_LEN_BYTES) == 0)
            {
                s_target_selected = true;
                const app_version_info_t *v = version_getInfo();
                uint8_t resp[5] = {
                    OTA_RESP_ACK,
                    OTA_MODE_APPLICATION,
                    v->fw_major,
                    v->fw_minor,
                    v->fw_patch
                };
                (void)isotp_send_pdu(&s_isotp_link, resp, sizeof(resp));
                safe_printf(TAG, "[SELECT] Device SELECTED by matching UID! ACK sent.\r\n");
            }
            else
            {
                s_target_selected = false;
            }
            break;
        }

        case CMD_ENTER_BOOTLOADER:
        {
            if (!s_target_selected)
            {
                return;
            }

            safe_printf(TAG, "[CMD] ENTER_BOOTLOADER received (Dummy mode: sending ACK)\r\n");
            uint8_t ack[1] = {OTA_RESP_ACK};
            (void)isotp_send_pdu(&s_isotp_link, ack, sizeof(ack));
            break;
        }

        case CMD_ERASE_APP:
        {
            if (!s_target_selected)
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_NOT_SELECTED};
                (void)isotp_send_pdu(&s_isotp_link, nack, sizeof(nack));
                return;
            }

            uint32_t erase_addr = 0U;
            uint32_t erase_size = 0U;
            if (length >= 9U)
            {
                erase_addr = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) |
                             ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
                erase_size = (uint32_t)payload[5] | ((uint32_t)payload[6] << 8) |
                             ((uint32_t)payload[7] << 16) | ((uint32_t)payload[8] << 24);
            }

            s_running_crc32 = crc32_init();
            s_total_bytes_received = 0U;
            s_block_count = 0U;

            safe_printf(TAG, "[OTA DUMMY] Erase simulated: addr=0x%08lX, size=%lu B\r\n",
                        (unsigned long)erase_addr, (unsigned long)erase_size);

            uint8_t ack[1] = {OTA_RESP_ACK};
            (void)isotp_send_pdu(&s_isotp_link, ack, sizeof(ack));
            break;
        }

        case CMD_WRITE_MEMORY:
        {
            if (!s_target_selected)
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_NOT_SELECTED};
                (void)isotp_send_pdu(&s_isotp_link, nack, sizeof(nack));
                return;
            }

            if (length < 11U)
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_SEQUENCE};
                (void)isotp_send_pdu(&s_isotp_link, nack, sizeof(nack));
                return;
            }

            uint32_t block_addr = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) |
                                  ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
            uint16_t block_len  = (uint16_t)payload[5] | ((uint16_t)payload[6] << 8);

            if (length < (7U + block_len + 4U))
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_SEQUENCE};
                (void)isotp_send_pdu(&s_isotp_link, nack, sizeof(nack));
                return;
            }

            uint32_t expected_crc = (uint32_t)payload[7 + block_len] |
                                    ((uint32_t)payload[8 + block_len] << 8) |
                                    ((uint32_t)payload[9 + block_len] << 16) |
                                    ((uint32_t)payload[10 + block_len] << 24);

            /* Calculate CRC32 of address + length + data */
            uint32_t actual_crc = crc32_calculate(&payload[1], 4U + 2U + block_len);

            if (actual_crc != expected_crc)
            {
                safe_printf(TAG, "[OTA DUMMY] Block CRC mismatch! Exp=0x%08lX, Calc=0x%08lX\r\n",
                            (unsigned long)expected_crc, (unsigned long)actual_crc);
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_CRC};
                (void)isotp_send_pdu(&s_isotp_link, nack, sizeof(nack));
                return;
            }

            /* Update global stream CRC32 accumulator with block payload only */
            s_running_crc32 = crc32_update(s_running_crc32, &payload[7], block_len);
            s_total_bytes_received += block_len;

            uint8_t block_seq = (uint8_t)(s_block_count++ & 0xFFU);

            if ((s_block_count % 16U) == 0U)
            {
                safe_printf(TAG, "[OTA DUMMY] Received block #%lu (addr=0x%08lX, %lu B total)\r\n",
                            (unsigned long)s_block_count, (unsigned long)block_addr, (unsigned long)s_total_bytes_received);
            }

            uint8_t ack[2] = {OTA_RESP_ACK, block_seq};
            (void)isotp_send_pdu(&s_isotp_link, ack, sizeof(ack));
            break;
        }

        case CMD_VERIFY_FIRMWARE:
        {
            if (!s_target_selected)
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_NOT_SELECTED};
                (void)isotp_send_pdu(&s_isotp_link, nack, sizeof(nack));
                return;
            }

            uint32_t expected_total_size = 0U;
            uint32_t expected_full_crc = 0U;

            if (length >= 9U)
            {
                expected_total_size = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) |
                                      ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
                expected_full_crc   = (uint32_t)payload[5] | ((uint32_t)payload[6] << 8) |
                                      ((uint32_t)payload[7] << 16) | ((uint32_t)payload[8] << 24);
            }

            uint32_t final_crc = crc32_finalize(s_running_crc32);

            if (s_total_bytes_received == expected_total_size && final_crc == expected_full_crc)
            {
                safe_printf(TAG, "[OTA DUMMY] Verification SUCCESS! Received=%lu B, Full CRC=0x%08lX matched!\r\n",
                            (unsigned long)s_total_bytes_received, (unsigned long)final_crc);
                uint8_t ack[1] = {OTA_RESP_ACK};
                (void)isotp_send_pdu(&s_isotp_link, ack, sizeof(ack));
            }
            else
            {
                safe_printf(TAG, "[OTA DUMMY] Verification FAILED! Recv=%lu B (exp %lu), CRC=0x%08lX (exp 0x%08lX)\r\n",
                            (unsigned long)s_total_bytes_received, (unsigned long)expected_total_size,
                            (unsigned long)final_crc, (unsigned long)expected_full_crc);
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_CRC};
                (void)isotp_send_pdu(&s_isotp_link, nack, sizeof(nack));
            }
            break;
        }

        case CMD_GO_EXECUTE:
        {
            safe_printf(TAG, "[OTA DUMMY] CMD_GO_EXECUTE received -> ACK sent\r\n");
            uint8_t ack[1] = {OTA_RESP_ACK};
            (void)isotp_send_pdu(&s_isotp_link, ack, sizeof(ack));
            break;
        }

        default:
        {
            if (s_target_selected)
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_UNKNOWN_CMD};
                (void)isotp_send_pdu(&s_isotp_link, nack, sizeof(nack));
            }
            break;
        }
    }
}

/**
 * @brief Background worker task processing incoming OTA frames asynchronously.
 */
static void ota_worker_task(void *pvParameters)
{
    (void)pvParameters;
    CAN_msg_t rx_msg;

    safe_printf(TAG, "OTA Worker Task started\r\n");

    for (;;)
    {
        if (xQueueReceive(s_ota_rx_queue, &rx_msg, portMAX_DELAY) == pdTRUE)
        {
            uint32_t can_id = CAN_msg_get_can_id(rx_msg.ident);

            if (can_id == CAN_ID_OTA_BROADCAST)
            {
                /* Check if payload is CMD_DISCOVER_NODES */
                if (rx_msg.len >= 2U && rx_msg.data[1] == CMD_DISCOVER_NODES)
                {
                    handle_discovery_cmd();
                }
            }
            else if (can_id == CAN_ID_ISOTP_REQ)
            {
                isotp_on_can_frame(&s_isotp_link, can_id, rx_msg.data, rx_msg.len);
            }
        }
    }
}

/* --- Public API ------------------------------------------------------------*/

bool ota_manager_init(can_instance_t *can2_inst)
{
    if (can2_inst == NULL)
    {
        return false;
    }

    s_can2_inst = can2_inst;

    /* Read factory 96-bit Unique Device ID into cache using strictly 32-bit word accesses.
     * The STM32 Information Block / OTP bus does not support 8-bit or 16-bit accesses
     * and triggers an immediate BusFault / HardFault if LDRB/LDRH is used. */
    uint32_t uid_words[3];
    uid_words[0] = *((const volatile uint32_t *)(STM32C5_UID_BASE + 0x00U));
    uid_words[1] = *((const volatile uint32_t *)(STM32C5_UID_BASE + 0x04U));
    uid_words[2] = *((const volatile uint32_t *)(STM32C5_UID_BASE + 0x08U));
    memcpy(s_mcu_uid, uid_words, sizeof(s_mcu_uid));

    safe_printf(TAG, "Initialized. MCU UID: %02X%02X%02X%02X-%02X%02X%02X%02X-%02X%02X%02X%02X\r\n",
                s_mcu_uid[0], s_mcu_uid[1], s_mcu_uid[2], s_mcu_uid[3],
                s_mcu_uid[4], s_mcu_uid[5], s_mcu_uid[6], s_mcu_uid[7],
                s_mcu_uid[8], s_mcu_uid[9], s_mcu_uid[10], s_mcu_uid[11]);

    /* Create non-blocking queue for incoming CAN messages */
    s_ota_rx_queue = xQueueCreate(OTA_QUEUE_LENGTH, sizeof(CAN_msg_t));
    if (s_ota_rx_queue == NULL)
    {
        return false;
    }

    /* Initialize ISO-TP transport link */
    isotp_init(&s_isotp_link,
               CAN_ID_ISOTP_REQ,
               CAN_ID_ISOTP_RESP,
               ota_can_send_frame,
               handle_isotp_pdu,
               s_can2_inst);

    /* Create OTA worker task */
    BaseType_t ret = xTaskCreate(ota_worker_task,
                                "ota_worker",
                                TASK_STACK_OTA_WORKER,
                                NULL,
                                TASK_PRIO_OTA_WORKER,
                                NULL);

    return (ret == pdPASS);
}

void ota_manager_feed_can_frame(const CAN_msg_t *msg)
{
    if (msg != NULL && s_ota_rx_queue != NULL)
    {
        (void)xQueueSend(s_ota_rx_queue, msg, 0);
    }
}

bool ota_manager_is_target_selected(void)
{
    return s_target_selected;
}

const uint8_t *ota_manager_get_uid(void)
{
    return s_mcu_uid;
}
