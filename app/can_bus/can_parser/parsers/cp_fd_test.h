/**
 * @file cp_fd_test.h
 * @brief CAN parser profile: CAN FD bus test (runtime switchable bus configuration).
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-22
 */

#ifndef CP_FD_TEST_H
#define CP_FD_TEST_H

#include "can_parser.h"

/**
 * @brief Returns the CAN FD test parser profile descriptor.
 * @return Pointer to static profile structure.
 */
const can_parser_profile_t *cp_fd_test_profile_get(void);

/**
 * @brief Sets the bus configuration applied on the next can_parser_select(CAN_PARSER_ID_FD_TEST).
 * @param[in] config New bus configuration (copied).
 */
void cp_fd_test_bus_config_set(const can_config_t *config);

#endif /* CP_FD_TEST_H */
