#ifndef XCP_DAQ_H
#define XCP_DAQ_H

#include "Xcp_Config.h"
#include "Xcp_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if (XCP_DAQ_ENABLED == STD_ON)

void Xcp_InitDaqLists(Xcp_Channel *ch);

void Xcp_ResetEventChannelCounters(Xcp_Channel *ch);

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

void Xcp_ProcessDaq(Xcp_Channel *ch, uint16 event_channel);

void Xcp_ReportDtoOverload(Xcp_Channel *ch);

Std_ReturnType Xcp_ReceiveStimDto(Xcp_Channel *ch, const uint8 *dto_data, uint8 dto_length);

void Xcp_TransmitDto(Xcp_Channel *ch, uint16 daq_list_no, uint8 odt_no);

#if (XCP_STIM_TIMEOUT_ENABLED == STD_ON)

void Xcp_CheckStimTimeout(Xcp_Channel *ch);

void Xcp_ResetStimTimeout(Xcp_Channel *ch, uint16 daq_list_no);

void Xcp_InitStimTimeout(Xcp_Channel *ch);
#endif

void Xcp_FreeDaqLists(Xcp_Channel *ch);

void Xcp_RebuildActiveDaqList(Xcp_Channel *ch);

#endif

#ifdef __cplusplus
}
#endif

#endif
