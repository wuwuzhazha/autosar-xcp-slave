#ifndef XCP_CALLBACK_H
#define XCP_CALLBACK_H

#include "Xcp_Config.h"
#include "ComStack_Types.h"
#include "StandardTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#if (XCP_ON_CAN_ENABLED == STD_ON)

extern void Xcp_CanIfRxIndication(PduIdType rx_pdu_id,
                                   const PduInfoType* pdu_info_ptr);

extern void Xcp_CanIfTxConfirmation(PduIdType tx_pdu_id,
                                     Std_ReturnType result);

extern Std_ReturnType Xcp_CanIfTriggerTransmit(PduIdType tx_pdu_id,
                                                 PduInfoType* pdu_info_ptr);
#endif

#if (XCP_ON_ETHERNET_ENABLED == STD_ON)

extern void Xcp_SoAdIfRxIndication(PduIdType rx_pdu_id,
                                    const PduInfoType* pdu_info_ptr);

extern void Xcp_SoAdIfTxConfirmation(PduIdType tx_pdu_id,
                                      Std_ReturnType result);

extern Std_ReturnType Xcp_SoAdIfTriggerTransmit(PduIdType tx_pdu_id,
                                                PduInfoType* pdu_info_ptr);

extern uint16 Xcp_EthPackMessage(const uint8 *msg, uint8 msg_len, uint8 *frame, uint16 frame_capacity);
#endif

#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)

extern void Xcp_FrIfRxIndication(PduIdType rx_pdu_id,
                                  const PduInfoType* pdu_info_ptr);

extern void Xcp_FrIfTxConfirmation(PduIdType tx_pdu_id,
                                    Std_ReturnType result);

extern Std_ReturnType Xcp_FrIfTriggerTransmit(PduIdType tx_pdu_id,
                                                PduInfoType* pdu_info_ptr);
#endif

#if (XCP_ON_CDD_ENABLED == STD_ON)

extern void XCP_CDD_RX_INDICATION_NAME(PduIdType rx_pdu_id,
                                       const PduInfoType* pdu_info_ptr);

extern void XCP_CDD_TX_CONFIRMATION_NAME(PduIdType tx_pdu_id,
                                         Std_ReturnType result);

extern Std_ReturnType XCP_CDD_TRIGGER_TRANSMIT_NAME(PduIdType tx_pdu_id,
                                                    PduInfoType* pdu_info_ptr);
#endif

extern Std_ReturnType Xcp_CalcChecksum_Callout(uint32 address, uint32 length, uint8* checksum_type, uint32* checksum);

extern Std_ReturnType Xcp_BuildChecksum_Callout(uint32 addr, uint32 len, uint32* checksum, uint8* checksumLen);

extern Std_ReturnType Xcp_UserCommand_Callout(const uint8* cmd_data, uint8 cmd_length);

#if (XCP_SEED_KEY_ENABLED == STD_ON)

extern void Xcp_GetSeed_Callout(uint8 resource, uint8 *seed, uint8 *seed_length);

extern boolean Xcp_VerifyKey_Callout(uint8 resource, const uint8 *key, uint8 key_length);
#endif

#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)

extern Std_ReturnType Xcp_CopyCalPage_Callout(uint8 src_segment, uint8 src_page, uint8 dst_segment, uint8 dst_page);
#endif

#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)

extern Std_ReturnType Xcp_FlashErase_Callout(uint32 address, uint32 length);

extern Std_ReturnType Xcp_FlashProgram_Callout(uint32 address, const uint8 *data, uint32 length);

extern Std_ReturnType Xcp_FlashVerify_Callout(uint32 address, uint32 length, uint32 expected_value);

extern void Xcp_EcuReset_Callout(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
