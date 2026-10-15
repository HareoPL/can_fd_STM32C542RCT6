/**
 * @file version.h
 * @brief Firmware and Hardware version definitions and system boot banner.
 * @details Provides access to semantic versioning (SemVer 2.0), Git commit hash,
 *          build metadata, and target hardware revision.
 */
#ifndef VERSION_H
#define VERSION_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Application version and build metadata descriptor.
 */
typedef struct {
    uint8_t     fw_major;           /**< Firmware Semantic Version Major */
    uint8_t     fw_minor;           /**< Firmware Semantic Version Minor */
    uint8_t     fw_patch;           /**< Firmware Semantic Version Patch */
    const char *fw_version_str;     /**< Full semantic version string (e.g. "v1.0.0") */
    const char *git_commit;         /**< Git short commit hash (e.g. "d258caa") */
    const char *git_describe;       /**< Git describe tag/commit string */
    const char *build_timestamp;    /**< Build timestamp string (UTC) */
    const char *build_type;         /**< Build type (e.g. "Debug", "Release") */
    const char *hw_target_name;     /**< Target hardware board name */
    const char *hw_target_mcu;      /**< Target MCU part number */
    uint8_t     hw_major;           /**< Hardware Revision Major */
    uint8_t     hw_minor;           /**< Hardware Revision Minor */
} app_version_info_t;

/**
 * @brief Retrieves the immutable application version and metadata descriptor.
 * @return Pointer to constant app_version_info_t structure.
 */
const app_version_info_t *version_getInfo(void);

/**
 * @brief Prints the system boot splash banner to the diagnostic UART console.
 * @details Displays firmware version, commit hash, build date, target hardware, and MCU.
 *          Safe to invoke both before and after the FreeRTOS scheduler starts.
 */
void version_printBootBanner(void);

#ifdef __cplusplus
}
#endif

#endif /* VERSION_H */

