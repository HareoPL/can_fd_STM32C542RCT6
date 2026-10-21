/**
 * @file openbl_core.c
 * @brief Bootloader core command parser, Flash driver integration and ISO-TP transport for STM32C542.
 */

#include "openbl_core.h"
#include "openbl_flash_c5.h"
#include "boot_jump.h"
#include "boot_shared.h"
#include "ota_protocol.h"
#include "isotp.h"
#include "crc32.h"
#include "mx_fdcan1.h"
#include "mx_fdcan2.h"
#include "safe_printf.h"
#include "boot_version_config.h"
#include <string.h>

#define TAG                     "BOOT"

_Static_assert((BOOT_VERSION_MAJOR <= 15U) && (BOOT_VERSION_MINOR <= 15U),
               "Bootloader MAJOR/MINOR must fit the 4-bit fields of the packed discovery version byte");

/** @brief Bootloader version packed as (major << 4 | minor) for the discovery response (from boot-vX.Y.Z tag). */
#define PACKED_BOOT_VERSION     ((uint8_t)((BOOT_VERSION_MAJOR << 4) | BOOT_VERSION_MINOR))

/** @brief ISO-TP Flow Control Block Size: bare-metal polling drains RX FIFO0 every ~10 us, no limit needed. */
#define BOOT_ISOTP_FC_BLOCK_SIZE    0U

/** @brief ISO-TP Flow Control STmin: back-to-back frames (raise to 0xF5 = 500 us if the PC adapter drops frames). */
#define BOOT_ISOTP_FC_ST_MIN        0U

_Static_assert((OTA_MAX_BLOCK_SIZE + OTA_WRITE_PDU_OVERHEAD) <= ISOTP_RX_BUF_SIZE,
               "ISO-TP RX buffer too small for the largest OTA write block");

static hal_fdcan_handle_t *s_hfdcan1 = NULL;
static hal_fdcan_handle_t *s_hfdcan2 = NULL;
static hal_fdcan_handle_t *s_active_hfdcan = NULL;
static boot_mode_t s_boot_mode = BOOT_MODE_STAY_IN_BOOT;
static isotp_link_t s_boot_isotp;
static uint8_t s_mcu_uid[STM32C5_UID_LEN_BYTES];
static bool s_target_selected = false;
static uint32_t s_stream_crc = 0U;
static uint32_t s_total_bytes_written = 0U;
static uint32_t s_expected_addr = BOOT_APP_START_ADDR;
static uint32_t s_rx_fifo_lost_events = 0U;   /**< RX FIFO0 overflow events observed since the last erase */

/**
 * @brief Deferred vector table block (0x08008000) buffer.
 * @details Held in RAM and programmed to Flash only upon successful CMD_VERIFY_FIRMWARE.
 *          Guarantees that an interrupted download leaves Flash at 0x08008000 in erased state
 *          (0xFFFFFFFF), forcing the Bootloader into Recovery Mode instead of jumping to partial code.
 */
static uint8_t s_deferred_vector_block[OTA_MAX_BLOCK_SIZE];
static uint16_t s_deferred_vector_len = 0U;
static bool s_deferred_vector_pending = false;

/**
 * @brief Converts byte length (0..64) to FDCAN DLC enum.
 */
static hal_fdcan_data_length_code_t bytes_to_fdcan_dlc(uint8_t len)
{
    if (len <= 8U)  return (hal_fdcan_data_length_code_t)len;
    if (len <= 12U) return HAL_FDCAN_DATA_LEN_FDCAN_12_BYTE;
    if (len <= 16U) return HAL_FDCAN_DATA_LEN_FDCAN_16_BYTE;
    if (len <= 20U) return HAL_FDCAN_DATA_LEN_FDCAN_20_BYTE;
    if (len <= 24U) return HAL_FDCAN_DATA_LEN_FDCAN_24_BYTE;
    if (len <= 32U) return HAL_FDCAN_DATA_LEN_FDCAN_32_BYTE;
    if (len <= 48U) return HAL_FDCAN_DATA_LEN_FDCAN_48_BYTE;
    return HAL_FDCAN_DATA_LEN_FDCAN_64_BYTE;
}

/**
 * @brief Converts FDCAN DLC enum to byte length.
 */
static uint8_t fdcan_dlc_to_bytes(hal_fdcan_data_length_code_t dlc)
{
    switch (dlc)
    {
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_0_BYTE: return 0U;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_1_BYTE: return 1U;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_2_BYTE: return 2U;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_3_BYTE: return 3U;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_4_BYTE: return 4U;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_5_BYTE: return 5U;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_6_BYTE: return 6U;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_7_BYTE: return 7U;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_8_BYTE: return 8U;
        case HAL_FDCAN_DATA_LEN_FDCAN_12_BYTE:    return 12U;
        case HAL_FDCAN_DATA_LEN_FDCAN_16_BYTE:    return 16U;
        case HAL_FDCAN_DATA_LEN_FDCAN_20_BYTE:    return 20U;
        case HAL_FDCAN_DATA_LEN_FDCAN_24_BYTE:    return 24U;
        case HAL_FDCAN_DATA_LEN_FDCAN_32_BYTE:    return 32U;
        case HAL_FDCAN_DATA_LEN_FDCAN_48_BYTE:    return 48U;
        case HAL_FDCAN_DATA_LEN_FDCAN_64_BYTE:    return 64U;
        default:                                  return 8U;
    }
}

/**
 * @brief Low-level 29-bit Extended CAN frame transmission routine for ISO-TP and discovery announcements.
 */
static bool boot_can_send_frame(uint32_t id, const uint8_t *data, uint8_t len)
{
    hal_fdcan_handle_t *hfdcan = (s_active_hfdcan != NULL) ? s_active_hfdcan : s_hfdcan1;
    if (hfdcan == NULL)
    {
        hfdcan = s_hfdcan2;
    }

    if (hfdcan == NULL || data == NULL)
    {
        return false;
    }

    if (HAL_FDCAN_GetState(hfdcan) != HAL_FDCAN_STATE_ACTIVE)
    {
        if (HAL_FDCAN_Start(hfdcan) != HAL_OK)
        {
            return false;
        }
    }

    /* Wait for free TX buffer (up to 10 ms polling) */
    uint32_t t_timeout = 50000U;
    while ((HAL_FDCAN_GetTxFifoFreeLevel(hfdcan) == 0U) && (t_timeout > 0U))
    {
        t_timeout--;
    }

    if (HAL_FDCAN_GetTxFifoFreeLevel(hfdcan) == 0U)
    {
        return false;
    }

    hal_fdcan_tx_header_t tx_header = {0};
    tx_header.b.identifier = id & 0x1FFFFFFFUL;
    tx_header.b.identifier_type = HAL_FDCAN_ID_EXTENDED;
    tx_header.b.frame_type = HAL_FDCAN_FRAME_DATA;
    tx_header.b.data_length = bytes_to_fdcan_dlc(len);
    tx_header.b.frame_format = HAL_FDCAN_HEADER_FRAME_FORMAT_CAN;
    tx_header.b.bit_rate_switch = HAL_FDCAN_BIT_RATE_SWITCH_OFF;

    return (HAL_FDCAN_ReqTransmitMsgFromFIFOQ(hfdcan, &tx_header, data) == HAL_OK);
}

/**
 * @brief ISO-TP hardware transmission callback adapter.
 */
static bool boot_isotp_can_tx_fn(uint32_t id, const uint8_t *data, uint8_t len, void *user_ctx)
{
    (void)user_ctx;
    return boot_can_send_frame(id, data, len);
}

/**
 * @brief Handles broadcast discovery request CMD_DISCOVER_NODES (0xA0) from Extended ID 0x01F2F3DF.
 */
static void handle_broadcast_discovery(const uint8_t *data, uint8_t len)
{
    if ((data == NULL) || (len < OTA_DISCOVERY_MIN_LEN))
    {
        return;
    }

    /* Validate command code and 4-byte "VBOX" authorization token */
    if ((data[1] != CMD_DISCOVER_NODES) ||
        (data[2] != OTA_MAGIC_TOKEN_0)  ||
        (data[3] != OTA_MAGIC_TOKEN_1)  ||
        (data[4] != OTA_MAGIC_TOKEN_2)  ||
        (data[5] != OTA_MAGIC_TOKEN_3))
    {
        return;
    }

    /* Calculate 8-bit XOR hash and 32-bit CRC32 from our 12-byte UID */
    uint8_t uid_hash = 0U;
    for (size_t i = 0; i < STM32C5_UID_LEN_BYTES; i++)
    {
        uid_hash ^= s_mcu_uid[i];
    }
    uint32_t uid_crc = crc32_calculate(s_mcu_uid, STM32C5_UID_LEN_BYTES);

    /* Anti-collision prime-modulo slot delay: 10 to 332 ms (prevents simultaneous TX in bare-metal) */
    uint32_t slot_delay_ms = 10U + ((uint32_t)(uid_crc ^ ((uint32_t)uid_hash << 16)) % 47U) * 7U;
    for (volatile uint32_t i = 0; i < slot_delay_ms * 36000U; i++)
    {
        __NOP();
    }

    /* Node-specific 29-bit CAN Extended ID guarantees non-destructive hardware arbitration */
    uint32_t resp_id = CAN_ID_OTA_DISCOVERY_RESP_NODE(uid_hash);

    uint8_t reported_mode = (s_boot_mode == BOOT_MODE_RECOVERY) ? OTA_MODE_RECOVERY : OTA_MODE_BOOTLOADER;

    /* Frame 1: [0x01, UID[0..6]] (8 bytes total) */
    uint8_t frame1[8];
    frame1[0] = 0x01U;
    memcpy(&frame1[1], &s_mcu_uid[0], 7U);
    (void)boot_can_send_frame(resp_id, frame1, sizeof(frame1));

    /* Frame 2: [0x02, UID[7..11], Mode (BOOTLOADER=0 or RECOVERY=2), PackedVersion] (8 bytes total) */
    uint8_t frame2[8];
    frame2[0] = 0x02U;
    memcpy(&frame2[1], &s_mcu_uid[7], 5U);
    frame2[6] = reported_mode;
    frame2[7] = PACKED_BOOT_VERSION;
    (void)boot_can_send_frame(resp_id, frame2, sizeof(frame2));

    /* On-demand diagnostic crash beacon in Recovery Mode (zero unsolicited traffic on vehicle bus) */
    if (s_boot_mode == BOOT_MODE_RECOVERY)
    {
        openbl_core_send_recovery_beacon((uint8_t)g_boot_shared.fault_reason,
                                         (uint8_t)g_boot_shared.boot_fail_counter,
                                         g_boot_shared.fault_pc);
    }

    safe_printf(TAG, "[DISCOVERY] Responded on 0x%08lX (%s, mode=%u) after %lu ms slot\r\n",
                (unsigned long)resp_id,
                (s_active_hfdcan == s_hfdcan1) ? "CAN1" : "CAN2",
                (unsigned int)reported_mode,
                (unsigned long)slot_delay_ms);
}

/**
 * @brief ISO-TP PDU received callback (dispatches bootloader commands).
 */
static void boot_isotp_pdu_cb(const uint8_t *payload, uint16_t length, void *user_ctx)
{
    (void)user_ctx;

    if ((payload == NULL) || (length == 0U))
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
                uint8_t resp[5] = {
                    OTA_RESP_ACK,
                    OTA_MODE_BOOTLOADER,
                    BOOT_VERSION_MAJOR, BOOT_VERSION_MINOR, BOOT_VERSION_PATCH
                };
                (void)isotp_send_pdu(&s_boot_isotp, resp, sizeof(resp));
                safe_printf(TAG, "[SELECT] Device SELECTED by UID! ACK sent.\r\n");
            }
            else
            {
                s_target_selected = false;
                safe_printf(TAG, "[SELECT] UID mismatch. Device unselected.\r\n");
            }
            break;
        }

        case CMD_ENTER_BOOTLOADER:
        {
            if (!s_target_selected)
            {
                return;
            }

            uint8_t resp[2] = {OTA_RESP_ACK, OTA_MODE_BOOTLOADER};
            (void)isotp_send_pdu(&s_boot_isotp, resp, sizeof(resp));
            safe_printf(TAG, "[CMD] Already in Bootloader mode! ACK sent.\r\n");
            break;
        }

        case CMD_ERASE_APP:
        {
            if (!s_target_selected)
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_NOT_SELECTED};
                (void)isotp_send_pdu(&s_boot_isotp, nack, sizeof(nack));
                return;
            }

            if (length < 9U)
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_UNKNOWN_CMD};
                (void)isotp_send_pdu(&s_boot_isotp, nack, sizeof(nack));
                return;
            }

            uint32_t erase_addr = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) |
                                  ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
            uint32_t erase_size = (uint32_t)payload[5] | ((uint32_t)payload[6] << 8) |
                                  ((uint32_t)payload[7] << 16) | ((uint32_t)payload[8] << 24);

            safe_printf(TAG, "[ERASE] Erasing Flash 0x%08lX, size=%lu B...\r\n", erase_addr, erase_size);

            hal_status_t st = openbl_flash_erase(erase_addr, erase_size);
            if (st == HAL_OK)
            {
                s_stream_crc = crc32_init();
                s_total_bytes_written = 0U;
                s_expected_addr = erase_addr;
                s_deferred_vector_pending = false;
                s_deferred_vector_len = 0U;
                s_rx_fifo_lost_events = 0U;

                uint8_t ack[1] = {OTA_RESP_ACK};
                (void)isotp_send_pdu(&s_boot_isotp, ack, sizeof(ack));
                safe_printf(TAG, "[ERASE] Flash erase successful! ACK sent.\r\n");
            }
            else
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_FLASH};
                (void)isotp_send_pdu(&s_boot_isotp, nack, sizeof(nack));
                safe_printf(TAG, "[ERASE] Flash erase failed (code=%d)!\r\n", (int)st);
            }
            break;
        }

        case CMD_WRITE_MEMORY:
        {
            if (!s_target_selected)
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_NOT_SELECTED};
                (void)isotp_send_pdu(&s_boot_isotp, nack, sizeof(nack));
                return;
            }

            if (length < 11U)
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_UNKNOWN_CMD};
                (void)isotp_send_pdu(&s_boot_isotp, nack, sizeof(nack));
                return;
            }

            uint32_t addr = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) |
                            ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
            uint16_t blk_len = (uint16_t)payload[5] | ((uint16_t)payload[6] << 8);

            if ((blk_len > OTA_MAX_BLOCK_SIZE) || (length < (7U + blk_len + 4U)))
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_SEQUENCE};
                (void)isotp_send_pdu(&s_boot_isotp, nack, sizeof(nack));
                return;
            }

            uint32_t expected_block_crc = (uint32_t)payload[7U + blk_len] |
                                         ((uint32_t)payload[7U + blk_len + 1U] << 8) |
                                         ((uint32_t)payload[7U + blk_len + 2U] << 16) |
                                         ((uint32_t)payload[7U + blk_len + 3U] << 24);

            /* Calculate block CRC: addr(4B) + len(2B) + data */
            uint32_t computed_block_crc = crc32_calculate(&payload[1], 6U + blk_len);
            if (computed_block_crc != expected_block_crc)
            {
                safe_printf(TAG, "[WRITE] CRC error at 0x%08lX (calc=0x%08lX, exp=0x%08lX)\r\n",
                            addr, computed_block_crc, expected_block_crc);
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_CRC};
                (void)isotp_send_pdu(&s_boot_isotp, nack, sizeof(nack));
                return;
            }

            /* Flash programming:
             * If writing the first block at BOOT_APP_START_ADDR (vector table), buffer it in RAM
             * and defer Flash programming until CMD_VERIFY_FIRMWARE succeeds. This ensures
             * that an interrupted update leaves 0x08008000 in erased state (0xFFFFFFFF). */
            if (addr == BOOT_APP_START_ADDR)
            {
                /* blk_len <= OTA_MAX_BLOCK_SIZE is guaranteed above, so the block always fits */
                memcpy(s_deferred_vector_block, &payload[7], blk_len);
                s_deferred_vector_len = blk_len;
                s_deferred_vector_pending = true;
                safe_printf(TAG, "[WRITE] Deferred vector table at 0x%08lX (%u B) until verification\r\n",
                            addr, (unsigned int)blk_len);
            }
            else
            {
                hal_status_t st = openbl_flash_write(addr, &payload[7], blk_len);
                if (st != HAL_OK)
                {
                    safe_printf(TAG, "[WRITE] Flash write/verify failed at 0x%08lX (st=%d)\r\n", addr, (int)st);
                    uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_FLASH};
                    (void)isotp_send_pdu(&s_boot_isotp, nack, sizeof(nack));
                    return;
                }
            }

            /* Accumulate into full stream CRC32 */
            s_stream_crc = crc32_update(s_stream_crc, &payload[7], blk_len);
            s_total_bytes_written += blk_len;
            s_expected_addr = addr + blk_len;

            uint8_t ack[2] = {OTA_RESP_ACK, (uint8_t)(s_total_bytes_written / 256U)};
            (void)isotp_send_pdu(&s_boot_isotp, ack, sizeof(ack));
            break;
        }

        case CMD_VERIFY_FIRMWARE:
        {
            if (!s_target_selected)
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_NOT_SELECTED};
                (void)isotp_send_pdu(&s_boot_isotp, nack, sizeof(nack));
                return;
            }

            if (length < 9U)
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_UNKNOWN_CMD};
                (void)isotp_send_pdu(&s_boot_isotp, nack, sizeof(nack));
                return;
            }

            uint32_t exp_size = (uint32_t)payload[1] | ((uint32_t)payload[2] << 8) |
                                ((uint32_t)payload[3] << 16) | ((uint32_t)payload[4] << 24);
            uint32_t exp_crc  = (uint32_t)payload[5] | ((uint32_t)payload[6] << 8) |
                                ((uint32_t)payload[7] << 16) | ((uint32_t)payload[8] << 24);

            uint32_t final_crc = crc32_finalize(s_stream_crc);

            if ((s_total_bytes_written == exp_size) && (final_crc == exp_crc))
            {
                /* Now commit the deferred vector table to 0x08008000 only after full integrity is confirmed */
                if (s_deferred_vector_pending)
                {
                    hal_status_t st = openbl_flash_write(BOOT_APP_START_ADDR, s_deferred_vector_block, s_deferred_vector_len);
                    if (st != HAL_OK)
                    {
                        safe_printf(TAG, "[VERIFY] Failed to commit vector table at 0x%08lX (st=%d)!\r\n",
                                    BOOT_APP_START_ADDR, (int)st);
                        uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_FLASH};
                        (void)isotp_send_pdu(&s_boot_isotp, nack, sizeof(nack));
                        return;
                    }
                    s_deferred_vector_pending = false;
                    safe_printf(TAG, "[VERIFY] Vector table committed to 0x%08lX successfully!\r\n", BOOT_APP_START_ADDR);
                }

                safe_printf(TAG, "[VERIFY] Success! Size=%lu B, CRC32=0x%08lX, RX FIFO lost events=%lu\r\n",
                            s_total_bytes_written, final_crc, s_rx_fifo_lost_events);
                uint8_t ack[1] = {OTA_RESP_ACK};
                (void)isotp_send_pdu(&s_boot_isotp, ack, sizeof(ack));
            }
            else
            {
                safe_printf(TAG, "[VERIFY] FAILED! Written=%lu/Exp=%lu, CalcCRC=0x%08lX/ExpCRC=0x%08lX, RX FIFO lost events=%lu\r\n",
                            s_total_bytes_written, exp_size, final_crc, exp_crc, s_rx_fifo_lost_events);
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_CRC};
                (void)isotp_send_pdu(&s_boot_isotp, nack, sizeof(nack));
            }
            break;
        }

        case CMD_GO_EXECUTE:
        {
            if (!s_target_selected)
            {
                uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_NOT_SELECTED};
                (void)isotp_send_pdu(&s_boot_isotp, nack, sizeof(nack));
                return;
            }

            safe_printf(TAG, "[GO] Jump command received! Acknowledging and jumping to App...\r\n");
            uint8_t ack[1] = {OTA_RESP_ACK};
            (void)isotp_send_pdu(&s_boot_isotp, ack, sizeof(ack));

            /* Wait briefly for TX to complete */
            for (volatile uint32_t i = 0; i < 200000; i++)
            {
                __NOP();
            }

            /* Clear shared RAM magic before launching app */
            boot_shared_clear();

            boot_jump_to_application(BOOT_APP_START_ADDR);
            break;
        }

        default:
        {
            uint8_t nack[2] = {OTA_RESP_NACK, OTA_ERR_UNKNOWN_CMD};
            (void)isotp_send_pdu(&s_boot_isotp, nack, sizeof(nack));
            break;
        }
    }
}

/**
 * @brief Restricts FDCAN RX FIFO0 acceptance to the two OTA identifiers used by the bootloader.
 * @details The 3-element hardware FIFO would otherwise be shared with all foreign vehicle traffic,
 *          which can evict back-to-back ISO-TP consecutive frames on a loaded bus. A single extended
 *          dual-ID filter accepts CAN_ID_ISOTP_REQ and CAN_ID_OTA_BROADCAST; everything else is
 *          rejected in hardware. Falls back to accept-all if the filter cannot be programmed.
 * @param[in] hfdcan FDCAN peripheral handle (must be in IDLE state).
 */
static void boot_fdcan_config_ota_filter(hal_fdcan_handle_t *hfdcan)
{
    FDCAN_GlobalTypeDef *p_fdcanx = (FDCAN_GlobalTypeDef *)((uint32_t)hfdcan->instance);

    /* Generated Message RAM config reserves no filter elements: enable one extended filter */
    STM32_MODIFY_REG(p_fdcanx->RXGFC, FDCAN_RXGFC_LSS, (0U << FDCAN_RXGFC_LSS_Pos));
    STM32_MODIFY_REG(p_fdcanx->RXGFC, FDCAN_RXGFC_LSE, (1U << FDCAN_RXGFC_LSE_Pos));

    hal_fdcan_filter_t f_cfg = {0};
    f_cfg.id_type       = HAL_FDCAN_ID_EXTENDED;
    f_cfg.filter_index  = 0U;
    f_cfg.filter_type   = HAL_FDCAN_FILTER_TYPE_DUAL;
    f_cfg.filter_config = HAL_FDCAN_FILTER_TO_RX_FIFO_0;
    f_cfg.filter_id1    = CAN_ID_ISOTP_REQ;
    f_cfg.filter_id2    = CAN_ID_OTA_BROADCAST;

    bool filter_ok = (HAL_FDCAN_SetFilter(hfdcan, &f_cfg) == HAL_OK);
    if (!filter_ok)
    {
        STM32_MODIFY_REG(p_fdcanx->RXGFC, FDCAN_RXGFC_LSE, (0U << FDCAN_RXGFC_LSE_Pos));
        safe_printf(TAG, "[INIT] OTA ID filter setup failed, accepting all frames\r\n");
    }

    hal_fdcan_global_filter_config_t g_filter = {
        .acceptance_non_matching_std = filter_ok ? HAL_FDCAN_NO_MATCH_REJECT : HAL_FDCAN_NO_MATCH_TO_RX_FIFO_0,
        .acceptance_non_matching_ext = filter_ok ? HAL_FDCAN_NO_MATCH_REJECT : HAL_FDCAN_NO_MATCH_TO_RX_FIFO_0,
        .acceptance_remote_std       = HAL_FDCAN_REMOTE_REJECT,
        .acceptance_remote_ext       = HAL_FDCAN_REMOTE_REJECT
    };
    (void)HAL_FDCAN_SetGlobalFilter(hfdcan, &g_filter);
}

void openbl_core_init(bool session_selected_init, boot_mode_t boot_mode, uint8_t active_can_bus)
{
    s_target_selected = session_selected_init;
    s_boot_mode = boot_mode;

    /* Read factory 96-bit Unique Device ID into cache using 32-bit aligned words */
    uint32_t uid_w0 = *((const volatile uint32_t *)(STM32C5_UID_BASE));
    uint32_t uid_w1 = *((const volatile uint32_t *)(STM32C5_UID_BASE + 4U));
    uint32_t uid_w2 = *((const volatile uint32_t *)(STM32C5_UID_BASE + 8U));

    memcpy(&s_mcu_uid[0], &uid_w0, 4U);
    memcpy(&s_mcu_uid[4], &uid_w1, 4U);
    memcpy(&s_mcu_uid[8], &uid_w2, 4U);

    s_stream_crc = crc32_init();
    s_total_bytes_written = 0U;
    s_deferred_vector_pending = false;
    s_deferred_vector_len = 0U;

    (void)openbl_flash_init();

    /* Initialize ISO-TP link */
    isotp_init(&s_boot_isotp, CAN_ID_ISOTP_REQ, CAN_ID_ISOTP_RESP, boot_isotp_can_tx_fn, boot_isotp_pdu_cb, NULL);
    isotp_set_flow_control(&s_boot_isotp, BOOT_ISOTP_FC_BLOCK_SIZE, BOOT_ISOTP_FC_ST_MIN);

    /* Initialize and start both FDCAN1 and FDCAN2 so single-CAN and dual-CAN boards both work seamlessly */
    s_hfdcan1 = mx_fdcan1_init();
    if (s_hfdcan1 != NULL)
    {
        boot_fdcan_config_ota_filter(s_hfdcan1);
        (void)HAL_FDCAN_Start(s_hfdcan1);
    }

    s_hfdcan2 = mx_fdcan2_init();
    if (s_hfdcan2 != NULL)
    {
        boot_fdcan_config_ota_filter(s_hfdcan2);
        (void)HAL_FDCAN_Start(s_hfdcan2);
    }

    /* Select initial active bus from shared RAM handoff */
    if (active_can_bus == BOOT_CAN_BUS_FDCAN1 && s_hfdcan1 != NULL)
    {
        s_active_hfdcan = s_hfdcan1;
    }
    else if (active_can_bus == BOOT_CAN_BUS_FDCAN2 && s_hfdcan2 != NULL)
    {
        s_active_hfdcan = s_hfdcan2;
    }
    else
    {
        s_active_hfdcan = NULL; /* Unlocked: will lock to whichever bus receives the first frame */
    }
}

/**
 * @brief Polls a single FDCAN hardware instance RX FIFO 0 and locks active bus upon receiving a valid OTA frame.
 */
static void poll_fdcan_instance(hal_fdcan_handle_t *hfdcan)
{
    if (hfdcan == NULL)
    {
        return;
    }

    /* Count RX FIFO0 overflow events (RF0L is write-1-to-clear) for transfer diagnostics */
    FDCAN_GlobalTypeDef *p_fdcanx = (FDCAN_GlobalTypeDef *)((uint32_t)hfdcan->instance);
    if ((p_fdcanx->IR & FDCAN_IR_RF0L) != 0U)
    {
        p_fdcanx->IR = FDCAN_IR_RF0L;
        s_rx_fifo_lost_events++;
    }

    uint32_t fill_level = 0U;
    while (1)
    {
        HAL_FDCAN_GetRxFifoFillLevel(hfdcan, HAL_FDCAN_RX_FIFO_0, &fill_level);
        if (fill_level == 0U)
        {
            break;
        }

        hal_fdcan_rx_header_t rx_header = {0};
        uint8_t rx_data[64] = {0};

        if (HAL_FDCAN_GetReceivedMessage(hfdcan, HAL_FDCAN_RX_FIFO_0, &rx_header, rx_data) == HAL_OK)
        {
            /* If an active CAN bus is locked, drain and ignore duplicate frames from the other bus */
            if (s_active_hfdcan != NULL && hfdcan != s_active_hfdcan)
            {
                continue;
            }

            /* Only accept 29-bit Extended CAN frames in the V-Box OTA namespace */
            if (rx_header.b.identifier_type == HAL_FDCAN_ID_EXTENDED)
            {
                uint32_t can_id = rx_header.b.identifier;
                uint8_t len = fdcan_dlc_to_bytes(rx_header.b.data_length);

                if (can_id == CAN_ID_OTA_BROADCAST)
                {
                    if ((len >= OTA_DISCOVERY_MIN_LEN) &&
                        (rx_data[1] == CMD_DISCOVER_NODES) &&
                        (rx_data[2] == OTA_MAGIC_TOKEN_0)  &&
                        (rx_data[3] == OTA_MAGIC_TOKEN_1)  &&
                        (rx_data[4] == OTA_MAGIC_TOKEN_2)  &&
                        (rx_data[5] == OTA_MAGIC_TOKEN_3))
                    {
                        s_active_hfdcan = hfdcan;
                        handle_broadcast_discovery(rx_data, len);
                    }
                }
                else if (can_id == CAN_ID_ISOTP_REQ)
                {
                    s_active_hfdcan = hfdcan;
                    isotp_on_can_frame(&s_boot_isotp, can_id, rx_data, len);
                }
            }
        }
    }
}

void openbl_core_poll(void)
{
    poll_fdcan_instance(s_hfdcan1);
    poll_fdcan_instance(s_hfdcan2);
}

bool openbl_core_is_session_active(void)
{
    return s_target_selected;
}

void openbl_core_send_recovery_beacon(uint8_t reason, uint8_t fail_cnt, uint32_t pc)
{
    uint8_t uid_hash = 0U;
    for (size_t i = 0; i < STM32C5_UID_LEN_BYTES; i++)
    {
        uid_hash ^= s_mcu_uid[i];
    }

    uint8_t beacon[8];
    beacon[0] = 0xEEU; /* Crash beacon magic */
    beacon[1] = reason;
    beacon[2] = fail_cnt;
    beacon[3] = uid_hash;
    beacon[4] = (uint8_t)(pc & 0xFFU);
    beacon[5] = (uint8_t)((pc >> 8) & 0xFFU);
    beacon[6] = (uint8_t)((pc >> 16) & 0xFFU);
    beacon[7] = (uint8_t)((pc >> 24) & 0xFFU);

    uint32_t resp_id = CAN_ID_OTA_DISCOVERY_RESP_NODE(uid_hash);
    (void)boot_can_send_frame(resp_id, beacon, sizeof(beacon));
}
