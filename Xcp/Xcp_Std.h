#ifndef XCP_STD_H
#define XCP_STD_H

#include "Xcp_Config.h"
#include "Xcp_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

sint16 Xcp_CmdConnect(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdDisconnect(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetStatus(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdSynch(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetCommModeInfo(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetId(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdSetMta(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdUpload(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdShortUpload(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdDownload(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdShortDownload(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdDownloadNext(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdDownloadMax(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdBuildChecksum(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdModifyBits(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdSetRequest(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdTransportLayerCmd(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

#if (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)

void Xcp_InitBlockTransfer(void);

Std_ReturnType Xcp_StartBlockTransfer(uint8 block_type, uint32 total_size, uint32 first_packet_size);

Std_ReturnType Xcp_ValidateBlockSequence(uint8 block_type, uint32 data_size);

void Xcp_CancelBlockTransfer(void);

boolean Xcp_IsBlockTransferActive(void);

uint32 Xcp_GetBlockTransferRemaining(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
