#include <string.h>
#include "Xcp_Internal.h"
#include "Xcp_Types.h"
#include "Xcp_Callback.h"

/* CDD：报文即 XCP 帧，无链路头。接口名由生成宏 XCP_CDD_*_NAME 展开。 */

#if (XCP_ON_CDD_ENABLED == STD_ON)
#include "Cdd.h"

void XCP_CDD_RX_INDICATION_NAME(PduIdType rx_pdu_id, const PduInfoType *pdu_info_ptr)
{
    XCP_TL_CHECK_INIT(XCP_SID_RX_INDICATION)
    XCP_TL_CHECK_RX_PARAMS(XCP_SID_RX_INDICATION, pdu_info_ptr,
                         rx_pdu_id, g_xcp_channel.config_ptr->xcp_rx_pdu_id)
    (void)rx_pdu_id;
    Xcp_HandleRxIndication(pdu_info_ptr);
}

void XCP_CDD_TX_CONFIRMATION_NAME(PduIdType tx_pdu_id, Std_ReturnType result)
{
    XCP_TL_CHECK_INIT(XCP_SID_TX_CONFIRMATION)
    (void)tx_pdu_id;
    Xcp_HandleTxConfirmation(result);
}

Std_ReturnType XCP_CDD_TRIGGER_TRANSMIT_NAME(PduIdType tx_pdu_id, PduInfoType *pdu_info_ptr)
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
    return XCP_CDD_TRANSMIT_NAME(g_xcp_channel.config_ptr->xcp_tx_pdu_id, &pdu_info);
}

Std_ReturnType Xcp_Tl_SendDto(const uint8 *data, uint8 length)
{
    return Xcp_Tl_SendCto(data, length);
}
#endif
