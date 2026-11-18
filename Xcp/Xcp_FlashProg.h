#ifndef XCP_FLASH_PROG_H
#define XCP_FLASH_PROG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Xcp_Config.h"
#include "Xcp_Types.h"

#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)

/* GET_PGM_PROCESSOR_INFO: PGM_PROPERTIES（ASAM 位号） */
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

void Xcp_CmdProgramStart(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_CmdProgramClear(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_CmdProgram(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_CmdProgramReset(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_CmdGetPgmProcessorInfo(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_CmdGetSectorInfo(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_CmdProgramPrepare(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_CmdProgramFormat(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_CmdProgramNext(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_CmdProgramMax(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_CmdProgramVerify(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_InitFlashProg(void);

const Xcp_FlashSectorType *Xcp_GetSectorConfig(uint8 sector_number);

boolean Xcp_IsProgrammingActive(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
