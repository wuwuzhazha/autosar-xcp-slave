#ifndef OS_H
#define OS_H

#include "StandardTypes.h"

typedef uint32 TickType;
typedef uint32 *TickRefType;

#define XCP_OS_COUNTER_ID (0U)

/* AUTOSAR OS 计数器接口 [SWS_Xcp_00768] */
Std_ReturnType GetCounterValue(uint32 counter_id, TickRefType value);
Std_ReturnType GetElapsedValue(uint32 counter_id, TickRefType value, TickRefType elapsed_value);

#endif /* OS_H */
