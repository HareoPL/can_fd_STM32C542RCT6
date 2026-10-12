/**
 * @file stm32c5_can_adapter.c
 * @brief STM32C5 hardware adapter implementation for CAN and CAN FD bus.
 */

#include "stm32c5_can_adapter.h"
#include "can_port.h"
#include "can_msg_types.h"
#include "stm32_hal.h"
#include "stm32c5xx_hal_fdcan.h"
#include "stm32c5xx_hal_cortex.h"
#include "mx_fdcan1.h"
#include <stddef.h>

/**
 * @brief Variables storing RX notification callback and context for CAN1.
 */
static can_rx_notify_fn s_can1_rx_cb = NULL;
static void* s_can1_rx_context = NULL;

/**
 * @brief Active configuration cached for CAN1 instance.
 */
static can_config_t s_can1_config = {
    .frame_format = CAN_MSG_FRAME_FORMAT_CAN,
    .baudrate_normal = CAN_BAUDRATE_500K,
    .baudrate_FD = CAN_BAUDRATE_CUSTOM
};

/**
 * @brief Converts payload length in bytes to STM32C5xx DLC enum.
 * @param[in] bytes Payload length in bytes (0-64).
 * @return FDCAN DLC enum value.
 */
static hal_fdcan_data_length_code_t bytes_to_fdcan_dlc(uint8_t bytes)
{
    if (bytes <= 8)  return (hal_fdcan_data_length_code_t)bytes;
    if (bytes <= 12) return HAL_FDCAN_DATA_LEN_FDCAN_12_BYTE;
    if (bytes <= 16) return HAL_FDCAN_DATA_LEN_FDCAN_16_BYTE;
    if (bytes <= 20) return HAL_FDCAN_DATA_LEN_FDCAN_20_BYTE;
    if (bytes <= 24) return HAL_FDCAN_DATA_LEN_FDCAN_24_BYTE;
    if (bytes <= 32) return HAL_FDCAN_DATA_LEN_FDCAN_32_BYTE;
    if (bytes <= 48) return HAL_FDCAN_DATA_LEN_FDCAN_48_BYTE;
    return HAL_FDCAN_DATA_LEN_FDCAN_64_BYTE;
}

/**
 * @brief Converts STM32C5xx DLC enum to payload length in bytes.
 * @param[in] dlc FDCAN DLC enum value.
 * @return Length in bytes (0-64).
 */
static uint8_t fdcan_dlc_to_bytes(hal_fdcan_data_length_code_t dlc)
{
    switch (dlc)
    {
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_0_BYTE: return 0;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_1_BYTE: return 1;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_2_BYTE: return 2;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_3_BYTE: return 3;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_4_BYTE: return 4;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_5_BYTE: return 5;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_6_BYTE: return 6;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_7_BYTE: return 7;
        case HAL_FDCAN_DATA_LEN_CAN_FDCAN_8_BYTE: return 8;
        case HAL_FDCAN_DATA_LEN_FDCAN_12_BYTE:    return 12;
        case HAL_FDCAN_DATA_LEN_FDCAN_16_BYTE:    return 16;
        case HAL_FDCAN_DATA_LEN_FDCAN_20_BYTE:    return 20;
        case HAL_FDCAN_DATA_LEN_FDCAN_24_BYTE:    return 24;
        case HAL_FDCAN_DATA_LEN_FDCAN_32_BYTE:    return 32;
        case HAL_FDCAN_DATA_LEN_FDCAN_48_BYTE:    return 48;
        case HAL_FDCAN_DATA_LEN_FDCAN_64_BYTE:    return 64;
        default:                                  return 8;
    }
}

/**
 * @brief Configures nominal bit rate timing for 144 MHz clock and 87.5% sample point.
 * @param[in] hfdcan FDCAN peripheral handle.
 * @param[in] baudrate Nominal bit rate.
 * @return true on success, false if unsupported bit rate or HAL error.
 */
static bool stm32c5_set_nominal_baudrate(hal_fdcan_handle_t *hfdcan, can_baudrate_t baudrate)
{
    if (baudrate == CAN_BAUDRATE_CUSTOM)
    {
        /* Preserve default hardware configuration from mx_fdcan1.c */
        return true;
    }

    hal_fdcan_nominal_bit_timing_t timing = {0};
    timing.nominal_jump_width = 1;

    switch (baudrate)
    {
        case CAN_BAUDRATE_1000K:
            timing.nominal_prescaler = 1;
            timing.nominal_time_seg1 = 125;
            timing.nominal_time_seg2 = 18;
            break;

        case CAN_BAUDRATE_500K:
            timing.nominal_prescaler = 1;
            timing.nominal_time_seg1 = 251;
            timing.nominal_time_seg2 = 36;
            break;

        case CAN_BAUDRATE_250K:
            timing.nominal_prescaler = 2;
            timing.nominal_time_seg1 = 251;
            timing.nominal_time_seg2 = 36;
            break;

        case CAN_BAUDRATE_125K:
            timing.nominal_prescaler = 4;
            timing.nominal_time_seg1 = 251;
            timing.nominal_time_seg2 = 36;
            break;

        default:
            return false;
    }

    return (HAL_FDCAN_SetNominalBitTiming(hfdcan, &timing) == HAL_OK);
}

/**
 * @brief Configures fast data bit rate timing for CAN FD (144 MHz clock, 75% sample point).
 * @param[in] hfdcan FDCAN peripheral handle.
 * @param[in] baudrate Fast data bit rate.
 * @return true on success, false if unsupported bit rate or HAL error.
 */
static bool stm32c5_set_data_baudrate(hal_fdcan_handle_t *hfdcan, can_baudrate_t baudrate)
{
    if (baudrate == CAN_BAUDRATE_CUSTOM)
    {
        return true;
    }

    hal_fdcan_data_bit_timing_t timing = {0};
    timing.data_time_seg1 = 26;
    timing.data_time_seg2 = 9;
    timing.data_jump_width = 4;

    switch (baudrate)
    {
        case CAN_BAUDRATE_4000K:
            timing.data_prescaler = 1;
            break;

        case CAN_BAUDRATE_2000K:
            timing.data_prescaler = 2;
            break;

        case CAN_BAUDRATE_1000K:
            timing.data_prescaler = 4;
            break;

        case CAN_BAUDRATE_500K:
            timing.data_prescaler = 8;
            break;

        default:
            return false;
    }

    return (HAL_FDCAN_SetDataBitTiming(hfdcan, &timing) == HAL_OK);
}

/**
 * @brief Checks if Bit Rate Switch (BRS) is enabled for the current configuration.
 * @param[in] config Pointer to CAN configuration parameters.
 * @return true if BRS is active (CAN FD with faster data phase bit rate), false otherwise.
 */
static bool stm32c5_is_brs_enabled(const can_config_t *config)
{
    if (config == NULL)
    {
        return false;
    }

    return (config->frame_format == CAN_MSG_FRAME_FORMAT_FD_CAN) &&
           (config->baudrate_FD != CAN_BAUDRATE_CUSTOM) &&
           (config->baudrate_FD > config->baudrate_normal);
}

/**
 * @brief Initializes CAN1 controller with given configuration (frame format, nominal and FD bit rates).
 * @param[in] config Pointer to CAN configuration parameters.
 * @return true on success, false on error.
 */
static bool stm32c5_can1_init(const can_config_t *config)
{
    hal_fdcan_handle_t *hfdcan = mx_fdcan1_gethandle();
    if (hfdcan == NULL)
    {
        return false;
    }

    /* Stop FDCAN peripheral if currently active before reconfiguring */
    if (HAL_FDCAN_GetState(hfdcan) == HAL_FDCAN_STATE_ACTIVE)
    {
        if (HAL_FDCAN_Stop(hfdcan) != HAL_OK)
        {
            return false;
        }
    }

    /* Cache active configuration */
    if (config != NULL)
    {
        s_can1_config = *config;
    }
    else
    {
        s_can1_config.frame_format = CAN_MSG_FRAME_FORMAT_CAN;
        s_can1_config.baudrate_normal = CAN_BAUDRATE_500K;
        s_can1_config.baudrate_FD = CAN_BAUDRATE_CUSTOM;
    }

    /* Configure nominal bit rate */
    if (!stm32c5_set_nominal_baudrate(hfdcan, s_can1_config.baudrate_normal))
    {
        return false;
    }

    /* Configure frame format (Classic CAN vs CAN FD) */
    if (s_can1_config.frame_format == CAN_MSG_FRAME_FORMAT_FD_CAN)
    {
        if (stm32c5_is_brs_enabled(&s_can1_config))
        {
            if (HAL_FDCAN_SetFrameFormat(hfdcan, HAL_FDCAN_FRAME_FORMAT_FD_BRS) != HAL_OK)
            {
                return false;
            }

            if (!stm32c5_set_data_baudrate(hfdcan, s_can1_config.baudrate_FD))
            {
                return false;
            }
        }
        else
        {
            /* CAN FD mode without bit rate switching (data baudrate equals nominal or not specified) */
            if (HAL_FDCAN_SetFrameFormat(hfdcan, HAL_FDCAN_FRAME_FORMAT_FD_NO_BRS) != HAL_OK)
            {
                return false;
            }
        }
    }
    else
    {
        /* Classic CAN mode */
        if (HAL_FDCAN_SetFrameFormat(hfdcan, HAL_FDCAN_FRAME_FORMAT_CLASSIC_CAN) != HAL_OK)
        {
            return false;
        }
    }

    /* Set safe interrupt priority for FreeRTOS (>= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY) */
    HAL_CORTEX_NVIC_SetPriority(FDCAN1_IT0_IRQn, HAL_CORTEX_NVIC_PREEMP_PRIORITY_5, HAL_CORTEX_NVIC_SUB_PRIORITY_0);
    HAL_CORTEX_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);

    /* Assign Rx FIFO 0 interrupt group to line 0 and enable */
    HAL_FDCAN_SetInterruptGroupsToLine(hfdcan, HAL_FDCAN_IT_GROUP_RX_FIFO_0, HAL_FDCAN_IT_LINE_0);
    HAL_FDCAN_EnableInterruptLines(hfdcan, HAL_FDCAN_IT_LINE_0);
    HAL_FDCAN_EnableInterrupts(hfdcan, HAL_FDCAN_IT_RX_FIFO_0_NEW_MSG);

    /* Start the peripheral */
    if (HAL_FDCAN_Start(hfdcan) != HAL_OK)
    {
        return false;
    }

    return true;
}

/**
 * @brief Transmits a CAN or CAN FD message.
 * @param[in] id CAN Identifier (Standard or Extended).
 * @param[in] data Pointer to payload data buffer.
 * @param[in] len Length of data in bytes.
 * @return true on success, false if TX FIFO is full or parameter error.
 */
static bool stm32c5_can1_transmit(uint32_t id, const uint8_t* data, uint8_t len)
{
    hal_fdcan_handle_t *hfdcan = mx_fdcan1_gethandle();
    if (hfdcan == NULL || data == NULL)
    {
        return false;
    }

    /* Start the FDCAN module if not already active */
    if (HAL_FDCAN_GetState(hfdcan) != HAL_FDCAN_STATE_ACTIVE)
    {
        if (HAL_FDCAN_Start(hfdcan) != HAL_OK)
        {
            return false;
        }
    }

    if (HAL_FDCAN_GetTxFifoFreeLevel(hfdcan) == 0)
    {
        return false;
    }

    hal_fdcan_tx_header_t tx_header = {0};
    uint8_t max_payload = (s_can1_config.frame_format == CAN_MSG_FRAME_FORMAT_FD_CAN) ? 64U : 8U;
    uint8_t payload_len = (len > max_payload) ? max_payload : len;

    tx_header.b.data_length = bytes_to_fdcan_dlc(payload_len);
    tx_header.b.identifier_type = CAN_msg_is_extended_id(id) ? HAL_FDCAN_ID_EXTENDED : HAL_FDCAN_ID_STANDARD;
    tx_header.b.identifier = CAN_msg_get_can_id(id);
    tx_header.b.frame_type = HAL_FDCAN_FRAME_DATA;

    if (s_can1_config.frame_format == CAN_MSG_FRAME_FORMAT_FD_CAN)
    {
        tx_header.b.frame_format = HAL_FDCAN_HEADER_FRAME_FORMAT_FD_CAN;
        tx_header.b.bit_rate_switch = stm32c5_is_brs_enabled(&s_can1_config)
                                      ? HAL_FDCAN_BIT_RATE_SWITCH_ON
                                      : HAL_FDCAN_BIT_RATE_SWITCH_OFF;
    }
    else
    {
        tx_header.b.frame_format = HAL_FDCAN_HEADER_FRAME_FORMAT_CAN;
        tx_header.b.bit_rate_switch = HAL_FDCAN_BIT_RATE_SWITCH_OFF;
    }

    if (HAL_FDCAN_ReqTransmitMsgFromFIFOQ(hfdcan, &tx_header, data) != HAL_OK)
    {
        return false;
    }

    return true;
}

/**
 * @brief Receives a CAN or CAN FD message from Rx FIFO 0.
 * @param[out] id Pointer to variable storing received identifier.
 * @param[out] data Buffer for received payload.
 * @param[out] len Pointer to variable storing received data length.
 * @return true on success, false if FIFO is empty or error occurred.
 */
static bool stm32c5_can1_receive(uint32_t* id, uint8_t* data, uint8_t* len)
{
    hal_fdcan_handle_t *hfdcan = mx_fdcan1_gethandle();
    if (hfdcan == NULL || id == NULL || data == NULL || len == NULL)
    {
        return false;
    }

    uint32_t fill_level = 0;
    HAL_FDCAN_GetRxFifoFillLevel(hfdcan, HAL_FDCAN_RX_FIFO_0, &fill_level);
    if (fill_level == 0)
    {
        return false;
    }

    hal_fdcan_rx_header_t rx_header = {0};
    if (HAL_FDCAN_GetReceivedMessage(hfdcan, HAL_FDCAN_RX_FIFO_0, &rx_header, data) != HAL_OK)
    {
        return false;
    }

    *len = fdcan_dlc_to_bytes(rx_header.b.data_length);
    if (rx_header.b.identifier_type == HAL_FDCAN_ID_EXTENDED)
    {
        *id = CAN_msg_set_extended_id(rx_header.b.identifier);
    }
    else
    {
        *id = CAN_msg_set_standard_id(rx_header.b.identifier);
    }

    return true;
}

/**
 * @brief Attaches RX notification callback for CAN1.
 * @param[in] cb Callback function pointer.
 * @param[in] context User context passed to callback.
 */
static void stm32c5_can1_attach_rx_callback(can_rx_notify_fn cb, void* context)
{
    s_can1_rx_cb = cb;
    s_can1_rx_context = context;
}

/**
 * @brief Callback invoked from interrupt (ISR) upon Rx FIFO 0 events.
 * @param[in] hfdcan FDCAN peripheral handle.
 * @param[in] rx_fifo0_interrupts FIFO 0 interrupt flags.
 */
void HAL_FDCAN_RxFifo0Callback(hal_fdcan_handle_t *hfdcan, uint32_t rx_fifo0_interrupts)
{
    if (hfdcan == mx_fdcan1_gethandle())
    {
        if ((rx_fifo0_interrupts & (FDCAN_IR_RF0N | FDCAN_IR_RF0F)) != 0U)
        {
            if (s_can1_rx_cb != NULL)
            {
                s_can1_rx_cb(s_can1_rx_context);
            }
        }
    }
}

/**
 * @brief Hardware interface constant for CAN1.
 */
static const can_interface_t s_stm32c5_can1_interface = {
    .init = stm32c5_can1_init,
    .transmit = stm32c5_can1_transmit,
    .receive = stm32c5_can1_receive,
    .attach_rx_callback = stm32c5_can1_attach_rx_callback
};

const can_interface_t *stm32c5_can1_interfaceGet(void)
{
    return &s_stm32c5_can1_interface;
}