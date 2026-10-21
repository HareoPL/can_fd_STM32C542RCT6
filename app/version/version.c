/**
 * @file version.c
 * @brief Implementation of system version reporting and boot banner.
 */
#include "version.h"
#include "version_config.h"
#include "safe_printf.h"
#include "boot_version_record.h"
#include "ota_protocol.h"
#include <stdio.h>

static const app_version_info_t s_version_info = {
    .fw_major        = FW_VERSION_MAJOR,
    .fw_minor        = FW_VERSION_MINOR,
    .fw_patch        = FW_VERSION_PATCH,
    .fw_version_str  = FW_VERSION_STRING,
    .git_commit      = FW_GIT_COMMIT,
    .git_describe    = FW_GIT_DESCRIBE,
    .build_timestamp = FW_BUILD_TIMESTAMP,
    .build_type      = FW_BUILD_TYPE,
    .hw_target_name  = HW_TARGET_NAME,
    .hw_target_mcu   = HW_TARGET_MCU,
    .hw_major        = HW_VERSION_MAJOR,
    .hw_minor        = HW_VERSION_MINOR
};

const app_version_info_t *version_getInfo(void)
{
    return &s_version_info;
}

void version_printBootBanner(void)
{
    safe_printf(NULL, "\r\n");
    safe_printf(NULL, "======================================================================\r\n");
    safe_printf(NULL, "  V-BOX TESTER - Automotive Telematics Test Bench & Signal Converter  \r\n");
    safe_printf(NULL, "======================================================================\r\n");
    safe_printf(NULL, "  Firmware   : %s (%s) [%s]\r\n", 
                s_version_info.fw_version_str, 
                s_version_info.git_describe,
                s_version_info.build_type);
    safe_printf(NULL, "  Commit     : %s\r\n", s_version_info.git_commit);
    safe_printf(NULL, "  Target MCU : %s (ARM Cortex-M33 @ 144 MHz)\r\n", 
                s_version_info.hw_target_mcu);
    safe_printf(NULL, "  Hardware   : %s (Rev %u.%u)\r\n", 
                s_version_info.hw_target_name,
                (unsigned int)s_version_info.hw_major,
                (unsigned int)s_version_info.hw_minor);
    safe_printf(NULL, "  Build Time : %s\r\n", s_version_info.build_timestamp);

    const boot_version_record_t *boot_rec = boot_version_record_get(BOOTLOADER_FLASH_BASE);
    if (boot_rec != NULL)
    {
        safe_printf(NULL, "  Bootloader : v%u.%u.%u (%s)\r\n",
                    (unsigned int)boot_rec->major,
                    (unsigned int)boot_rec->minor,
                    (unsigned int)boot_rec->patch,
                    boot_rec->git_commit);
    }
    else
    {
        safe_printf(NULL, "  Bootloader : unknown (no version record at 0x%08lX)\r\n",
                    (unsigned long)(BOOTLOADER_FLASH_BASE + BOOT_VERSION_RECORD_OFFSET));
    }
    safe_printf(NULL, "======================================================================\r\n");
    safe_printf(NULL, "\r\n");
}

