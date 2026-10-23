/**
 * @file can_msg_types.h
 * @brief Definitions and utility functions for CAN message handling.
 * 
 * This file provides the structure for CAN messages and inline functions 
 * to manage Standard (11-bit) and Extended (29-bit) identifiers.
 */

 #ifndef CAN_MSG_TYPES_H
 #define CAN_MSG_TYPES_H
 
 #include <stdint.h>
 #include <stdbool.h>
 
 /**
  * @brief Structure representing a CAN or FDCAN message.
  */
 typedef struct {
     uint32_t ident;     /**< CAN Identifier. Bit 31: 1 = Extended, 0 = Standard. */
     uint8_t format;     /**< Frame format: CAN_MSG_FRAME_FORMAT_CAN or CAN_MSG_FRAME_FORMAT_FD_CAN. */
     uint8_t len;        /**< Length of the data payload in bytes (0-8 Classic, 0-64 CAN FD). */
     uint8_t flags;      /**< Per-frame options CAN_MSG_FLAG_* (0 = behaviour defined by the bus mode). */
     uint8_t data[64];   /**< Payload buffer sized for the largest CAN FD frame: 4+1+1+1+64 = 71 -> 72 bytes */
 } CAN_msg_t;

 _Static_assert(sizeof(CAN_msg_t) == 72U, "CAN_msg_t size changed (queues and OTA rely on 72 bytes)");

 #define CAN_MSG_FRAME_FORMAT_CAN    0U /*!< Standard frame format                        */
 #define CAN_MSG_FRAME_FORMAT_FD_CAN 1U /*!< CAN FD frame format (new DLC-coding and CRC) */

 /* --- Per-frame flags (CAN_msg_t.flags) --- */

 /** @brief CAN FD: bit rate switch in the data phase (TX: honoured only with CAN_MSG_FLAG_BRS_SET). */
 #define CAN_MSG_FLAG_BRS     0x01U
 /** @brief Classic CAN: remote frame (RTR), len = requested DLC, payload ignored. */
 #define CAN_MSG_FLAG_RTR     0x02U
 /** @brief TX: BRS chosen explicitly for this frame (value in CAN_MSG_FLAG_BRS). Without it the bus mode decides. */
 #define CAN_MSG_FLAG_BRS_SET 0x04U

 
 /* --- CAN ID Flags and Masks --- */
 
 /** 
  * @brief Flag indicating an Extended Frame Format. 
  * Set in the most significant bit (bit 31) of the identifier.
  */
 #define CAN_EFF_FLAG 0x80000000U 
 
 /** @brief Mask for 29-bit Extended Identifiers. */
 #define CAN_EFF_MASK 0x1FFFFFFFU 
 
 /** @brief Mask for 11-bit Standard Identifiers. */
 #define CAN_SFF_MASK 0x000007FFU 
 
 /**
  * @brief Checks if the provided CAN ID has the Extended flag set.
  * 
  * @param id The raw CAN identifier including flags.
  * @return true If the ID is marked as Extended.
  * @return false If the ID is marked as Standard.
  */
 static inline bool CAN_msg_is_extended_id(uint32_t id) {
     return (id & CAN_EFF_FLAG) != 0U;
 }
 
 /**
  * @brief Extracts the clean CAN identifier by removing flags.
  * This function masks the ID to ensure only the relevant 11 or 29 bits are returned.
  * 
  * @param id The raw CAN identifier including flags.
  * @return uint32_t The numerical CAN ID (11-bit or 29-bit).
  */
 static inline uint32_t CAN_msg_get_can_id(uint32_t id) {
     if (CAN_msg_is_extended_id(id)) {
         return id & CAN_EFF_MASK;
     }
     return id & CAN_SFF_MASK;
 }
 
 /**
  * @brief Creates an Extended CAN identifier with the required flag.
  * Applies the Extended mask to the input ID and sets the MSB flag.
  * 
  * @param id The numerical 29-bit identifier.
  * @return uint32_t The formatted Extended ID with the EFF flag set.
  */
 static inline uint32_t CAN_msg_set_extended_id(uint32_t id) {
     return (id & CAN_EFF_MASK) | CAN_EFF_FLAG;
 }
 
 /**
  * @brief Creates a Standard CAN identifier.
  * Ensures the ID is masked to 11 bits and the EFF flag is cleared.
  * 
  * @param id The numerical 11-bit identifier.
  * @return uint32_t The formatted Standard ID.
  */
 static inline uint32_t CAN_msg_set_standard_id(uint32_t id) {
     return (id & CAN_SFF_MASK);
 }
 
 #endif // CAN_MSG_TYPES_H