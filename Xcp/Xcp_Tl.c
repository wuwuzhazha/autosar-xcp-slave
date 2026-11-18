#include "Xcp_Config.h"

#if (XCP_ON_CAN_ENABLED == STD_ON)
#include "XcpOnCan.c"
#elif (XCP_ON_FLEXRAY_ENABLED == STD_ON)
#include "XcpOnFr.c"
#elif (XCP_ON_ETHERNET_ENABLED == STD_ON)
#include "XcpOnEth.c"
#elif (XCP_ON_CDD_ENABLED == STD_ON)
#include "XcpOnCdd.c"
#endif
