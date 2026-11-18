#ifndef XCP_STD_H
#define XCP_STD_H

#include "Xcp_Config.h"
#include "Xcp_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

sint16 Xcp_CmdConnect(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdDisconnect(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetStatus(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdSynch(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetCommModeInfo(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetId(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdSetMta(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdUpload(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdShortUpload(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdDownload(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdShortDownload(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdDownloadNext(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdDownloadMax(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdBuildChecksum(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdModifyBits(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdSetRequest(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdTransportLayerCmd(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

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
