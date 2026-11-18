#ifndef XCP_RESUME_H
#define XCP_RESUME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "Xcp_Config.h"
#include "Xcp_Types.h"

#if (XCP_RESUME_MODE_ENABLED == STD_ON)

#define XCP_RESUME_MAGIC_NUMBER         (0x58435052U)

#define XCP_RESUME_DATA_VERSION         (0x0300U)

typedef enum
{
    XCP_RESUME_STATE_IDLE = 0x00U,
    XCP_RESUME_STATE_PENDING,
    XCP_RESUME_STATE_ACTIVE,
    XCP_RESUME_STATE_ERROR
} Xcp_ResumeStateType;

typedef struct
{
    uint16 event_channel;
    uint8  odt_count;
    uint8  mode;
    uint8  direction;
    uint8  prescaler;
    uint8  priority;
    uint8  resume_enabled;
} Xcp_DaqResumeConfigType;

typedef struct
{
    uint32 address;
    uint8  address_extension;
    uint8  size;
    uint8  bit_offset;
} Xcp_OdtEntryResumeConfigType;

typedef struct
{
    uint32 magic_number;
    uint16 version;
    uint16 daq_count;
    uint16 checksum;
    uint8  resume_active;
    uint8  reserved;
    uint16 session_cfg_id;

    Xcp_DaqResumeConfigType daq_config[XCP_MAX_DAQ];

    Xcp_OdtEntryResumeConfigType odt_entries[XCP_MAX_DAQ][XCP_MAX_ODT][XCP_MAX_ODT_ENTRIES];
    uint8 odt_entry_counts[XCP_MAX_DAQ][XCP_MAX_ODT];
} Xcp_ResumeDataType;

typedef struct
{
    Xcp_ResumeStateType state;
    boolean             data_valid;
    boolean             store_pending;
    boolean             restore_pending;
} Xcp_ResumeRuntimeType;

extern Xcp_ResumeRuntimeType g_xcp_resume_runtime;

extern Xcp_ResumeDataType g_xcp_resume_data;

void Xcp_InitResumeMode(void);

Std_ReturnType Xcp_StoreDaqConfig(Xcp_Channel *ch, uint16 session_cfg_id, boolean resume);

Std_ReturnType Xcp_RestoreDaqConfig(void);

Std_ReturnType Xcp_ClearStoredDaqConfig(Xcp_Channel *ch);

boolean Xcp_IsDaqListResume(Xcp_Channel *ch, uint16 daq_list_no);

boolean Xcp_IsResumeModeActive(Xcp_Channel *ch);

void Xcp_ResumeMainFunction(void);

void Xcp_NvMReadRamBlockFromNvmCallback(uint16 block_id, uint8 result);

void Xcp_NvMWriteRamBlockToNvmCallback(uint16 block_id, uint8 result);

#endif

#ifdef __cplusplus
}
#endif

#endif
