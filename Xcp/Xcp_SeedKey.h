#ifndef XCP_SEEDKEY_H
#define XCP_SEEDKEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Xcp_Config.h"
#include "Xcp_Types.h"

#if (XCP_SEED_KEY_ENABLED == STD_ON)

sint16 Xcp_CmdGetSeed(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdUnlock(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

boolean Xcp_IsResourceUnlocked(uint8 resource);

void Xcp_InitProtection(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
