/**
 * @file can_parser.c
 * @brief Implementation of CAN parser profile dispatch and dynamic selection.
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-13
 */

#include "can_parser.h"
#include "cp_v_box.h"
#include "cp_example.h"
#include "safe_printf.h"
#include <stddef.h>

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
    [CAN_PARSER_ID_EXAMPLE] = cp_example_profile_get
};

/** @brief Active parser profile ID. */
static can_parser_id_t s_active_parser = CAN_PARSER_COUNT;

/* --- Public API ------------------------------------------------------------*/

/**
 * @brief Dynamically selects and activates a CAN parser profile by its numerical ID.
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
 * @brief Retrieves the currently active parser profile identifier.
 * @return Current can_parser_id_t, or CAN_PARSER_COUNT if none active.
 */
can_parser_id_t can_parser_get_current_id(void)
{
    return s_active_parser;
}
