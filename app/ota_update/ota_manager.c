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
#include "stm32_hal.h"
#include "boot_shared.h"
#include "fault_recovery.h"
#include "bootloader_updater.h"
#include "boot_version_record.h"
#include <string.h>

#define TAG "OTA_MGR"

_Static_assert((OTA_MAX_BLOCK_SIZE + OTA_WRITE_PDU_OVERHEAD) <= ISOTP_RX_BUF_SIZE,
               "ISO-TP RX buffer too small for the largest OTA write block");

/** @brief Length of the incoming frame queue (must hold at least one ISO-TP block of OTA_ISOTP_FC_BLOCK_SIZE frames plus FF/FC slack). */
#define OTA_QUEUE_LENGTH                64U

/**
 * @brief ISO-TP Flow Control Block Size advertised to the PC tool.
 * @details Frames travel ISR -> can_rx_task -> s_ota_rx_queue -> ota_worker. Limiting the sender to 16
 *          back-to-back frames per Flow Control guarantees the queue never overflows, even when the
 *          low-priority ota_worker is delayed by other tasks.
 */
#define OTA_ISOTP_FC_BLOCK_SIZE         16U

/** @brief ISO-TP Flow Control STmin advertised to the PC tool (0 = back-to-back frames within a block). */
#define OTA_ISOTP_FC_ST_MIN             0U

_Static_assert(OTA_ISOTP_FC_BLOCK_SIZE < OTA_QUEUE_LENGTH, "OTA queue must hold a full ISO-TP block");

/** @brief Static storage for cached 96-bit MCU UID. */
static uint8_t s_mcu_uid[STM32C5_UID_LEN_BYTES];

/** @brief Target selection flag (true if this node's UID matched CMD_SELECT_TARGET). */
static bool s_target_selected = false;

/** @brief Bootloader update session flag (true when staging slot 0x08038000 is targeted). */
static bool s_bootloader_update_active = false;

/** @brief Pointer to the currently active CAN instance used for OTA transmission (CAN1 or CAN2). */
static can_instance_t *s_active_can_inst = NULL;

/** @brief Timestamp of the last accepted OTA frame to support idle timeout and duplicate rejection. */
static TickType_t s_last_ota_rx_tick = 0U;

/** @brief Timestamp of the last accepted broadcast discovery frame. */
static TickType_t s_last_discovery_tick = 0U;
static bool s_discovery_received = false;

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
 * @brief Helper callback for ISO-TP to transmit a 29-bit Extended CAN frame over the active CAN bus.
 */
static bool ota_can_send_frame(uint32_t can_id, const uint8_t *data, uint8_t len, void *user_ctx)
{
    can_instance_t *inst = (s_active_can_inst != NULL) ? s_active_can_inst : (can_instance_t *)user_ctx;
    if (inst == NULL || data == NULL || len == 0U)
    {
        return false;
    }

    CAN_msg_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.ident = CAN_msg_set_extended_id(can_id);
    frame.format = CAN_MSG_FRAME_FORMAT_CAN;
    frame.len = len;
    memcpy(frame.data, data, len);

    return can_manager_send(inst, &frame);
}

/**
 * @brief Handles Discovery command received via Broadcast ID 0x01F2F3DF.
 */
static void handle_discovery_cmd(void)
{
    /* Calculate 8-bit XOR hash and 32-bit CRC32 from our 12-byte UID */
    uint8_t uid_hash = 0U;
    for (size_t i = 0; i < STM32C5_UID_LEN_BYTES; i++)
    {
        uid_hash ^= s_mcu_uid[i];
    }
    uint32_t uid_crc = crc32_calculate(s_mcu_uid, STM32C5_UID_LEN_BYTES);

    /* Anti-collision prime-modulo slot delay: 10 to 332 ms (prevents burst contention) */
    uint32_t slot_delay_ms = 10U + ((uint32_t)(uid_crc ^ ((uint32_t)uid_hash << 16)) % 47U) * 7U;
    vTaskDelay(pdMS_TO_TICKS(slot_delay_ms));

    /* Node-specific 29-bit CAN Extended ID guarantees non-destructive hardware arbitration */
    uint32_t resp_id = CAN_ID_OTA_DISCOVERY_RESP_NODE(uid_hash);

    const app_version_info_t *ver = version_getInfo();
    uint8_t packed_ver = (uint8_t)(((ver->fw_major & 0x0FU) << 4) | (ver->fw_minor & 0x0FU));

    /* Frame 1: [0x01, UID[0..6]] (8 bytes total) */
    uint8_t frame1[8];
    frame1[0] = 0x01U;
    memcpy(&frame1[1], &s_mcu_uid[0], 7U);
    (void)ota_can_send_frame(resp_id, frame1, sizeof(frame1), s_active_can_inst);

    /* Frame 2: [0x02, UID[7..11], Mode=1 (APP), PackedVersion] (8 bytes total) */
    uint8_t frame2[8];
    frame2[0] = 0x02U;
    memcpy(&frame2[1], &s_mcu_uid[7], 5U);
    frame2[6] = OTA_MODE_APPLICATION;
    frame2[7] = packed_ver;
    (void)ota_can_send_frame(resp_id, frame2, sizeof(frame2), s_active_can_inst);

    /* Frame 3: [0x03, AppMaj, AppMin, AppPatch, BootMaj, BootMin, BootPatch, 0x00]
     * Full 8-bit versions; installed bootloader version read from its Flash record (0xFF if absent) */
    const boot_version_record_t *boot_rec = boot_version_record_get(BOOTLOADER_FLASH_BASE);
    uint8_t frame3[8] = {
        0x03U, ver->fw_major, ver->fw_minor, ver->fw_patch,
        (boot_rec != NULL) ? boot_rec->major : 0xFFU,
        (boot_rec != NULL) ? boot_rec->minor : 0xFFU,
        (boot_rec != NULL) ? boot_rec->patch : 0xFFU,
        0x00U
    };
    (void)ota_can_send_frame(resp_id, frame3, sizeof(frame3), s_active_can_inst);

    safe_printf(TAG, "[DISCOVERY] Responded on 0x%08lX (%s) after %lu ms slot (UID: %02X%02X%02X%02X...)\r\n",
                (unsigned long)resp_id,
                (s_active_can_inst != NULL) ? s_active_can_inst->name : "CAN",
                (unsigned long)slot_delay_ms, s_mcu_uid[0], s_mcu_uid[1], s_mcu_uid[2], s_mcu_uid[3]);
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

            uint8_t active_bus = BOOT_CAN_BUS_AUTO;
            if (s_active_can_inst != NULL)
            {
                if (strcmp(s_active_can_inst->name, "CAN1") == 0)
                {
                    active_bus = BOOT_CAN_BUS_FDCAN1;
                }
                else if (strcmp(s_active_can_inst->name, "CAN2") == 0)
                {
                    active_bus = BOOT_CAN_BUS_FDCAN2;
                }
            }

            safe_printf(TAG, "[CMD] ENTER_BOOTLOADER requested on %s! Saving state to SHARED_RAM and resetting...\r\n",
                        (s_active_can_inst != NULL) ? s_active_can_inst->name : "AUTO");
            uint8_t resp[2] = {OTA_RESP_ACK, OTA_MODE_APPLICATION};
            (void)isotp_send_pdu(&s_isotp_link, resp, sizeof(resp));

            boot_shared_request_bootloader(s_target_selected, active_bus);
            vTaskDelay(pdMS_TO_TICKS(30));
            NVIC_SystemReset();
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

            if (erase_addr == BOOTLOADER_STAGING_BASE)
            {
                s_bootloader_update_active = true;
                safe_printf(TAG, "[BOOTLOADER UPDATE] Erasing staging slot 0x%08lX (%lu B)...\r\n",
                            (unsigned long)erase_addr, (unsigned long)erase_size);

                if (!bootloader_updater_erase_staging(erase_size))
                {
                    uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_FLASH};
                    (void)isotp_send_pdu(&s_isotp_link, nack, sizeof(nack));
                    return;
                }
            }
            else
            {
                s_bootloader_update_active = false;
                safe_printf(TAG, "[OTA DUMMY] Erase simulated: addr=0x%08lX, size=%lu B\r\n",
                            (unsigned long)erase_addr, (unsigned long)erase_size);
            }

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

            if ((block_len > OTA_MAX_BLOCK_SIZE) || (length < (7U + block_len + 4U)))
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
                safe_printf(TAG, "[OTA] Block CRC mismatch! Exp=0x%08lX, Calc=0x%08lX\r\n",
                            (unsigned long)expected_crc, (unsigned long)actual_crc);
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_CRC};
                (void)isotp_send_pdu(&s_isotp_link, nack, sizeof(nack));
                return;
            }

            /* Flash into Staging Slot if Bootloader update is active */
            if (s_bootloader_update_active && (block_addr >= BOOTLOADER_STAGING_BASE) &&
                ((block_addr + block_len) <= (BOOTLOADER_STAGING_BASE + BOOTLOADER_STAGING_SIZE_BYTES)))
            {
                if (!bootloader_updater_write_staging(block_addr, &payload[7], block_len))
                {
                    safe_printf(TAG, "[BOOTLOADER UPDATE] Write failed at 0x%08lX\r\n", (unsigned long)block_addr);
                    uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_FLASH};
                    (void)isotp_send_pdu(&s_isotp_link, nack, sizeof(nack));
                    return;
                }
            }

            /* Update global stream CRC32 accumulator with block payload only */
            s_running_crc32 = crc32_update(s_running_crc32, &payload[7], block_len);
            s_total_bytes_received += block_len;

            uint8_t block_seq = (uint8_t)(s_block_count++ & 0xFFU);

            if ((s_block_count % 16U) == 0U)
            {
                safe_printf(TAG, "[OTA] Received block #%lu (addr=0x%08lX, %lu B total)\r\n",
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
                safe_printf(TAG, "[OTA] Verification SUCCESS! Received=%lu B, Full CRC=0x%08lX matched!\r\n",
                            (unsigned long)s_total_bytes_received, (unsigned long)final_crc);
                uint8_t ack[1] = {OTA_RESP_ACK};
                (void)isotp_send_pdu(&s_isotp_link, ack, sizeof(ack));
            }
            else
            {
                safe_printf(TAG, "[OTA] Verification FAILED! Recv=%lu B (exp %lu), CRC=0x%08lX (exp 0x%08lX)\r\n",
                            (unsigned long)s_total_bytes_received, (unsigned long)expected_total_size,
                            (unsigned long)final_crc, (unsigned long)expected_full_crc);
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_CRC};
                (void)isotp_send_pdu(&s_isotp_link, nack, sizeof(nack));
            }
            break;
        }

        case CMD_UPDATE_BOOTLOADER:
        {
            if (!s_target_selected)
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_NOT_SELECTED};
                (void)isotp_send_pdu(&s_isotp_link, nack, sizeof(nack));
                return;
            }

            uint32_t exp_size = 0U;
            uint32_t exp_crc  = 0U;
            if (length >= 9U)
            {
                exp_size = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) |
                           ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
                exp_crc  = (uint32_t)payload[5] | ((uint32_t)payload[6] << 8) |
                           ((uint32_t)payload[7] << 16) | ((uint32_t)payload[8] << 24);
            }

            safe_printf(TAG, "[BOOT_UPDATE] Install requested! Size=%lu B, CRC=0x%08lX\r\n",
                        (unsigned long)exp_size, (unsigned long)exp_crc);

            bool ok = bootloader_updater_install(exp_size, exp_crc);
            if (ok)
            {
                safe_printf(TAG, "[BOOT_UPDATE] Bootloader successfully installed at 0x08000000!\r\n");
                uint8_t ack[1] = {OTA_RESP_ACK};
                (void)isotp_send_pdu(&s_isotp_link, ack, sizeof(ack));
            }
            else
            {
                safe_printf(TAG, "[BOOT_UPDATE] Installation FAILED!\r\n");
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_FLASH};
                (void)isotp_send_pdu(&s_isotp_link, nack, sizeof(nack));
            }
            break;
        }

        case CMD_GO_EXECUTE:
        {
            if (s_bootloader_update_active)
            {
                safe_printf(TAG, "[BOOT_UPDATE] Resetting MCU to launch new Bootloader...\r\n");
                uint8_t ack[1] = {OTA_RESP_ACK};
                (void)isotp_send_pdu(&s_isotp_link, ack, sizeof(ack));
                vTaskDelay(pdMS_TO_TICKS(50));
                NVIC_SystemReset();
            }
            else
            {
                safe_printf(TAG, "[OTA DUMMY] CMD_GO_EXECUTE received -> ACK sent\r\n");
                uint8_t ack[1] = {OTA_RESP_ACK};
                (void)isotp_send_pdu(&s_isotp_link, ack, sizeof(ack));
            }
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

    bool healthy_confirmed = false;

    for (;;)
    {
        /* Check if system has run stably for 3000 ms (clears boot failure counter) */
        if (!healthy_confirmed && (xTaskGetTickCount() >= pdMS_TO_TICKS(3000U)))
        {
            healthy_confirmed = true;
            fault_recovery_confirm_app_healthy();
            safe_printf(TAG, "[HEALTHY] Application ran stably for 3s. Boot fail counter cleared.\r\n");
        }

        if (xQueueReceive(s_ota_rx_queue, &rx_msg, pdMS_TO_TICKS(500U)) == pdTRUE)
        {
            /* Extract 29-bit Extended CAN ID directly without TX helper macros (Rule 13) */
            if (rx_msg.ident <= CAN_SFF_MASK)
            {
                continue;
            }
            uint32_t can_id = rx_msg.ident & CAN_EFF_MASK;

            if (can_id == CAN_ID_OTA_BROADCAST)
            {
                /* Check if payload is CMD_DISCOVER_NODES with valid 4-byte "VBOX" magic token */
                if ((rx_msg.len >= OTA_DISCOVERY_MIN_LEN) &&
                    (rx_msg.data[1] == CMD_DISCOVER_NODES) &&
                    (rx_msg.data[2] == OTA_MAGIC_TOKEN_0) &&
                    (rx_msg.data[3] == OTA_MAGIC_TOKEN_1) &&
                    (rx_msg.data[4] == OTA_MAGIC_TOKEN_2) &&
                    (rx_msg.data[5] == OTA_MAGIC_TOKEN_3))
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

    s_active_can_inst = can2_inst;
    (void)bootloader_updater_init();

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
               s_active_can_inst);
    isotp_set_flow_control(&s_isotp_link, OTA_ISOTP_FC_BLOCK_SIZE, OTA_ISOTP_FC_ST_MIN);

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
    if (msg == NULL || s_ota_rx_queue == NULL)
    {
        return;
    }

    can_instance_t *rx_inst = can_manager_get_active_rx_instance();
    if (rx_inst == NULL)
    {
        rx_inst = s_active_can_inst;
    }

    TickType_t now = xTaskGetTickCount();
    uint32_t can_id = msg->ident & CAN_EFF_MASK;

    /* Handle Broadcast Discovery on 0x01F2F3DF */
    if (can_id == CAN_ID_OTA_BROADCAST)
    {
        /* If both CAN1 and CAN2 share the physical bus, drop duplicate discovery frames arriving within 100 ms */
        if (s_discovery_received && (s_active_can_inst != NULL) && (rx_inst != s_active_can_inst) &&
            ((now - s_last_discovery_tick) < pdMS_TO_TICKS(100U)))
        {
            return;
        }

        s_discovery_received = true;
        s_last_discovery_tick = now;
        s_last_ota_rx_tick = now;
        if (rx_inst != NULL)
        {
            s_active_can_inst = rx_inst;
        }

        (void)xQueueSend(s_ota_rx_queue, msg, 0);
        return;
    }

    /* Handle ISO-TP Requests on 0x01F2F3E0 */
    if (can_id == CAN_ID_ISOTP_REQ)
    {
        uint8_t pci_type = (msg->len > 0U) ? (msg->data[0] & 0xF0U) : 0xFFU;

        /* If an active session is in progress (target selected or recent frames received),
         * strictly accept frames ONLY from the active CAN instance to prevent duplicate frames from
         * a shared/bridged bus corrupting the ISO-TP sequence counter. */
        if (s_active_can_inst != NULL && rx_inst != NULL && rx_inst != s_active_can_inst)
        {
            if (s_target_selected || ((now - s_last_ota_rx_tick) < pdMS_TO_TICKS(1000U)))
            {
                /* Drop duplicate frame from the other bus */
                return;
            }
            /* Session has been idle (> 1s) and target is not locked: switch to the newly active bus */
            s_active_can_inst = rx_inst;
        }

        /* If this is a Single Frame (0x00) or First Frame (0x10), it initiates a new PDU */
        if (pci_type == ISOTP_PCI_TYPE_SINGLE || pci_type == ISOTP_PCI_TYPE_FIRST)
        {
            if (rx_inst != NULL)
            {
                s_active_can_inst = rx_inst;
            }
        }

        s_last_ota_rx_tick = now;
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
