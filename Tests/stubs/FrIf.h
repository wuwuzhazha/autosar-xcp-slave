#ifndef FRIF_H
#define FRIF_H

#include "ComStack_Types.h"

Std_ReturnType FrIf_Transmit(PduIdType tx_pdu_id, const PduInfoType *pdu_info_ptr);
Std_ReturnType FrIf_DisableLPdu(uint16 lpdu_handle);
Std_ReturnType FrIf_ReconfigLPdu(uint16 lpdu_handle, uint16 frame_id, uint8 chn_idx,
                                 uint8 cycle_rep, uint16 offset, uint16 len, uint16 cfg_idx);

#endif
