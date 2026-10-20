/**
 * @file ota_protocol.h
 * @brief Common command identifiers, CAN arbitration IDs, and protocol constants for OTA and Bootloader.
 */

#ifndef OTA_PROTOCOL_H
#define OTA_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===========================================================================
 * CAN Arbitration Identifiers (29-bit Extended IDs in V-Box Namespace)
 * Eliminates collisions with standard 11-bit OBD-II / UDS vehicle traffic.
 * =========================================================================== */
#define CAN_ID_OTA_BROADCAST            0x01F2F3DFUL  /**< PC -> All Nodes: Discovery & Broadcast */
#define CAN_ID_OTA_DISCOVERY_RESP       0x01F2F3E9UL  /**< Node -> PC: Discovery Response & On-Demand SOS */
#define CAN_ID_ISOTP_REQ                0x01F2F3E0UL  /**< PC -> Target Node: ISO-TP Requests (SF/FF/CF) */
#define CAN_ID_ISOTP_RESP               0x01F2F3E8UL  /**< Target Node -> PC: ISO-TP Responses & Flow Control */

/* ===========================================================================
 * Broadcast Discovery Authorization Token ("VBOX")
 * Frame format on 0x01F2F3DF: [0x06, CMD_DISCOVER_NODES, 'V', 'B', 'O', 'X', 0, 0]
 * =========================================================================== */
#define OTA_MAGIC_TOKEN_0               0x56U   /**< ASCII 'V' */
#define OTA_MAGIC_TOKEN_1               0x42U   /**< ASCII 'B' */
#define OTA_MAGIC_TOKEN_2               0x4FU   /**< ASCII 'O' */
#define OTA_MAGIC_TOKEN_3               0x58U   /**< ASCII 'X' */
#define OTA_DISCOVERY_MIN_LEN           6U      /**< Minimum frame length for valid discovery */

/* ===========================================================================
 * Protocol Command Identifiers
 * =========================================================================== */
#define CMD_DISCOVER_NODES              0xA0U   /**< Discover nodes command (Broadcast on 0x7DF) */
#define CMD_SELECT_TARGET               0xA1U   /**< Select target by 96-bit UID (ISO-TP on 0x7E0) */
#define CMD_ENTER_BOOTLOADER            0xA2U   /**< Request MCU to enter/stay in bootloader mode */
#define CMD_VERIFY_FIRMWARE             0xA3U   /**< Verify entire firmware CRC32 */
#define CMD_UPDATE_BOOTLOADER           0xA4U   /**< Install and commit Bootloader from Staging Slot */
#define CMD_GET_CMD                     0x00U   /**< OpenBootloader: Get allowed commands */
#define CMD_GET_VERSION                 0x01U   /**< OpenBootloader: Get bootloader version */
#define CMD_GET_ID                      0x02U   /**< OpenBootloader: Get chip ID */
#define CMD_GO_EXECUTE                  0x21U   /**< OpenBootloader: Jump to address / start app */
#define CMD_WRITE_MEMORY                0x31U   /**< OpenBootloader: Stream write memory block */
#define CMD_ERASE_APP                   0x44U   /**< OpenBootloader: Erase application flash memory */

/* ===========================================================================
 * Bootloader & Staging Memory Layout Constants
 * =========================================================================== */
#define BOOTLOADER_FLASH_BASE           0x08000000UL    /**< Bootloader base address (Flash Bank 1) */
#define BOOTLOADER_MAX_SIZE_BYTES       0x8000UL        /**< Bootloader maximum size: 32 KB */
#define BOOTLOADER_PAGE_SIZE            2048U           /**< STM32C5 Flash page size: 2 KB */
#define BOOTLOADER_STAGING_BASE         0x08038000UL    /**< Bootloader Staging Slot base in Flash (top 32 KB) */
#define BOOTLOADER_STAGING_SIZE_BYTES   0x8000UL        /**< Bootloader Staging Slot size: 32 KB (16 pages) */

/* ===========================================================================
 * Protocol Response Codes
 * =========================================================================== */
#define OTA_RESP_ACK                    0x79U   /**< Positive acknowledgment */
#define OTA_RESP_NACK                   0x1FU   /**< Negative acknowledgment */

/* ===========================================================================
 * NACK Error Reason Codes
 * =========================================================================== */
#define OTA_ERR_CRC                     0x01U   /**< CRC32 mismatch */
#define OTA_ERR_ADDR                    0x02U   /**< Address out of application flash range */
#define OTA_ERR_FLASH                   0x03U   /**< Hardware flash write/erase failure */
#define OTA_ERR_SEQUENCE                0x04U   /**< ISO-TP consecutive frame sequence error */
#define OTA_ERR_NOT_SELECTED            0x05U   /**< Device not selected by UID */
#define OTA_ERR_UNKNOWN_CMD             0x06U   /**< Unsupported command */

/* ===========================================================================
 * Device Operating Modes & Hardware UID Constants
 * =========================================================================== */
#define OTA_MODE_BOOTLOADER             0x00U   /**< Node running in Bootloader mode */
#define OTA_MODE_APPLICATION            0x01U   /**< Node running in Main Application mode */
#define OTA_MODE_RECOVERY               0x02U   /**< Node in Recovery mode after HardFault/Crash */

#define STM32C5_UID_BASE                0x08FFF800UL /**< 96-bit Unique Device ID Base Address */
#define STM32C5_UID_LEN_BYTES           12U          /**< 12 bytes = 96 bits */

#ifdef __cplusplus
}
#endif

#endif /* OTA_PROTOCOL_H */

