#ifndef XCP_SEEDKEY_H
#define XCP_SEEDKEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Xcp_Config.h"
#include "Xcp_Types.h"

#if (XCP_SEED_KEY_ENABLED == STD_ON)

void Xcp_CmdGetSeed(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_CmdUnlock(const uint8 *cmd_data, uint8 cmd_length);

boolean Xcp_IsResourceUnlocked(uint8 resource);

void Xcp_InitProtection(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
