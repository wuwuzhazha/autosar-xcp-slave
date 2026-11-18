#ifndef XCP_FLASH_PROG_H
#define XCP_FLASH_PROG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Xcp_Config.h"
#include "Xcp_Types.h"

#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)

#define XCP_PGM_PROP_ABSOLUTE_MODE          (0x01U)
#define XCP_PGM_PROP_FUNCTIONAL_MODE        (0x02U)
#define XCP_PGM_PROP_COMPRESSION_SUPPORTED  (0x04U)
#define XCP_PGM_PROP_COMPRESSION_REQUIRED   (0x08U)
#define XCP_PGM_PROP_ENCRYPTION_SUPPORTED   (0x10U)
#define XCP_PGM_PROP_ENCRYPTION_REQUIRED    (0x20U)
#define XCP_PGM_PROP_NON_SEQ_PGM_SUPPORTED  (0x40U)
#define XCP_PGM_PROP_NON_SEQ_PGM_REQUIRED   (0x80U)

#define XCP_PGM_STATUS_IDLE        (0x00U)
#define XCP_PGM_STATUS_STARTED     (0x01U)
#define XCP_PGM_STATUS_CLEARING    (0x02U)
#define XCP_PGM_STATUS_PROGRAMMING (0x03U)
#define XCP_PGM_STATUS_VERIFYING   (0x04U)

sint16 Xcp_CmdProgramStart(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdProgramClear(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdProgram(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdProgramReset(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetPgmProcessorInfo(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetSectorInfo(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdProgramPrepare(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdProgramFormat(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdProgramNext(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdProgramMax(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

sint16 Xcp_CmdProgramVerify(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);

void Xcp_InitFlashProg(void);

const Xcp_FlashSectorType *Xcp_GetSectorConfig(uint8 sector_number);

boolean Xcp_IsProgrammingActive(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
