#include <string.h>
#include "Xcp_Internal.h"
#include "Xcp_Types.h"
#include "Xcp_Callback.h"
#include "Xcp_Daq.h"

#if (XCP_ON_ETHERNET_ENABLED == STD_ON)
#include "SoAd.h"

#define XCP_ETH_HEADER_LEN (4U)
#define XCP_ETH_MAX_FRAME  (1518U)

void Xcp_SoAdIfRxIndication(PduIdType rx_pdu_id, const PduInfoType *pdu_info_ptr)
{
    XCP_TL_CHECK_INIT(XCP_SID_RX_INDICATION)
    XCP_TL_CHECK_RX_PARAMS(XCP_SID_RX_INDICATION, pdu_info_ptr,
                         rx_pdu_id, g_xcp_channel.config_ptr->xcp_rx_pdu_id)
    (void)rx_pdu_id;

#if (XCP_ETH_SEGMENTED_RX == STD_ON)
    {
        const uint8 *frame = pdu_info_ptr->sdu_data_ptr;
        uint16 frame_len;
        uint16 pos = 0U;

        if (frame == NULL_PTR)
        {
            return;
        }
        frame_len = (uint16) pdu_info_ptr->sdu_length;

        while (pos + XCP_ETH_HEADER_LEN <= frame_len)
        {
            uint16 msg_len = (uint16) frame[pos] | ((uint16) frame[pos + 1U] << 8U);
            PduInfoType seg;

            if (msg_len == 0U)
            {
                break;
            }
            if (msg_len > (uint16) (frame_len - pos - XCP_ETH_HEADER_LEN))
            {
                return;
            }

            seg.sdu_data_ptr  = (uint8 *) &frame[pos + XCP_ETH_HEADER_LEN];
            seg.sdu_length    = msg_len;
            seg.meta_data_ptr = pdu_info_ptr->meta_data_ptr;
            Xcp_HandleRxIndication(&seg);
            pos += (uint16) (XCP_ETH_HEADER_LEN + msg_len);
        }
        return;
    }
#else
    Xcp_HandleRxIndication(pdu_info_ptr);
#endif
}

void Xcp_SoAdIfTxConfirmation(PduIdType tx_pdu_id, Std_ReturnType result)
{
    XCP_TL_CHECK_INIT(XCP_SID_TX_CONFIRMATION)
    (void)tx_pdu_id;
    Xcp_HandleTxConfirmation(result);
}

Std_ReturnType Xcp_SoAdIfTriggerTransmit(PduIdType tx_pdu_id, PduInfoType *pdu_info_ptr)
{
    XCP_TL_CHECK_INIT_R(XCP_SID_TRIGGER_TRANSMIT)
    XCP_TL_CHECK_TT_PARAMS(XCP_SID_TRIGGER_TRANSMIT, pdu_info_ptr)
    (void)tx_pdu_id;
    return Xcp_HandleTriggerTransmit(pdu_info_ptr);
}

uint16 Xcp_EthPackMessage(const uint8 *msg, uint8 msg_len, uint8 *frame, uint16 frame_capacity)
{
    static uint16 eth_counter = 0U;
    uint16 block;

    if ((msg == NULL_PTR) || (frame == NULL_PTR) || (msg_len == 0U))
    {
        return 0U;
    }

    block = (uint16) (XCP_ETH_HEADER_LEN + msg_len);
    if (block > frame_capacity)
    {
        return 0U;
    }

    frame[0] = (uint8) (msg_len & 0xFFU);
    frame[1] = (uint8) ((msg_len >> 8U) & 0xFFU);
    frame[2] = (uint8) (eth_counter & 0xFFU);
    frame[3] = (uint8) ((eth_counter >> 8U) & 0xFFU);
    eth_counter++;
    (void) memcpy(&frame[XCP_ETH_HEADER_LEN], msg, msg_len);
    return block;
}

static Std_ReturnType Xcp_EthSend(const uint8 *data, uint8 length)
{
    uint8 frame[XCP_ETH_MAX_FRAME];
    PduInfoType pdu_info;
    uint16 used = Xcp_EthPackMessage(data, length, frame, (uint16) sizeof(frame));

    if (used == 0U)
    {
        return E_NOT_OK;
    }

    pdu_info.sdu_data_ptr = frame;
    pdu_info.sdu_length   = used;
    pdu_info.meta_data_ptr = NULL_PTR;
    return SoAd_IfTransmit(g_xcp_channel.config_ptr->xcp_tx_pdu_id, &pdu_info);
}

boolean Xcp_SoAdConnectionClosed(void)
{
    if ((g_xcp_channel.runtime_ptr == NULL_PTR)
        || (g_xcp_channel.runtime_ptr->connection_state != XCP_CONNECTED))
    {
        return FALSE;
    }
    Xcp_SessionClose();
    return TRUE;
}

Std_ReturnType Xcp_Tl_SendCto(const uint8 *data, uint8 length)
{
    return Xcp_EthSend(data, length);
}

Std_ReturnType Xcp_Tl_SendDto(const uint8 *data, uint8 length)
{
    return Xcp_Tl_SendCto(data, length);
}
#endif
