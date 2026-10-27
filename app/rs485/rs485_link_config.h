/**
 * @file rs485_link_config.h
 * @brief Compile-time configuration of the RS485 link to the external telematics device.
 *
 * Every value can be overridden from CMake (target_compile_definitions) without editing this file.
 *
 * @author Jan Lukaszewicz <brak@email.com>
 * @date 2026-10-26
 */

#ifndef RS485_LINK_CONFIG_H
#define RS485_LINK_CONFIG_H

/* --- Identity used in the requests (values of the PC service tool, sniffed on 2026-10-26) --- */
#ifndef RS485_LINK_SRC_ADDR
#define RS485_LINK_SRC_ADDR            0x90U    /**< Source address of the requests (PC service tool) */
#endif
#ifndef RS485_LINK_TOOL_VERSION
#define RS485_LINK_TOOL_VERSION        0x0F45U  /**< Tool version sent in the requests (3909) */
#endif
#ifndef RS485_LINK_PASSWORD
#define RS485_LINK_PASSWORD            "9999"   /**< Device password, at most 9 characters */
#endif

/* --- superStatus polling --- */
#ifndef RS485_LINK_POLL_PERIOD_MS
#define RS485_LINK_POLL_PERIOD_MS      5000U    /**< superStatus request period */
#endif
#ifndef RS485_LINK_RESP_TIMEOUT_MS
#define RS485_LINK_RESP_TIMEOUT_MS     300U     /**< Response timeout (observed 80..140 ms) */
#endif
#ifndef RS485_LINK_RETRIES
#define RS485_LINK_RETRIES             3U       /**< Repeated requests after a timeout */
#endif
#ifndef RS485_LINK_LOST_POLLS
#define RS485_LINK_LOST_POLLS          3U       /**< Unanswered polls before the link is reported lost */
#endif

/* --- Bus access --- */
#ifndef RS485_LINK_BUS_IDLE_MS
#define RS485_LINK_BUS_IDLE_MS         20U      /**< Silence required before transmitting */
#endif
#ifndef RS485_LINK_FRAME_GAP_MS
#define RS485_LINK_FRAME_GAP_MS        50U      /**< Silence that ends an incomplete frame candidate */
#endif
#ifndef RS485_LINK_ECHO_WINDOW_MS
#define RS485_LINK_ECHO_WINDOW_MS      50U      /**< Own request received back within this time = echo */
#endif
#ifndef RS485_LINK_PASSIVE_HOLD_MS
#define RS485_LINK_PASSIVE_HOLD_MS     15000U   /**< No requests after a frame of another master (service tool) */
#endif
#ifndef RS485_LINK_TRANSFER_HOLD_MS
#define RS485_LINK_TRANSFER_HOLD_MS    10000U   /**< No requests after a file transfer frame (parser update) */
#endif
#ifndef RS485_LINK_AFTER_TRANSFER_MS
#define RS485_LINK_AFTER_TRANSFER_MS   3000U    /**< superStatus this long after the transfer end (device restarts CAN) */
#endif

#endif /* RS485_LINK_CONFIG_H */
