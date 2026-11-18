#include <string.h>
#include "Xcp_Internal.h"
#include "Xcp_Types.h"

/* CAN：报文即 XCP 帧，无链路头。 */

#if (XCP_ON_CAN_ENABLED == STD_ON)
#include "CanIf.h"

void Xcp_CanIfRxIndication(PduIdType rx_pdu_id, const PduInfoType *pdu_info_ptr)
{
    XCP_TL_CHECK_INIT(XCP_SID_RX_INDICATION)
    XCP_TL_CHECK_RX_PARAMS(XCP_SID_RX_INDICATION, pdu_info_ptr,
                         rx_pdu_id, g_xcp_manager.config_ptr->xcp_rx_pdu_id)
    (void)rx_pdu_id;
    Xcp_HandleRxIndication(pdu_info_ptr);
}

void Xcp_CanIfTxConfirmation(PduIdType tx_pdu_id, Std_ReturnType result)
{
    XCP_TL_CHECK_INIT(XCP_SID_TX_CONFIRMATION)
    (void)tx_pdu_id;
    Xcp_HandleTxConfirmation(result);
}

Std_ReturnType Xcp_CanIfTriggerTransmit(PduIdType tx_pdu_id, PduInfoType *pdu_info_ptr)
{
    XCP_TL_CHECK_INIT_R(XCP_SID_TRIGGER_TRANSMIT)
    XCP_TL_CHECK_TT_PARAMS(XCP_SID_TRIGGER_TRANSMIT, pdu_info_ptr)
    (void)tx_pdu_id;
    return Xcp_HandleTriggerTransmit(pdu_info_ptr);
}

Std_ReturnType Xcp_Tl_SendCto(const uint8 *data, uint8 length)
{
    PduInfoType pdu_info;
    pdu_info.sdu_data_ptr  = (uint8 *)data;
    pdu_info.sdu_length    = length;
    pdu_info.meta_data_ptr = NULL_PTR;
    return CanIf_Transmit(g_xcp_manager.config_ptr->xcp_tx_pdu_id, &pdu_info);
}

Std_ReturnType Xcp_Tl_SendDto(const uint8 *data, uint8 length)
{
    return Xcp_Tl_SendCto(data, length);
}
#endif
