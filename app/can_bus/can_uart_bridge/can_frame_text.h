/**
 * @file can_frame_text.h
 * @brief Text frame parser and formatter compatible with Serial Commander (can_support.py).
 *
 * Syntax (cansend / CAN FD / TLV), identical to can_support.parse_can_frame():
 *   187#01 02 03         Classic (auto FD+BRS above 8 bytes)
 *   321##1#01 02 03      CAN FD, first character after '##' = BRS flag (0/1)
 *   65A#R4               Classic remote frame with DLC
 *   TLV#321#0xA5#Hello   ot_app_msg_tlv (<BH) padded to 64 bytes, CAN FD
 * Optional bus prefix: CAN1: / CAN2: (case insensitive).
 *
 * Pure C: no HAL, no FreeRTOS, no dynamic memory. Error texts are UTF-8 copies of CanFrameError.
 */

#ifndef CAN_FRAME_TEXT_H
#define CAN_FRAME_TEXT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "can_msg_types.h"

/** @brief Maximum TLV value length that fits a 64 byte CAN FD frame (64 - 3 byte header). */
#define CFT_TLV_MAX_VALUE   61U

/** @brief TLV type used by the forced 'tlv' frame mode. */
#define CFT_TLV_DEFAULT_TYPE 0xA5U

/** @brief Buffer size sufficient for can_frame_text_format() of any frame (64 B + TLV suffix). */
#define CFT_FORMAT_BUF_SIZE 512U

/**
 * @brief Forced frame standard (can_mode of Serial Commander).
 */
typedef enum {
    CFT_MODE_AUTO = 0,   /**< From syntax / length ("auto") */
    CFT_MODE_CLASSIC,    /**< Classic CAN 2.0 only ("classic") */
    CFT_MODE_FD_BRS,     /**< CAN FD with BRS ("fd_brs") */
    CFT_MODE_FD_NO_BRS,  /**< CAN FD without BRS ("fd_no_brs") */
    CFT_MODE_TLV         /**< Data packed as TLV type 0xA5 ("tlv") */
} cft_mode_t;

/**
 * @brief Parsed text frame.
 */
typedef struct {
    uint8_t bus;          /**< 0 = default bus, 1 = CAN1, 2 = CAN2 (from prefix) */
    CAN_msg_t msg;        /**< ident (with CAN_EFF_FLAG), format, len, flags, data */
} cft_frame_t;

/**
 * @brief Parses one text line into a CAN frame.
 *        FD frames always carry CAN_MSG_FLAG_BRS_SET, so BRS = 0 is honoured on an FD+BRS bus.
 * @param[in] line NUL terminated text (whitespace around is ignored).
 * @param[in] mode Forced frame standard.
 * @param[out] out Parsed frame (zeroed first).
 * @param[out] err Error text on failure (UTF-8, may be NULL).
 * @param[in] err_size Size of err.
 * @return true on success, false on syntax error (err filled).
 */
bool can_frame_text_parse(const char *line, cft_mode_t mode, cft_frame_t *out,
                          char *err, size_t err_size);

/**
 * @brief Formats a frame like CanFrame.to_display(show_tlv=True), e.g. "321 [3] [FD+BRS] 01 02 03".
 *        TLV value characters outside ASCII 32-126 are shown as '?' (Python: U+FFFD / raw control).
 * @param[in] msg Frame to format.
 * @param[out] buf Output buffer (CFT_FORMAT_BUF_SIZE is always enough).
 * @param[in] size Size of buf.
 * @return Length of the text (without NUL), truncated to size - 1.
 */
size_t can_frame_text_format(const CAN_msg_t *msg, char *buf, size_t size);

/**
 * @brief Parses a frame mode name (auto, classic, fd_brs, fd_no_brs, tlv; case insensitive).
 * @return true if the name is known.
 */
bool can_frame_text_mode_parse(const char *name, cft_mode_t *mode);

/**
 * @brief Returns the Serial Commander name of a frame mode ("auto", "classic", ...).
 */
const char *can_frame_text_mode_name(cft_mode_t mode);

#endif /* CAN_FRAME_TEXT_H */
