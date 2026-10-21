/**
 * @file boot_version_record.c
 * @brief Lookup and validation of the Bootloader version record.
 */

#include "boot_version_record.h"
#include <stddef.h>

_Static_assert(sizeof(boot_version_record_t) == 32U, "boot_version_record_t layout must stay 32 bytes");

const boot_version_record_t *boot_version_record_get(uint32_t image_base)
{
    const boot_version_record_t *rec =
        (const boot_version_record_t *)(image_base + BOOT_VERSION_RECORD_OFFSET);

    if ((rec->magic != BOOT_VERSION_RECORD_MAGIC) ||
        (rec->magic_inv != ~BOOT_VERSION_RECORD_MAGIC) ||
        (rec->format != BOOT_VERSION_RECORD_FORMAT) ||
        (rec->git_commit[BOOT_VERSION_RECORD_COMMIT_LEN - 1U] != '\0'))
    {
        return NULL;
    }

    return rec;
}
