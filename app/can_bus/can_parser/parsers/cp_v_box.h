/**
 * @file cp_v_box.h
 * @brief CAN parser profile: V-Box telemetry and relay handler.
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-13
 */

#ifndef CP_V_BOX_H
#define CP_V_BOX_H

#include "can_parser.h"

/**
 * @brief Returns the v-box parser profile descriptor.
 * @return Pointer to static immutable profile structure.
 */
const can_parser_profile_t *cp_v_box_profile_get(void);

#endif /* CP_V_BOX_H */

