/**
 * @file cp_example.h
 * @brief CAN parser profile: Example diagnostic and calibration handler.
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-13
 */

#ifndef CP_EXAMPLE_H
#define CP_EXAMPLE_H

#include "can_parser.h"

/**
 * @brief Returns the example parser profile descriptor.
 * @return Pointer to static immutable profile structure.
 */
const can_parser_profile_t *cp_example_profile_get(void);

#endif /* CP_EXAMPLE_H */

