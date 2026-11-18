#ifndef SCHM_XCP_H
#define SCHM_XCP_H
#include "StandardTypes.h"
#define XCP_EXCLUSIVE_AREA_0 (0U)
extern void SchM_Enter_Xcp(uint8 exclusive_area);
extern void SchM_Exit_Xcp(uint8 exclusive_area);
#endif
