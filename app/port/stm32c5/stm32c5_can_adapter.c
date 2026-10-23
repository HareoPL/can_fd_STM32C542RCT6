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
#include <string.h>

/**
 * @brief Variables storing RX notification callback and context for CAN1.
 */
static can_rx_notify_fn s_can1_rx_cb = NULL;
static void* s_can1_rx_context = NULL;

/**
 * @brief Active configuration cached for CAN1 instance.
 */
static can_config_t s_can1_config = {
    .mode = CAN_MODE_CLASSIC,
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
    .mode = CAN_MODE_CLASSIC,
    .baudrate_normal = CAN_BAUDRATE_500K,
    .baudrate_FD = CAN_BAUDRATE_CUSTOM
};

/**
 * @brief Error diagnostics accumulated per FDCAN instance. ECR.CEL is cleared on read and LEC/DLEC
 *        are overwritten by the next frame, so they are collected on read and in the error interrupt.
 */
typedef struct
{
    uint32_t error_log_total;              /**< Accumulated ECR.CEL */
    uint16_t arb_errors[CAN_ERROR_TYPES];  /**< Protocol errors per type in arbitration phase (PEA) */
    uint16_t data_errors[CAN_ERROR_TYPES]; /**< Protocol errors per type in data phase (PED) */
} stm32c5_can_diag_t;

/** @brief Error diagnostics of CAN1 and CAN2 (updated from the FDCAN interrupt). */
static volatile stm32c5_can_diag_t s_can1_diag;
static volatile stm32c5_can_diag_t s_can2_diag;

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

/** @brief Nominal (arbitration) phase sample point in per mille. */
#define STM32C5_NOMINAL_SAMPLE_POINT 875U

/** @brief Data phase sample point in per mille. */
#define STM32C5_DATA_SAMPLE_POINT    750U

/** @brief Maximum accepted deviation from the requested sample point in per mille. */
#define STM32C5_SAMPLE_POINT_TOLERANCE 30U

/** @brief Minimum number of time quanta per bit accepted by the bit timing calculator. */
#define STM32C5_MIN_TQ_PER_BIT       8U

/** @brief Highest data prescaler for which Transmitter Delay Compensation is allowed (M_CAN constraint). */
#define STM32C5_TDC_MAX_PRESCALER    2U

/**
 * @brief Register field limits of one bit timing phase (Prescaler, TSEG1, TSEG2, SJW).
 */
typedef struct
{
    uint32_t prescaler_max;
    uint32_t tseg1_max;
    uint32_t tseg2_max;
    uint32_t sjw_max;
} stm32c5_timing_limits_t;

/**
 * @brief Calculated bit timing of one phase.
 */
typedef struct
{
    uint32_t prescaler; /**< Time quantum prescaler */
    uint32_t tseg1;     /**< Prop_Seg + Phase_Seg1 in time quanta */
    uint32_t tseg2;     /**< Phase_Seg2 in time quanta */
    uint32_t sjw;       /**< Synchronization jump width in time quanta */
} stm32c5_bit_timing_t;

/** @brief FDCAN NBTP register limits (nominal phase). */
static const stm32c5_timing_limits_t s_nominal_limits = { 512U, 256U, 128U, 128U };

/** @brief FDCAN DBTP register limits (data phase). */
static const stm32c5_timing_limits_t s_data_limits = { 32U, 32U, 16U, 16U };

/**
 * @brief Returns the FDCAN time quantum source clock (kernel clock after the common divider).
 * @param[in] hfdcan FDCAN peripheral handle.
 * @return Clock frequency in Hz.
 */
static uint32_t stm32c5_get_can_clock_hz(const hal_fdcan_handle_t *hfdcan)
{
    /* CKDIV encoding: 0 = /1, n = /(2*n) */
    uint32_t div_code = (uint32_t)HAL_FDCAN_GetClockDivider(hfdcan);
    uint32_t divider = (div_code == 0U) ? 1U : (2U * div_code);

    return HAL_FDCAN_GetClockFreq(hfdcan) / divider;
}

/**
 * @brief Calculates bit timing for an exact bit rate, searching from the smallest prescaler
 *        (highest time quantum resolution) within the given prescaler range.
 * @param[in] clock_hz FDCAN clock frequency in Hz.
 * @param[in] bitrate Requested bit rate in bit/s.
 * @param[in] sample_point Requested sample point in per mille.
 * @param[in] prescaler_min Lowest prescaler to try.
 * @param[in] prescaler_max Highest prescaler to try (clipped to the register limit).
 * @param[in] limits Register field limits of the phase.
 * @param[out] out Calculated timing.
 * @return true if an exact timing was found, false otherwise.
 */
static bool stm32c5_calc_bit_timing(uint32_t clock_hz,
                                    uint32_t bitrate,
                                    uint32_t sample_point,
                                    uint32_t prescaler_min,
                                    uint32_t prescaler_max,
                                    const stm32c5_timing_limits_t *limits,
                                    stm32c5_bit_timing_t *out)
{
    if (clock_hz == 0U || bitrate == 0U || limits == NULL || out == NULL)
    {
        return false;
    }

    if (prescaler_max > limits->prescaler_max)
    {
        prescaler_max = limits->prescaler_max;
    }

    for (uint32_t prescaler = prescaler_min; prescaler <= prescaler_max; prescaler++)
    {
        /* Bit rate must be reachable exactly, CAN has no tolerance for rounding */
        uint32_t clocks_per_bit = prescaler * bitrate;
        if ((clock_hz % clocks_per_bit) != 0U)
        {
            continue;
        }

        uint32_t tq_per_bit = clock_hz / clocks_per_bit;
        if (tq_per_bit < STM32C5_MIN_TQ_PER_BIT || tq_per_bit > (1U + limits->tseg1_max + limits->tseg2_max))
        {
            continue;
        }

        uint32_t tseg2 = ((tq_per_bit * (1000U - sample_point)) + 500U) / 1000U;
        if (tseg2 < 1U)
        {
            tseg2 = 1U;
        }
        if (tseg2 > limits->tseg2_max)
        {
            tseg2 = limits->tseg2_max;
        }

        uint32_t tseg1 = tq_per_bit - 1U - tseg2;
        if (tseg1 > limits->tseg1_max)
        {
            tseg1 = limits->tseg1_max;
            tseg2 = tq_per_bit - 1U - tseg1;
        }

        if (tseg1 < 1U || tseg2 > limits->tseg2_max)
        {
            continue;
        }

        /* Segment clipping may move the sample point; try a larger prescaler instead */
        uint32_t actual_sp = ((1U + tseg1) * 1000U) / tq_per_bit;
        uint32_t sp_error = (actual_sp > sample_point) ? (actual_sp - sample_point) : (sample_point - actual_sp);
        if (sp_error > STM32C5_SAMPLE_POINT_TOLERANCE)
        {
            continue;
        }

        out->prescaler = prescaler;
        out->tseg1 = tseg1;
        out->tseg2 = tseg2;
        out->sjw = (tseg2 < limits->sjw_max) ? tseg2 : limits->sjw_max;
        return true;
    }

    return false;
}

/**
 * @brief Configures nominal and data phase bit timing plus transmitter delay compensation.
 *        Peripheral must be in IDLE state.
 * @param[in] hfdcan FDCAN peripheral handle.
 * @param[in] config Desired bus configuration.
 * @return true on success, false if a bit rate is not reachable with the current clock or HAL error.
 */
static bool stm32c5_apply_bit_timing(hal_fdcan_handle_t *hfdcan, const can_config_t *config)
{
    const uint32_t clock_hz = stm32c5_get_can_clock_hz(hfdcan);
    const bool brs = (config->mode == CAN_MODE_FD_BRS);
    stm32c5_bit_timing_t data = {0};

    if (brs)
    {
        /* Bit rate switching requires an explicit data phase bit rate */
        if (config->baudrate_FD == CAN_BAUDRATE_CUSTOM)
        {
            return false;
        }

        if (!stm32c5_calc_bit_timing(clock_hz, (uint32_t)config->baudrate_FD, STM32C5_DATA_SAMPLE_POINT,
                                     1U, s_data_limits.prescaler_max, &s_data_limits, &data))
        {
            return false;
        }
    }

    /* CAN_BAUDRATE_CUSTOM keeps the nominal timing generated by CubeMX */
    if (config->baudrate_normal != CAN_BAUDRATE_CUSTOM)
    {
        stm32c5_bit_timing_t nominal = {0};

        /* Prefer the data phase prescaler for both phases (identical time quantum, CiA 601-3) */
        bool found = brs && stm32c5_calc_bit_timing(clock_hz, (uint32_t)config->baudrate_normal,
                                                    STM32C5_NOMINAL_SAMPLE_POINT, data.prescaler,
                                                    data.prescaler, &s_nominal_limits, &nominal);
        if (!found)
        {
            found = stm32c5_calc_bit_timing(clock_hz, (uint32_t)config->baudrate_normal,
                                            STM32C5_NOMINAL_SAMPLE_POINT, 1U,
                                            s_nominal_limits.prescaler_max, &s_nominal_limits, &nominal);
        }
        if (!found)
        {
            return false;
        }

        hal_fdcan_nominal_bit_timing_t nominal_timing = {
            .nominal_prescaler  = nominal.prescaler,
            .nominal_time_seg1  = nominal.tseg1,
            .nominal_time_seg2  = nominal.tseg2,
            .nominal_jump_width = nominal.sjw
        };
        if (HAL_FDCAN_SetNominalBitTiming(hfdcan, &nominal_timing) != HAL_OK)
        {
            return false;
        }
    }

    if (!brs)
    {
        return (HAL_FDCAN_DisableTxDelayCompensation(hfdcan) == HAL_OK);
    }

    hal_fdcan_data_bit_timing_t data_timing = {
        .data_prescaler  = data.prescaler,
        .data_time_seg1  = data.tseg1,
        .data_time_seg2  = data.tseg2,
        .data_jump_width = data.sjw
    };
    if (HAL_FDCAN_SetDataBitTiming(hfdcan, &data_timing) != HAL_OK)
    {
        return false;
    }

    /* The DBTP write above clears the TDC bit, so compensation is enabled only afterwards.
     * TDC is valid for data prescaler 1 or 2; slower data phases do not need it. */
    if (data.prescaler > STM32C5_TDC_MAX_PRESCALER)
    {
        return true;
    }

    /* Secondary sample point placed at the data sample point, in FDCAN clock periods (mtq) */
    hal_fdcan_tx_delay_comp_config_t tdc = {
        .tx_delay_comp_offset     = data.prescaler * data.tseg1,
        .tx_delay_comp_win_length = 0U
    };
    if (HAL_FDCAN_SetConfigTxDelayCompensation(hfdcan, &tdc) != HAL_OK)
    {
        return false;
    }

    return (HAL_FDCAN_EnableTxDelayCompensation(hfdcan) == HAL_OK);
}

/**
 * @brief Generic initialization routine for any FDCAN instance.
 * @param[in] hfdcan FDCAN peripheral handle.
 * @param[in,out] p_cached_config Pointer to cached config structure (updated only on success).
 * @param[in] config Pointer to desired CAN configuration (NULL = Classic CAN 500 kbps).
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

    can_config_t new_config = {
        .mode = CAN_MODE_CLASSIC,
        .baudrate_normal = CAN_BAUDRATE_500K,
        .baudrate_FD = CAN_BAUDRATE_CUSTOM
    };
    if (config != NULL)
    {
        new_config = *config;
    }

    hal_fdcan_frame_format_t frame_format;
    switch (new_config.mode)
    {
        case CAN_MODE_CLASSIC:   frame_format = HAL_FDCAN_FRAME_FORMAT_CLASSIC_CAN; break;
        case CAN_MODE_FD_NO_BRS: frame_format = HAL_FDCAN_FRAME_FORMAT_FD_NO_BRS;   break;
        case CAN_MODE_FD_BRS:    frame_format = HAL_FDCAN_FRAME_FORMAT_FD_BRS;      break;
        default:                 return false;
    }

    if (!stm32c5_apply_bit_timing(hfdcan, &new_config))
    {
        return false;
    }

    if (HAL_FDCAN_SetFrameFormat(hfdcan, frame_format) != HAL_OK)
    {
        return false;
    }

    *p_cached_config = new_config;

    /* Set safe interrupt priority for FreeRTOS (>= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY) */
    HAL_CORTEX_NVIC_SetPriority(irq_n, HAL_CORTEX_NVIC_PREEMP_PRIORITY_5, HAL_CORTEX_NVIC_SUB_PRIORITY_0);
    HAL_CORTEX_NVIC_EnableIRQ(irq_n);

    /* Assign Rx FIFO 0 interrupt group to line 0 and enable */
    HAL_FDCAN_SetInterruptGroupsToLine(hfdcan, HAL_FDCAN_IT_GROUP_RX_FIFO_0 | HAL_FDCAN_IT_GROUP_PROTOCOL_ERROR,
                                       HAL_FDCAN_IT_LINE_0);
    HAL_FDCAN_EnableInterruptLines(hfdcan, HAL_FDCAN_IT_LINE_0);
    HAL_FDCAN_EnableInterrupts(hfdcan, HAL_FDCAN_IT_RX_FIFO_0_NEW_MSG |
                                       HAL_FDCAN_IT_ARB_PROTOCOL_ERROR | HAL_FDCAN_IT_DATA_PROTOCOL_ERROR);

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
 * @param[in] msg Frame to send; msg->format selects Classic or FD frame, msg->flags BRS / RTR.
 * @return true on success, false if TX FIFO is full, FD frame on Classic bus, FD remote frame
 *         or parameter error.
 */
static bool stm32c5_can_transmit_generic(hal_fdcan_handle_t *hfdcan,
                                         const can_config_t *cached_config,
                                         const CAN_msg_t *msg)
{
    if (hfdcan == NULL || cached_config == NULL || msg == NULL)
    {
        return false;
    }

    const bool is_fd = (msg->format == CAN_MSG_FRAME_FORMAT_FD_CAN);
    const bool is_rtr = ((msg->flags & CAN_MSG_FLAG_RTR) != 0U);

    /* FD frame would be destroyed by error frames on a Classic CAN bus */
    if (is_fd && cached_config->mode == CAN_MODE_CLASSIC)
    {
        return false;
    }

    /* CAN FD has no remote frames (the core would silently send a Classic RTR) */
    if (is_fd && is_rtr)
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

    uint8_t max_payload = is_fd ? 64U : 8U;
    uint8_t payload_len = (msg->len > max_payload) ? max_payload : msg->len;

    /* FD lengths above 8 are quantized (e.g. 10 -> 12 bytes), unused tail is padded with zeros */
    uint8_t tx_data[64] = {0};
    memcpy(tx_data, msg->data, payload_len);

    hal_fdcan_tx_header_t tx_header = {0};
    tx_header.b.data_length = bytes_to_fdcan_dlc(payload_len);
    tx_header.b.identifier_type = CAN_msg_is_extended_id(msg->ident) ? HAL_FDCAN_ID_EXTENDED : HAL_FDCAN_ID_STANDARD;
    tx_header.b.identifier = CAN_msg_get_can_id(msg->ident);
    /* Remote frame: DLC = requested length, the payload is not transmitted */
    tx_header.b.frame_type = is_rtr ? HAL_FDCAN_FRAME_REMOTE : HAL_FDCAN_FRAME_DATA;

    if (is_fd)
    {
        /* Without BRS_SET the bus mode decides; with it the frame may only switch BRS off */
        const bool brs = (cached_config->mode == CAN_MODE_FD_BRS)
                         && (((msg->flags & CAN_MSG_FLAG_BRS_SET) == 0U) || ((msg->flags & CAN_MSG_FLAG_BRS) != 0U));
        tx_header.b.frame_format = HAL_FDCAN_HEADER_FRAME_FORMAT_FD_CAN;
        tx_header.b.bit_rate_switch = brs ? HAL_FDCAN_BIT_RATE_SWITCH_ON : HAL_FDCAN_BIT_RATE_SWITCH_OFF;
    }
    else
    {
        tx_header.b.frame_format = HAL_FDCAN_HEADER_FRAME_FORMAT_CAN;
        tx_header.b.bit_rate_switch = HAL_FDCAN_BIT_RATE_SWITCH_OFF;
    }

    if (HAL_FDCAN_ReqTransmitMsgFromFIFOQ(hfdcan, &tx_header, tx_data) != HAL_OK)
    {
        return false;
    }

    return true;
}

/**
 * @brief Generic message reception routine from Rx FIFO 0.
 * @param[in] hfdcan FDCAN peripheral handle.
 * @param[out] msg Received frame (identifier, length, payload and actual frame format).
 * @return true on success, false if FIFO is empty or error occurred.
 */
static bool stm32c5_can_receive_generic(hal_fdcan_handle_t *hfdcan, CAN_msg_t *msg)
{
    if (hfdcan == NULL || msg == NULL)
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
    if (HAL_FDCAN_GetReceivedMessage(hfdcan, HAL_FDCAN_RX_FIFO_0, &rx_header, msg->data) != HAL_OK)
    {
        return false;
    }

    msg->len = fdcan_dlc_to_bytes(rx_header.b.data_length);
    msg->format = (rx_header.b.frame_format == HAL_FDCAN_HEADER_FRAME_FORMAT_FD_CAN)
                  ? CAN_MSG_FRAME_FORMAT_FD_CAN
                  : CAN_MSG_FRAME_FORMAT_CAN;
    /* BRS_SET stays clear: a frame echoed back (e.g. CAN FD test) follows the bus mode of the sender */
    msg->flags = 0U;
    if (rx_header.b.bit_rate_switch == HAL_FDCAN_BIT_RATE_SWITCH_ON)
    {
        msg->flags |= CAN_MSG_FLAG_BRS;
    }
    if (rx_header.b.frame_type == HAL_FDCAN_FRAME_REMOTE)
    {
        msg->flags |= CAN_MSG_FLAG_RTR;
    }
    if (rx_header.b.identifier_type == HAL_FDCAN_ID_EXTENDED)
    {
        msg->ident = CAN_msg_set_extended_id(rx_header.b.identifier);
    }
    else
    {
        msg->ident = CAN_msg_set_standard_id(rx_header.b.identifier);
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

/**
 * @brief Generic bus health readout (error counters and protocol status register).
 * @param[in] hfdcan FDCAN peripheral handle.
 * @param[in,out] diag Error diagnostics of this instance.
 * @param[out] status Bus health snapshot. LEC/DLEC are cleared by hardware on read.
 * @return true on success, false on parameter or HAL error.
 */
static bool stm32c5_can_get_status_generic(hal_fdcan_handle_t *hfdcan,
                                           volatile stm32c5_can_diag_t *diag,
                                           can_bus_status_t *status)
{
    if (hfdcan == NULL || diag == NULL || status == NULL)
    {
        return false;
    }

    hal_fdcan_error_counters_t counters = {0};
    hal_fdcan_protocol_status_t protocol = {0};

    if (HAL_FDCAN_GetErrorCounters(hfdcan, &counters) != HAL_OK ||
        HAL_FDCAN_GetProtocolStatus(hfdcan, &protocol) != HAL_OK)
    {
        return false;
    }

    status->tx_error_cnt = (uint8_t)counters.tx_error_cnt;
    status->rx_error_cnt = (uint8_t)counters.rx_error_cnt;
    status->error_passive = (protocol.error_status == HAL_FDCAN_ERROR_PASSIVE);
    status->bus_off = (protocol.bus_off == HAL_FDCAN_BUS_OFF_FLAG_SET);
    status->last_error = (can_error_code_t)protocol.last_error_code;
    status->data_last_error = (can_error_code_t)protocol.data_last_error_code;
    status->tdc_value = (uint8_t)protocol.tdc_value;

    /* CEL counts every TEC/REC increment and is cleared on read, so it is accumulated here */
    diag->error_log_total += counters.global_cnt;
    status->error_log_cnt = diag->error_log_total;
    for (uint32_t i = 0U; i < CAN_ERROR_TYPES; i++)
    {
        status->arb_errors[i] = diag->arb_errors[i];
        status->data_errors[i] = diag->data_errors[i];
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

static bool stm32c5_can1_transmit(const CAN_msg_t *msg)
{
    return stm32c5_can_transmit_generic(mx_fdcan1_gethandle(), &s_can1_config, msg);
}

static bool stm32c5_can1_receive(CAN_msg_t *msg)
{
    return stm32c5_can_receive_generic(mx_fdcan1_gethandle(), msg);
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

static bool stm32c5_can1_get_status(can_bus_status_t *status)
{
    return stm32c5_can_get_status_generic(mx_fdcan1_gethandle(), &s_can1_diag, status);
}

static const can_interface_t s_stm32c5_can1_interface = {
    .init = stm32c5_can1_init,
    .transmit = stm32c5_can1_transmit,
    .receive = stm32c5_can1_receive,
    .attach_rx_callback = stm32c5_can1_attach_rx_callback,
    .set_filter = stm32c5_can1_set_filter,
    .get_status = stm32c5_can1_get_status
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

static bool stm32c5_can2_transmit(const CAN_msg_t *msg)
{
    return stm32c5_can_transmit_generic(mx_fdcan2_gethandle(), &s_can2_config, msg);
}

static bool stm32c5_can2_receive(CAN_msg_t *msg)
{
    return stm32c5_can_receive_generic(mx_fdcan2_gethandle(), msg);
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

static bool stm32c5_can2_get_status(can_bus_status_t *status)
{
    return stm32c5_can_get_status_generic(mx_fdcan2_gethandle(), &s_can2_diag, status);
}

static const can_interface_t s_stm32c5_can2_interface = {
    .init = stm32c5_can2_init,
    .transmit = stm32c5_can2_transmit,
    .receive = stm32c5_can2_receive,
    .attach_rx_callback = stm32c5_can2_attach_rx_callback,
    .set_filter = stm32c5_can2_set_filter,
    .get_status = stm32c5_can2_get_status
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

/**
 * @brief Counts one protocol error code (STUFF..CRC) in a per-type histogram (saturating).
 */
static void stm32c5_count_error(volatile uint16_t *hist, hal_fdcan_protocol_error_code_t code)
{
    if (code >= HAL_FDCAN_PROTOCOL_ERROR_STUFF && code <= HAL_FDCAN_PROTOCOL_ERROR_CRC)
    {
        uint32_t idx = (uint32_t)code - (uint32_t)HAL_FDCAN_PROTOCOL_ERROR_STUFF;
        if (hist[idx] < 0xFFFFU)
        {
            hist[idx]++;
        }
    }
}

/**
 * @brief Error callback invoked from interrupt (ISR) on protocol errors (PEA/PED) of FDCAN1 or FDCAN2.
 *        LEC/DLEC are read immediately, before the next frame overwrites them.
 * @param[in] hfdcan FDCAN peripheral handle.
 */
void HAL_FDCAN_ErrorCallback(hal_fdcan_handle_t *hfdcan)
{
    volatile stm32c5_can_diag_t *diag = NULL;

    if (hfdcan == mx_fdcan1_gethandle())
    {
        diag = &s_can1_diag;
    }
    else if (hfdcan == mx_fdcan2_gethandle())
    {
        diag = &s_can2_diag;
    }

    hal_fdcan_protocol_status_t protocol = {0};
    if (diag == NULL || HAL_FDCAN_GetProtocolStatus(hfdcan, &protocol) != HAL_OK)
    {
        return;
    }

    stm32c5_count_error(diag->arb_errors, protocol.last_error_code);
    stm32c5_count_error(diag->data_errors, protocol.data_last_error_code);
}
