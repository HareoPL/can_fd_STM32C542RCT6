/**
 * @file can_parser.c
 * @brief Implementation of CAN parser profile dispatch and dynamic selection.
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-13
 */

#include "can_parser.h"
#include "cp_v_box.h"
#include "cp_example.h"
#include "cp_boot_ota.h"
#if defined(CAN_FD_TEST) && (CAN_FD_TEST == 1)
#include "cp_fd_test.h"
#endif
#include "safe_printf.h"
#include <stddef.h>
#include <string.h>

/** @brief Logging tag for the CAN Parser dispatcher module. */
#define TAG "CAN_PARSER"

/**
 * @brief Function pointer type for retrieving a parser profile descriptor.
 */
typedef const can_parser_profile_t *(*can_profile_getter_fn)(void);

/**
 * @brief Registry of available CAN parser profile getters (4 bytes Flash per profile).
 */
static const can_profile_getter_fn s_parsers[CAN_PARSER_COUNT] = {
    [CAN_PARSER_ID_V_BOX]   = cp_v_box_profile_get,
    [CAN_PARSER_ID_EXAMPLE] = cp_example_profile_get,
    [CAN_PARSER_ID_BOOT_OTA] = cp_boot_ota_profile_get,
#if defined(CAN_FD_TEST) && (CAN_FD_TEST == 1)
    [CAN_PARSER_ID_FD_TEST] = cp_fd_test_profile_get
#endif
};

/** @brief Active parser profile ID. */
static can_parser_id_t s_active_parser = CAN_PARSER_COUNT;

/* --- Public API ------------------------------------------------------------*/

/**
 * @brief Dynamically selects and activates a CAN parser profile by its numerical ID.
 *        Applies the profile bus configuration first, then its acceptance filters.
 * @param[in,out] can_inst Pointer to target CAN instance (e.g. &can1_inst).
 * @param[in] id Parser profile identifier (CAN_PARSER_ID_V_BOX, CAN_PARSER_ID_EXAMPLE).
 * @return true on success, false on error.
 */
bool can_parser_select(can_instance_t *can_inst, can_parser_id_t id)
{
    if (can_inst == NULL || id >= CAN_PARSER_COUNT || s_parsers[id] == NULL)
    {
        return false;
    }

    const can_parser_profile_t *profile = s_parsers[id]();
    if (profile == NULL)
    {
        return false;
    }

    if (profile->bus_config != NULL && !can_manager_setBusConfig(can_inst, profile->bus_config))
    {
        return false;
    }

    if (!can_manager_setFilter(can_inst, profile->filters, profile->num_filters))
    {
        return false;
    }

    s_active_parser = id;
    safe_printf(TAG, "Active parser: ID %u (%u filters)\r\n",
                (unsigned int)id, (unsigned int)profile->num_filters);

    return true;
}

/**
 * @brief Attaches the system BOOT_OTA service parser filters alongside the currently active
 *        parser profile on the specified CAN instance (exclusive exception for OTA).
 * @param[in,out] can_inst Pointer to target CAN instance (e.g. &can1_inst).
 * @return true on success, false if filter capacity is exceeded or hardware configuration fails.
 */
bool can_parser_attach_boot_ota(can_instance_t *can_inst)
{
    if (can_inst == NULL || s_parsers[CAN_PARSER_ID_BOOT_OTA] == NULL)
    {
        return false;
    }

    const can_parser_profile_t *ota_profile = s_parsers[CAN_PARSER_ID_BOOT_OTA]();
    if (ota_profile == NULL || ota_profile->filters == NULL || ota_profile->num_filters == 0U)
    {
        return false;
    }

    if ((can_inst->num_filters + ota_profile->num_filters) > CAN_MAX_FILTERS)
    {
        return false;
    }

    can_filter_t combined[CAN_MAX_FILTERS];
    uint32_t total_filters = can_inst->num_filters;

    if (total_filters > 0U)
    {
        memcpy(combined, can_inst->filters, total_filters * sizeof(can_filter_t));
    }

    memcpy(&combined[total_filters], ota_profile->filters, ota_profile->num_filters * sizeof(can_filter_t));
    total_filters += ota_profile->num_filters;

    if (!can_manager_setFilter(can_inst, combined, total_filters))
    {
        return false;
    }

    safe_printf(TAG, "Attached BOOT_OTA parser to %s (total filters: %lu)\r\n",
                can_inst->name, (unsigned long)total_filters);
    return true;
}

/**
 * @brief Retrieves the currently active parser profile identifier.
 * @return Current can_parser_id_t, or CAN_PARSER_COUNT if none active.
 */
can_parser_id_t can_parser_get_current_id(void)
{
    return s_active_parser;
}
