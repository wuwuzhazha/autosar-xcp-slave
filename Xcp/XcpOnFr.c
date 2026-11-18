#include <string.h>
#include "Xcp_Internal.h"
#include "Xcp_Types.h"
#include "Xcp_Callback.h"

/* FlexRay：LEN(2) | CTR(1) | MSG | FILL，串联报文按头长度逐段入队。 */

#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)
#include "FrIf.h"

#define XCP_FR_HEADER_LEN (3U)
#define XCP_FR_MAX_FRAME  (264U)

void Xcp_FrIfRxIndication(PduIdType rx_pdu_id, const PduInfoType *pdu_info_ptr)
{
    XCP_TL_CHECK_INIT(XCP_SID_RX_INDICATION)
    XCP_TL_CHECK_RX_PARAMS(XCP_SID_RX_INDICATION, pdu_info_ptr,
                         rx_pdu_id, g_xcp_channel.config_ptr->xcp_rx_pdu_id)
    (void)rx_pdu_id;

#if (XCP_FR_CONCATENATED_FRAMES == STD_ON)
    {
        const uint8 *frame = pdu_info_ptr->sdu_data_ptr;
        uint16 frame_len;
        uint16 pos = 0U;

        if (frame == NULL_PTR)
        {
            return;
        }
        frame_len = (uint16) pdu_info_ptr->sdu_length;

        while (pos + XCP_FR_HEADER_LEN <= frame_len)
        {
            uint16 msg_len = (uint16) frame[pos] | ((uint16) frame[pos + 1U] << 8U);
            PduInfoType seg;

            if (msg_len == 0U)
            {
                break;
            }
            if (msg_len > (uint16) (frame_len - pos - XCP_FR_HEADER_LEN))
            {
                return;
            }

            seg.sdu_data_ptr  = (uint8 *) &frame[pos + XCP_FR_HEADER_LEN];
            seg.sdu_length    = msg_len;
            seg.meta_data_ptr = pdu_info_ptr->meta_data_ptr;
            Xcp_HandleRxIndication(&seg);
            pos += (uint16) (XCP_FR_HEADER_LEN + msg_len);
            pos += (uint16) ((XCP_FR_HEADER_LEN + msg_len) & 1U);
        }
        return;
    }
#else
    Xcp_HandleRxIndication(pdu_info_ptr);
#endif
}

void Xcp_FrIfTxConfirmation(PduIdType tx_pdu_id, Std_ReturnType result)
{
    XCP_TL_CHECK_INIT(XCP_SID_TX_CONFIRMATION)
    (void)tx_pdu_id;
    Xcp_HandleTxConfirmation(result);
}

Std_ReturnType Xcp_FrIfTriggerTransmit(PduIdType tx_pdu_id, PduInfoType *pdu_info_ptr)
{
    XCP_TL_CHECK_INIT_R(XCP_SID_TRIGGER_TRANSMIT)
    XCP_TL_CHECK_TT_PARAMS(XCP_SID_TRIGGER_TRANSMIT, pdu_info_ptr)
    (void)tx_pdu_id;
    return Xcp_HandleTriggerTransmit(pdu_info_ptr);
}

uint16 Xcp_FrPackMessage(const uint8 *msg, uint8 msg_len, uint8 *frame, uint16 frame_capacity)
{
    uint16 block;
    static uint8 fr_counter = 0U;

    if ((msg == NULL_PTR) || (frame == NULL_PTR) || (msg_len == 0U))
    {
        return 0U;
    }

    block = (uint16) (XCP_FR_HEADER_LEN + msg_len);
    block += (uint16) (block & 1U);

    if (block > frame_capacity)
    {
        return 0U;
    }

    frame[0] = (uint8) (msg_len & 0xFFU);
    frame[1] = (uint8) ((msg_len >> 8U) & 0xFFU);
    frame[2] = fr_counter++;
    (void) memcpy(&frame[XCP_FR_HEADER_LEN], msg, msg_len);

    if ((block - XCP_FR_HEADER_LEN - msg_len) == 1U)
    {
        frame[block - 1U] = 0U;
    }

    return block;
}

Std_ReturnType Xcp_FrConfigureLPdu(void)
{
    const Xcp_ConfigType *cfg = g_xcp_channel.config_ptr;

    if (cfg == NULL_PTR)
    {
        return E_NOT_OK;
    }

    return FrIf_ReconfigLPdu(cfg->xcp_fr_lpdu_handle, cfg->xcp_fr_frame_id,
                             cfg->xcp_fr_channel, cfg->xcp_fr_cycle_rep,
                             cfg->xcp_fr_offset, (uint8) XCP_MAX_DTO, cfg->xcp_fr_config_idx);
}

static Std_ReturnType Xcp_FrSend(const uint8 *data, uint8 length)
{
    uint8 frame[XCP_FR_MAX_FRAME];
    PduInfoType pdu_info;
    uint16 used = Xcp_FrPackMessage(data, length, frame, (uint16) sizeof(frame));

    if (used == 0U)
    {
        return E_NOT_OK;
    }

    pdu_info.sdu_data_ptr = frame;
    pdu_info.sdu_length   = used;
    pdu_info.meta_data_ptr = NULL_PTR;
    return FrIf_Transmit(g_xcp_channel.config_ptr->xcp_tx_pdu_id, &pdu_info);
}

Std_ReturnType Xcp_Tl_SendCto(const uint8 *data, uint8 length)
{
    return Xcp_FrSend(data, length);
}

Std_ReturnType Xcp_Tl_SendDto(const uint8 *data, uint8 length)
{
    return Xcp_Tl_SendCto(data, length);
}
#endif
