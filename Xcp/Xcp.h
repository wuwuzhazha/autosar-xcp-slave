#ifndef XCP_H
#define XCP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "StandardTypes.h"
#include "ComStack_Types.h"
#include "Xcp_Config.h"
#include "Xcp_Types.h"

void Xcp_Init(const Xcp_ConfigType *Xcp_ConfigPtr);

#if (XCP_VERSION_INFO_API == STD_ON)

void Xcp_GetVersionInfo(Std_VersionInfoType *versioninfo);
#endif

#if (XCP_SUPPRESS_TX_SUPPORT == STD_ON)

void Xcp_SetTransmissionMode(NetworkHandleType channel, Xcp_TransmissionModeType mode);
#endif

void Xcp_MainFunction(void);

void Xcp_Event(uint16 event_channel);

Std_ReturnType Xcp_SendEventPacket(uint8 event_code, const uint8 *event_info, uint8 info_length);

Std_ReturnType Xcp_SendServiceRequest(uint8 service_code, const uint8 *service_data, uint8 data_length);

Std_ReturnType Xcp_SendDaqOverloadEvent(uint16 daq_list_no);

Std_ReturnType Xcp_SendSessionTerminatedEvent(void);

Std_ReturnType Xcp_SendSleepEvent(void);

Std_ReturnType Xcp_SendWakeUpEvent(void);

#if (XCP_ON_ETHERNET_ENABLED == STD_ON)

boolean Xcp_SoAdConnectionClosed(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
