#ifndef CANIF_H
#define CANIF_H

#include "ComStack_Types.h"

Std_ReturnType CanIf_Transmit(PduIdType tx_pdu_id, const PduInfoType *pdu_info_ptr);

#endif
