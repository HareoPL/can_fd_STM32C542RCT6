/**
 * @file boot_version_record.h
 * @brief Bootloader version record stored at a fixed offset inside the Bootloader image.
 * @details The Bootloader linker script places one boot_version_record_t at
 *          BOOTLOADER_FLASH_BASE + BOOT_VERSION_RECORD_OFFSET (directly after the vector table).
 *          The Main Application reads it from Flash to report the installed Bootloader version,
 *          and validates it inside a staged image before installing a new Bootloader.
 *          Pure C header shared by both targets (no HAL / RTOS dependencies).
 */

#ifndef BOOT_VERSION_RECORD_H
#define BOOT_VERSION_RECORD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Offset of the version record from the start of a Bootloader image (must match the linker script). */
#define BOOT_VERSION_RECORD_OFFSET      0x200U

/** @brief Record magic value (ASCII "VBVR" in little-endian memory order). */
#define BOOT_VERSION_RECORD_MAGIC       0x52564256UL

/** @brief Current layout revision of boot_version_record_t. */
#define BOOT_VERSION_RECORD_FORMAT      1U

/** @brief Size of the NUL-terminated short git commit hash field. */
#define BOOT_VERSION_RECORD_COMMIT_LEN  12U

/**
 * @brief Bootloader version record layout (32 bytes).
 */
typedef struct
{
    uint32_t magic;                                 /**< BOOT_VERSION_RECORD_MAGIC */
    uint8_t  format;                                /**< Layout revision (BOOT_VERSION_RECORD_FORMAT) */
    uint8_t  major;                                 /**< Bootloader version major (boot-vX.Y.Z tag) */
    uint8_t  minor;                                 /**< Bootloader version minor */
    uint8_t  patch;                                 /**< Bootloader version patch */
    char     git_commit[BOOT_VERSION_RECORD_COMMIT_LEN]; /**< NUL-terminated short git commit hash */
    uint32_t magic_inv;                             /**< Bitwise inverse of magic (integrity check) */
    uint32_t reserved[2];                           /**< Reserved for future fields (0) */
} boot_version_record_t;

/**
 * @brief Returns the version record of a Bootloader image if present and valid.
 * @param[in] image_base Start address of the Bootloader image (e.g. 0x08000000 or the staging slot).
 * @return Pointer to the record inside Flash, or NULL if the image carries no valid record.
 */
const boot_version_record_t *boot_version_record_get(uint32_t image_base);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_VERSION_RECORD_H */
