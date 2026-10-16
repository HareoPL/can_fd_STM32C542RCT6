/**
 * @file isotp.c
 * @brief Implementation of ISO 15765-2 (ISO-TP) transport layer.
 */

#include "isotp.h"
#include <string.h>

void isotp_init(isotp_link_t *link,
                uint32_t rx_can_id,
                uint32_t tx_can_id,
                isotp_can_tx_fn tx_fn,
                isotp_rx_pdu_fn rx_cb,
                void *user_ctx)
{
    if (link == NULL)
    {
        return;
    }

    memset(link, 0, sizeof(isotp_link_t));
    link->rx_can_id = rx_can_id;
    link->tx_can_id = tx_can_id;
    link->tx_fn     = tx_fn;
    link->rx_cb     = rx_cb;
    link->user_ctx  = user_ctx;
    link->rx_state  = ISOTP_RX_STATE_IDLE;
    link->tx_state  = ISOTP_TX_STATE_IDLE;
}

void isotp_reset_rx(isotp_link_t *link)
{
    if (link == NULL)
    {
        return;
    }

    link->rx_state = ISOTP_RX_STATE_IDLE;
    link->rx_total_len = 0U;
    link->rx_received_len = 0U;
    link->rx_sn = 0U;
}

void isotp_on_can_frame(isotp_link_t *link, uint32_t can_id, const uint8_t *data, uint8_t len)
{
    if (link == NULL || data == NULL || len == 0U)
    {
        return;
    }

    if (can_id != link->rx_can_id)
    {
        return;
    }

    uint8_t pci_type = data[0] & 0xF0U;

    switch (pci_type)
    {
        case ISOTP_PCI_TYPE_SINGLE:
        {
            uint8_t sf_dl = data[0] & 0x0FU;
            if (sf_dl == 0U || sf_dl > 7U || sf_dl > (len - 1U))
            {
                return;
            }

            isotp_reset_rx(link);

            if (link->rx_cb != NULL)
            {
                link->rx_cb(&data[1], sf_dl, link->user_ctx);
            }
            break;
        }

        case ISOTP_PCI_TYPE_FIRST:
        {
            if (len < 8U)
            {
                return;
            }

            uint16_t ff_dl = ((uint16_t)(data[0] & 0x0FU) << 8) | data[1];

            /* If payload exceeds our reassembly buffer, reject with Overflow */
            if (ff_dl > ISOTP_MAX_BUF_SIZE)
            {
                uint8_t fc_overflow[8] = {0x32U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
                if (link->tx_fn != NULL)
                {
                    link->tx_fn(link->tx_can_id, fc_overflow, sizeof(fc_overflow), link->user_ctx);
                }
                isotp_reset_rx(link);
                return;
            }

            link->rx_total_len = ff_dl;
            memcpy(link->rx_buf, &data[2], 6U);
            link->rx_received_len = 6U;
            link->rx_sn = 1U;
            link->rx_state = ISOTP_RX_STATE_RECEIVING_CF;

            /* Send Flow Control: Clear To Send (FS=0), BlockSize=0 (no limit), STmin=1ms */
            uint8_t fc_frame[8] = {0x30U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
            if (link->tx_fn != NULL)
            {
                link->tx_fn(link->tx_can_id, fc_frame, sizeof(fc_frame), link->user_ctx);
            }
            break;
        }

        case ISOTP_PCI_TYPE_CONSECUTIVE:
        {
            if (link->rx_state != ISOTP_RX_STATE_RECEIVING_CF)
            {
                return;
            }

            uint8_t sn = data[0] & 0x0FU;
            if (sn != link->rx_sn)
            {
                /* Sequence error: discard incomplete frame */
                isotp_reset_rx(link);
                return;
            }

            uint16_t remaining = link->rx_total_len - link->rx_received_len;
            uint8_t chunk_len = (remaining > 7U) ? 7U : (uint8_t)remaining;
            if ((len - 1U) < chunk_len)
            {
                chunk_len = len - 1U;
            }

            memcpy(&link->rx_buf[link->rx_received_len], &data[1], chunk_len);
            link->rx_received_len += chunk_len;
            link->rx_sn = (link->rx_sn + 1U) & 0x0FU;

            if (link->rx_received_len >= link->rx_total_len)
            {
                uint16_t total_len = link->rx_total_len;
                link->rx_state = ISOTP_RX_STATE_IDLE;

                if (link->rx_cb != NULL)
                {
                    link->rx_cb(link->rx_buf, total_len, link->user_ctx);
                }
            }
            break;
        }

        case ISOTP_PCI_TYPE_FLOW_CONTROL:
        {
            if (link->tx_state == ISOTP_TX_STATE_WAIT_FC)
            {
                uint8_t fs = data[0] & 0x0FU;
                if (fs == ISOTP_FC_FS_CTS)
                {
                    link->tx_block_size = data[1];
                    link->tx_st_min_ms  = data[2];
                    link->tx_state = ISOTP_TX_STATE_SENDING_CF;

                    /* Send remaining consecutive frames */
                    while (link->tx_sent_len < link->tx_total_len)
                    {
                        uint16_t rem = link->tx_total_len - link->tx_sent_len;
                        uint8_t c_len = (rem > 7U) ? 7U : (uint8_t)rem;

                        uint8_t cf_frame[8] = {0};
                        cf_frame[0] = ISOTP_PCI_TYPE_CONSECUTIVE | (link->tx_sn & 0x0FU);
                        memcpy(&cf_frame[1], &link->tx_buf[link->tx_sent_len], c_len);

                        if (link->tx_fn != NULL)
                        {
                            link->tx_fn(link->tx_can_id, cf_frame, sizeof(cf_frame), link->user_ctx);
                        }

                        link->tx_sent_len += c_len;
                        link->tx_sn = (link->tx_sn + 1U) & 0x0FU;
                    }

                    link->tx_state = ISOTP_TX_STATE_IDLE;
                }
                else
                {
                    /* Wait or Overflow: abort TX */
                    link->tx_state = ISOTP_TX_STATE_IDLE;
                }
            }
            break;
        }

        default:
            break;
    }
}

bool isotp_send_pdu(isotp_link_t *link, const uint8_t *payload, uint16_t length)
{
    if (link == NULL || payload == NULL || length == 0U || length > ISOTP_MAX_BUF_SIZE)
    {
        return false;
    }

    if (link->tx_fn == NULL)
    {
        return false;
    }

    if (length <= 7U)
    {
        /* Single Frame */
        uint8_t sf_frame[8] = {0};
        sf_frame[0] = (uint8_t)length;
        memcpy(&sf_frame[1], payload, length);

        return link->tx_fn(link->tx_can_id, sf_frame, sizeof(sf_frame), link->user_ctx);
    }
    else
    {
        /* Multi-frame transmission: First Frame */
        memcpy(link->tx_buf, payload, length);
        link->tx_total_len = length;
        link->tx_sent_len  = 6U;
        link->tx_sn        = 1U;
        link->tx_state     = ISOTP_TX_STATE_WAIT_FC;

        uint8_t ff_frame[8] = {0};
        ff_frame[0] = ISOTP_PCI_TYPE_FIRST | (uint8_t)((length >> 8) & 0x0FU);
        ff_frame[1] = (uint8_t)(length & 0xFFU);
        memcpy(&ff_frame[2], payload, 6U);

        return link->tx_fn(link->tx_can_id, ff_frame, sizeof(ff_frame), link->user_ctx);
    }
}
