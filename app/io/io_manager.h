
/**
 * @file io_manager.h
 * @brief 
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-12
 * @LastEditors Jan Lukaszewicz
 * @LastEditTime 2026-10-14
 */

#ifndef IO_MANAGER_H
#define IO_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Gets the debounced state of all 9 inputs (SW0..SW8).
 * @return 16-bit packed input state.
 */
uint16_t io_manager_getAllBtnState(void);

/**
 * @brief Gets the physical inputs regardless of the input source (io_sim.h).
 * @param[out] btn_state Debounced SW0..SW8.
 * @param[out] fuel Fuel level 0..100 % (potentiometer, refreshed every CAN period).
 */
void io_manager_getPhysInputs(uint16_t *btn_state, uint8_t *fuel);

/**
 * @brief Gets the current physical relay state (debounced PB12 / SW4).
 * @return 1 if relay contact is closed (ON), 0 if open (OFF).
 */
uint8_t io_manager_getRelayState(void);

/**
 * @brief Controls the edge-triggered bistable relay and verifies feedback (synchronous, blocking).
 * 
 * Generates a rising edge (0 -> 1) for ON or a falling edge (1 -> 0) for OFF,
 * and polls the debounced feedback pin until the relay settles. Retries if necessary.
 * 
 * @param[in] target_state Desired state: 1 for ON, 0 for OFF.
 * @return true if relay is in the target state, false on failure/timeout or while the simulated
 *         lock fault is active (io_sim_isLockFault(), the relay is not driven).
 */
bool io_manager_setRelayState(uint8_t target_state);

/**
 * @brief Posts an asynchronous relay state request to the background relay worker queue.
 * 
 * This function returns immediately (non-blocking, execution time < 1 us),
 * making it safe to invoke from high-priority CAN RX task callbacks without dropping frames.
 * 
 * @param[in] target_state Desired state: 1 for ON, 0 for OFF.
 * @return true if command was enqueued successfully or already matched, false on queue error.
 */
bool io_manager_setRelayStateAsync(uint8_t target_state);

/**
 * @brief Initializes I/O hardware, reads initial pin states, and launches FreeRTOS tasks.
 * @return true if initialization succeeded, false otherwise.
 */
bool io_manager_init(void);

#endif /* IO_MANAGER_H */
