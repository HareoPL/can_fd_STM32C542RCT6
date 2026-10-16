/**
 * @file stm32c5_can_adapter.c
 * @brief STM32C5 hardware adapter implementation for CAN and CAN FD bus (CAN1 and CAN2).
 */

#include "stm32c5_can_adapter.h"
#include "can_port.h"
#include "can_msg_types.h"
#include "stm32_hal.h"
#include "stm32c5xx_hal_fdcan.h"
#include "stm32c5xx_hal_cortex.h"
#include "mx_fdcan1.h"
#include "mx_fdcan2.h"
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
 * @brief Variables storing RX notification callback and context for CAN2.
 */
static can_rx_notify_fn s_can2_rx_cb = NULL;
static void* s_can2_rx_context = NULL;

/**
 * @brief Active configuration cached for CAN2 instance.
 */
static can_config_t s_can2_config = {
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
        /* Preserve default hardware configuration */
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

    /* Set Synchronization Jump Width equal to Phase_Seg2 for robust bit resynchronization */
    timing.nominal_jump_width = timing.nominal_time_seg2;

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
 * @brief Generic initialization routine for any FDCAN instance.
 * @param[in] hfdcan FDCAN peripheral handle.
 * @param[in,out] p_cached_config Pointer to cached config structure.
 * @param[in] config Pointer to desired CAN configuration.
 * @param[in] irq_n NVIC IRQ number for this FDCAN instance line 0.
 * @return true on success, false on error.
 */
static bool stm32c5_can_init_generic(hal_fdcan_handle_t *hfdcan,
                                     can_config_t *p_cached_config,
                                     const can_config_t *config,
                                     IRQn_Type irq_n)
{
    if (hfdcan == NULL || p_cached_config == NULL)
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
        *p_cached_config = *config;
    }
    else
    {
        p_cached_config->frame_format = CAN_MSG_FRAME_FORMAT_CAN;
        p_cached_config->baudrate_normal = CAN_BAUDRATE_500K;
        p_cached_config->baudrate_FD = CAN_BAUDRATE_CUSTOM;
    }

    /* Configure nominal bit rate */
    if (!stm32c5_set_nominal_baudrate(hfdcan, p_cached_config->baudrate_normal))
    {
        return false;
    }

    /* Configure frame format (Classic CAN vs CAN FD) */
    if (p_cached_config->frame_format == CAN_MSG_FRAME_FORMAT_FD_CAN)
    {
        if (stm32c5_is_brs_enabled(p_cached_config))
        {
            if (HAL_FDCAN_SetFrameFormat(hfdcan, HAL_FDCAN_FRAME_FORMAT_FD_BRS) != HAL_OK)
            {
                return false;
            }

            if (!stm32c5_set_data_baudrate(hfdcan, p_cached_config->baudrate_FD))
            {
                return false;
            }
        }
        else
        {
            /* CAN FD mode without bit rate switching */
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
    HAL_CORTEX_NVIC_SetPriority(irq_n, HAL_CORTEX_NVIC_PREEMP_PRIORITY_5, HAL_CORTEX_NVIC_SUB_PRIORITY_0);
    HAL_CORTEX_NVIC_EnableIRQ(irq_n);

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
 * @brief Generic message transmission routine for any FDCAN instance.
 * @param[in] hfdcan FDCAN peripheral handle.
 * @param[in] cached_config Pointer to active configuration.
 * @param[in] id CAN Identifier (Standard or Extended).
 * @param[in] data Pointer to payload data buffer.
 * @param[in] len Length of data in bytes.
 * @return true on success, false if TX FIFO is full or parameter error.
 */
static bool stm32c5_can_transmit_generic(hal_fdcan_handle_t *hfdcan,
                                         const can_config_t *cached_config,
                                         uint32_t id,
                                         const uint8_t* data,
                                         uint8_t len)
{
    if (hfdcan == NULL || cached_config == NULL || data == NULL)
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
    uint8_t max_payload = (cached_config->frame_format == CAN_MSG_FRAME_FORMAT_FD_CAN) ? 64U : 8U;
    uint8_t payload_len = (len > max_payload) ? max_payload : len;

    tx_header.b.data_length = bytes_to_fdcan_dlc(payload_len);
    tx_header.b.identifier_type = CAN_msg_is_extended_id(id) ? HAL_FDCAN_ID_EXTENDED : HAL_FDCAN_ID_STANDARD;
    tx_header.b.identifier = CAN_msg_get_can_id(id);
    tx_header.b.frame_type = HAL_FDCAN_FRAME_DATA;

    if (cached_config->frame_format == CAN_MSG_FRAME_FORMAT_FD_CAN)
    {
        tx_header.b.frame_format = HAL_FDCAN_HEADER_FRAME_FORMAT_FD_CAN;
        tx_header.b.bit_rate_switch = stm32c5_is_brs_enabled(cached_config)
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
 * @brief Generic message reception routine from Rx FIFO 0.
 * @param[in] hfdcan FDCAN peripheral handle.
 * @param[out] id Pointer to variable storing received identifier.
 * @param[out] data Buffer for received payload.
 * @param[out] len Pointer to variable storing received data length.
 * @return true on success, false if FIFO is empty or error occurred.
 */
static bool stm32c5_can_receive_generic(hal_fdcan_handle_t *hfdcan,
                                        uint32_t* id,
                                        uint8_t* data,
                                        uint8_t* len)
{
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
 * @brief Generic filter configuration routine for any FDCAN instance.
 * @param[in] hfdcan FDCAN peripheral handle.
 * @param[in] filters Array of filter configurations.
 * @param[in] num_filters Number of filters in the array (0 to accept all frames).
 * @return true on success, false on error.
 */
static bool stm32c5_can_set_filter_generic(hal_fdcan_handle_t *hfdcan,
                                           const can_filter_t *filters,
                                           uint32_t num_filters)
{
    if (hfdcan == NULL)
    {
        return false;
    }

    bool was_active = (HAL_FDCAN_GetState(hfdcan) == HAL_FDCAN_STATE_ACTIVE);
    if (was_active)
    {
        if (HAL_FDCAN_Stop(hfdcan) != HAL_OK)
        {
            return false;
        }
    }

    FDCAN_GlobalTypeDef *p_fdcanx = (FDCAN_GlobalTypeDef *)((uint32_t)hfdcan->instance);

    if (filters == NULL || num_filters == 0U)
    {
        /* Accept all frames (pass-all open filter) */
        STM32_MODIFY_REG(p_fdcanx->RXGFC, FDCAN_RXGFC_LSS, (0U << FDCAN_RXGFC_LSS_Pos));
        STM32_MODIFY_REG(p_fdcanx->RXGFC, FDCAN_RXGFC_LSE, (0U << FDCAN_RXGFC_LSE_Pos));

        hal_fdcan_global_filter_config_t g_filter = {
            .acceptance_non_matching_std = HAL_FDCAN_NO_MATCH_TO_RX_FIFO_0,
            .acceptance_non_matching_ext = HAL_FDCAN_NO_MATCH_TO_RX_FIFO_0,
            .acceptance_remote_std       = HAL_FDCAN_REMOTE_REJECT,
            .acceptance_remote_ext       = HAL_FDCAN_REMOTE_REJECT
        };
        HAL_FDCAN_SetGlobalFilter(hfdcan, &g_filter);

        if (was_active)
        {
            return (HAL_FDCAN_Start(hfdcan) == HAL_OK);
        }
        return true;
    }

    /* 1. Count standard and extended filters */
    uint32_t std_count = 0U;
    uint32_t ext_count = 0U;
    for (uint32_t i = 0U; i < num_filters; i++)
    {
        if (filters[i].arb > CAN_SFF_MASK)
        {
            ext_count++;
        }
        else
        {
            std_count++;
        }
    }

    /* Hardware limits for M_CAN: max 28 standard, max 8 extended */
    if (std_count > 28U || ext_count > 8U)
    {
        if (was_active)
        {
            (void)HAL_FDCAN_Start(hfdcan);
        }
        return false;
    }

    /* 2. Configure list sizes in RXGFC */
    STM32_MODIFY_REG(p_fdcanx->RXGFC, FDCAN_RXGFC_LSS, (std_count << FDCAN_RXGFC_LSS_Pos));
    STM32_MODIFY_REG(p_fdcanx->RXGFC, FDCAN_RXGFC_LSE, (ext_count << FDCAN_RXGFC_LSE_Pos));

    /* 3. Write each filter element to Message RAM */
    uint32_t std_idx = 0U;
    uint32_t ext_idx = 0U;

    for (uint32_t i = 0U; i < num_filters; i++)
    {
        hal_fdcan_filter_t f_cfg = {0};
        f_cfg.filter_type = HAL_FDCAN_FILTER_TYPE_CLASSIC;
        f_cfg.filter_config = HAL_FDCAN_FILTER_TO_RX_FIFO_0;

        if (CAN_msg_is_extended_id(filters[i].arb) || (filters[i].arb > CAN_SFF_MASK))
        {
            f_cfg.id_type = HAL_FDCAN_ID_EXTENDED;
            f_cfg.filter_index = ext_idx++;
            f_cfg.filter_id1 = filters[i].arb;
            f_cfg.filter_id2 = filters[i].mask & CAN_EFF_MASK;
        }
        else
        {
            f_cfg.id_type = HAL_FDCAN_ID_STANDARD;
            f_cfg.filter_index = std_idx++;
            f_cfg.filter_id1 = filters[i].arb;
            f_cfg.filter_id2 = filters[i].mask & CAN_SFF_MASK;
        }

        if (HAL_FDCAN_SetFilter(hfdcan, &f_cfg) != HAL_OK)
        {
            if (was_active)
            {
                (void)HAL_FDCAN_Start(hfdcan);
            }
            return false;
        }
    }

    /* 4. Configure global filter rejection for non-matching frames */
    hal_fdcan_global_filter_config_t g_filter = {
        .acceptance_non_matching_std = (std_count > 0U) ? HAL_FDCAN_NO_MATCH_REJECT : HAL_FDCAN_NO_MATCH_TO_RX_FIFO_0,
        .acceptance_non_matching_ext = (ext_count > 0U) ? HAL_FDCAN_NO_MATCH_REJECT : HAL_FDCAN_NO_MATCH_TO_RX_FIFO_0,
        .acceptance_remote_std       = HAL_FDCAN_REMOTE_REJECT,
        .acceptance_remote_ext       = HAL_FDCAN_REMOTE_REJECT
    };
    HAL_FDCAN_SetGlobalFilter(hfdcan, &g_filter);

    if (was_active)
    {
        return (HAL_FDCAN_Start(hfdcan) == HAL_OK);
    }

    return true;
}

/* ===========================================================================
 * CAN1 Implementation
 * =========================================================================== */

static bool stm32c5_can1_init(const can_config_t *config)
{
    return stm32c5_can_init_generic(mx_fdcan1_gethandle(), &s_can1_config, config, FDCAN1_IT0_IRQn);
}

static bool stm32c5_can1_transmit(uint32_t id, const uint8_t* data, uint8_t len)
{
    return stm32c5_can_transmit_generic(mx_fdcan1_gethandle(), &s_can1_config, id, data, len);
}

static bool stm32c5_can1_receive(uint32_t* id, uint8_t* data, uint8_t* len)
{
    return stm32c5_can_receive_generic(mx_fdcan1_gethandle(), id, data, len);
}

static void stm32c5_can1_attach_rx_callback(can_rx_notify_fn cb, void* context)
{
    s_can1_rx_cb = cb;
    s_can1_rx_context = context;
}

static bool stm32c5_can1_set_filter(const can_filter_t *filters, uint32_t num_filters)
{
    return stm32c5_can_set_filter_generic(mx_fdcan1_gethandle(), filters, num_filters);
}

static const can_interface_t s_stm32c5_can1_interface = {
    .init = stm32c5_can1_init,
    .transmit = stm32c5_can1_transmit,
    .receive = stm32c5_can1_receive,
    .attach_rx_callback = stm32c5_can1_attach_rx_callback,
    .set_filter = stm32c5_can1_set_filter
};

const can_interface_t *stm32c5_can1_interfaceGet(void)
{
    return &s_stm32c5_can1_interface;
}

/* ===========================================================================
 * CAN2 Implementation
 * =========================================================================== */

static bool stm32c5_can2_init(const can_config_t *config)
{
    return stm32c5_can_init_generic(mx_fdcan2_gethandle(), &s_can2_config, config, FDCAN2_IT0_IRQn);
}

static bool stm32c5_can2_transmit(uint32_t id, const uint8_t* data, uint8_t len)
{
    return stm32c5_can_transmit_generic(mx_fdcan2_gethandle(), &s_can2_config, id, data, len);
}

static bool stm32c5_can2_receive(uint32_t* id, uint8_t* data, uint8_t* len)
{
    return stm32c5_can_receive_generic(mx_fdcan2_gethandle(), id, data, len);
}

static void stm32c5_can2_attach_rx_callback(can_rx_notify_fn cb, void* context)
{
    s_can2_rx_cb = cb;
    s_can2_rx_context = context;
}

static bool stm32c5_can2_set_filter(const can_filter_t *filters, uint32_t num_filters)
{
    return stm32c5_can_set_filter_generic(mx_fdcan2_gethandle(), filters, num_filters);
}

static const can_interface_t s_stm32c5_can2_interface = {
    .init = stm32c5_can2_init,
    .transmit = stm32c5_can2_transmit,
    .receive = stm32c5_can2_receive,
    .attach_rx_callback = stm32c5_can2_attach_rx_callback,
    .set_filter = stm32c5_can2_set_filter
};

const can_interface_t *stm32c5_can2_interfaceGet(void)
{
    return &s_stm32c5_can2_interface;
}

/* ===========================================================================
 * Common Interrupt Callback Handler
 * =========================================================================== */

/**
 * @brief Callback invoked from interrupt (ISR) upon Rx FIFO 0 events for either FDCAN1 or FDCAN2.
 * @param[in] hfdcan FDCAN peripheral handle.
 * @param[in] rx_fifo0_interrupts FIFO 0 interrupt flags.
 */
void HAL_FDCAN_RxFifo0Callback(hal_fdcan_handle_t *hfdcan, uint32_t rx_fifo0_interrupts)
{
    if ((rx_fifo0_interrupts & (FDCAN_IR_RF0N | FDCAN_IR_RF0F)) == 0U)
    {
        return;
    }

    if (hfdcan == mx_fdcan1_gethandle())
    {
        if (s_can1_rx_cb != NULL)
        {
            s_can1_rx_cb(s_can1_rx_context);
        }
    }
    else if (hfdcan == mx_fdcan2_gethandle())
    {
        if (s_can2_rx_cb != NULL)
        {
            s_can2_rx_cb(s_can2_rx_context);
        }
    }
}