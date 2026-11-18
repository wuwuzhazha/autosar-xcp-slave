#ifndef XCP_DAQ_H
#define XCP_DAQ_H

#include "Xcp_Config.h"
#include "Xcp_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if (XCP_DAQ_ENABLED == STD_ON)

void Xcp_InitDaqLists(void);

void Xcp_ResetEventChannelCounters(void);

sint16 Xcp_CmdGetDaqProcessorInfo(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetDaqResolutionInfo(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetDaqEventInfo(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdFreeDaq(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdAllocDaq(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdAllocOdt(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdAllocOdtEntry(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdSetDaqPtr(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdWriteDaq(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdSetDaqListMode(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdStartStopDaqList(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdStartStopSynch(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetDaqListMode(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetDaqListInfo(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdClearDaqList(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdReadDaq(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetDaqClock(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

void Xcp_ProcessDaq(uint16 event_channel);

void Xcp_ReportDtoOverload(void);

void Xcp_ProcessStim(uint16 event_channel);

Std_ReturnType Xcp_ReceiveStimDto(const uint8 *dto_data, uint8 dto_length);

void Xcp_TransmitDto(uint16 daq_list_no, uint8 odt_no);

#if (XCP_STIM_TIMEOUT_ENABLED == STD_ON)

void Xcp_CheckStimTimeout(void);

void Xcp_ResetStimTimeout(uint16 daq_list_no);

void Xcp_InitStimTimeout(void);
#endif

void Xcp_FreeDaqLists(void);

void Xcp_RebuildActiveDaqList(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
