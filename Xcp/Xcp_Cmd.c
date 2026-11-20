#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Std.h"
#include "Xcp_Daq.h"
#include "Xcp_SeedKey.h"
#include "Xcp_Callback.h"
#include "Xcp_CalPage.h"
#include "Xcp_FlashProg.h"

static sint16 Xcp_DispatchUserCmd(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res);


static const Xcp_CmdEntryType XCP_CMD_TABLE[] = {
    { XCP_CMD_CONNECT,              2U, 0U,                    XCP_GUARD_ALLOW_DISCONNECTED, Xcp_CmdConnect },
    { XCP_CMD_DISCONNECT,           1U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdDisconnect },
    { XCP_CMD_GET_STATUS,           1U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdGetStatus },
    { XCP_CMD_SYNCH,                1U, 0U,                    XCP_GUARD_ALLOW_DISCONNECTED, Xcp_CmdSynch },
    { XCP_CMD_GET_COMM_MODE_INFO,   1U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdGetCommModeInfo },
    { XCP_CMD_GET_ID,               2U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdGetId },
    { XCP_CMD_SET_MTA,              8U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdSetMta },
    { XCP_CMD_UPLOAD,               2U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdUpload },
    { XCP_CMD_SHORT_UPLOAD,         8U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdShortUpload },
    { XCP_CMD_SET_REQUEST,          4U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdSetRequest },
    { XCP_CMD_BUILD_CHECKSUM,       8U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdBuildChecksum },
    { XCP_CMD_TRANSPORT_LAYER_CMD,  2U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdTransportLayerCmd },
    { XCP_CMD_USER_CMD,             1U, 0U,                    XCP_GUARD_NONE,               Xcp_DispatchUserCmd },
    { XCP_CMD_DOWNLOAD,             2U, XCP_RESOURCE_CAL_PAG,  XCP_GUARD_NONE,               Xcp_CmdDownload },
    { XCP_CMD_DOWNLOAD_NEXT,        2U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdDownloadNext },
    { XCP_CMD_DOWNLOAD_MAX,         1U, XCP_RESOURCE_CAL_PAG,  XCP_GUARD_NONE,               Xcp_CmdDownloadMax },
    { XCP_CMD_SHORT_DOWNLOAD,       8U, XCP_RESOURCE_CAL_PAG,  XCP_GUARD_NONE,               Xcp_CmdShortDownload },
    { XCP_CMD_MODIFY_BITS,          6U, XCP_RESOURCE_CAL_PAG,  XCP_GUARD_NONE,               Xcp_CmdModifyBits },
#if (XCP_DAQ_ENABLED == STD_ON)
    { XCP_CMD_GET_DAQ_PROCESSOR_INFO,  1U, 0U,                 XCP_GUARD_NONE,               Xcp_CmdGetDaqProcessorInfo },
    { XCP_CMD_GET_DAQ_RESOLUTION_INFO, 1U, 0U,                 XCP_GUARD_NONE,               Xcp_CmdGetDaqResolutionInfo },
    { XCP_CMD_GET_DAQ_EVENT_INFO,      4U, 0U,                 XCP_GUARD_NONE,               Xcp_CmdGetDaqEventInfo },
    { XCP_CMD_FREE_DAQ,                1U, XCP_RESOURCE_DAQ,   XCP_GUARD_NONE,               Xcp_CmdFreeDaq },
    { XCP_CMD_ALLOC_DAQ,               4U, XCP_RESOURCE_DAQ,   XCP_GUARD_NONE,               Xcp_CmdAllocDaq },
    { XCP_CMD_ALLOC_ODT,               5U, XCP_RESOURCE_DAQ,   XCP_GUARD_NONE,               Xcp_CmdAllocOdt },
    { XCP_CMD_ALLOC_ODT_ENTRY,         6U, XCP_RESOURCE_DAQ,   XCP_GUARD_NONE,               Xcp_CmdAllocOdtEntry },
    { XCP_CMD_SET_DAQ_PTR,             6U, 0U,                 XCP_GUARD_NONE,               Xcp_CmdSetDaqPtr },
    { XCP_CMD_WRITE_DAQ,               8U, XCP_RESOURCE_DAQ,   XCP_GUARD_NONE,               Xcp_CmdWriteDaq },
    { XCP_CMD_SET_DAQ_LIST_MODE,       8U, XCP_RESOURCE_DAQ,   XCP_GUARD_NONE,               Xcp_CmdSetDaqListMode },
    { XCP_CMD_GET_DAQ_LIST_MODE,       4U, 0U,                 XCP_GUARD_NONE,               Xcp_CmdGetDaqListMode },
    { XCP_CMD_START_STOP_DAQ_LIST,     4U, XCP_RESOURCE_DAQ,   XCP_GUARD_NONE,               Xcp_CmdStartStopDaqList },
    { XCP_CMD_START_STOP_SYNCH,        2U, XCP_RESOURCE_DAQ,   XCP_GUARD_NONE,               Xcp_CmdStartStopSynch },
    { XCP_CMD_GET_DAQ_CLOCK,           1U, 0U,                 XCP_GUARD_NONE,               Xcp_CmdGetDaqClock },
    { XCP_CMD_GET_DAQ_LIST_INFO,       4U, 0U,                 XCP_GUARD_NONE,               Xcp_CmdGetDaqListInfo },
    { XCP_CMD_CLEAR_DAQ_LIST,          4U, XCP_RESOURCE_DAQ,   XCP_GUARD_NONE,               Xcp_CmdClearDaqList },
    { XCP_CMD_READ_DAQ,                1U, 0U,                 XCP_GUARD_NONE,               Xcp_CmdReadDaq },
#endif
#if (XCP_SEED_KEY_ENABLED == STD_ON)
    { XCP_CMD_GET_SEED,             3U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdGetSeed },
    { XCP_CMD_UNLOCK,               3U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdUnlock },
#endif
#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)
    { XCP_CMD_SET_CAL_PAGE,         4U, XCP_RESOURCE_CAL_PAG,  XCP_GUARD_NONE,               Xcp_CmdSetCalPage },
    { XCP_CMD_GET_CAL_PAGE,         3U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdGetCalPage },
    { XCP_CMD_GET_PAG_PROCESSOR_INFO,1U, 0U,                   XCP_GUARD_NONE,               Xcp_CmdGetPagProcessorInfo },
    { XCP_CMD_GET_SEGMENT_INFO,     5U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdGetSegmentInfo },
    { XCP_CMD_GET_PAGE_INFO,        4U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdGetPageInfo },
    { XCP_CMD_SET_SEGMENT_MODE,     3U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdSetSegmentMode },
    { XCP_CMD_GET_SEGMENT_MODE,     3U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdGetSegmentMode },
    { XCP_CMD_COPY_CAL_PAGE,        5U, XCP_RESOURCE_CAL_PAG,  XCP_GUARD_NONE,               Xcp_CmdCopyCalPage },
#endif
#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)
    { XCP_CMD_PROGRAM_START,        1U, XCP_RESOURCE_PGM,      XCP_GUARD_NONE,               Xcp_CmdProgramStart },
    { XCP_CMD_PROGRAM_CLEAR,        8U, XCP_RESOURCE_PGM,      XCP_GUARD_NONE,               Xcp_CmdProgramClear },
    { XCP_CMD_PROGRAM,              2U, XCP_RESOURCE_PGM,      XCP_GUARD_NONE,               Xcp_CmdProgram },
    { XCP_CMD_PROGRAM_RESET,        1U, XCP_RESOURCE_PGM,      XCP_GUARD_NONE,               Xcp_CmdProgramReset },
    { XCP_CMD_GET_PGM_PROCESSOR_INFO,1U, 0U,                   XCP_GUARD_NONE,               Xcp_CmdGetPgmProcessorInfo },
    { XCP_CMD_GET_SECTOR_INFO,      3U, 0U,                    XCP_GUARD_NONE,               Xcp_CmdGetSectorInfo },
    { XCP_CMD_PROGRAM_PREPARE,      4U, XCP_RESOURCE_PGM,      XCP_GUARD_NONE,               Xcp_CmdProgramPrepare },
    { XCP_CMD_PROGRAM_FORMAT,       5U, XCP_RESOURCE_PGM,      XCP_GUARD_NONE,               Xcp_CmdProgramFormat },
    { XCP_CMD_PROGRAM_NEXT,         2U, XCP_RESOURCE_PGM,      XCP_GUARD_NONE,               Xcp_CmdProgramNext },
    { XCP_CMD_PROGRAM_MAX,          1U, XCP_RESOURCE_PGM,      XCP_GUARD_NONE,               Xcp_CmdProgramMax },
    { XCP_CMD_PROGRAM_VERIFY,       8U, XCP_RESOURCE_PGM,      XCP_GUARD_NONE,               Xcp_CmdProgramVerify },
#endif
};

#define XCP_CMD_TABLE_SIZE ((uint8)(sizeof(XCP_CMD_TABLE) / sizeof(XCP_CMD_TABLE[0])))

static sint16 Xcp_DispatchUserCmd(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void)ch;
    if (Xcp_UserCommand_Callout(cmd_data, cmd_length) != E_OK)
    {
        return Xcp_ResErr(res, XCP_ERR_CMD_UNKNOWN);
    }

    return 0;
}

void Xcp_ProcessCommand(const uint8 *cmd_data, uint8 cmd_length)
{
    Xcp_Channel *man = &g_xcp_channel;
    uint8 i;
    const Xcp_CmdEntryType *entry;

    if ((cmd_data == NULL) || (cmd_length == 0U))
    {
        return;
    }

    if ((cmd_data[0] != XCP_CMD_DOWNLOAD_MAX) && (cmd_data[0] != XCP_CMD_DOWNLOAD_NEXT))
    {
        Xcp_CancelBlockTransfer(man);
    }

    for (i = 0U; i < XCP_CMD_TABLE_SIZE; i++)
    {
        if (XCP_CMD_TABLE[i].cmd == cmd_data[0])
        {
            entry = &XCP_CMD_TABLE[i];

            if (cmd_length < entry->min_len)
            {
                Xcp_SendErrorResponse(XCP_ERR_CMD_SYNTAX);
                return;
            }

            if ((entry->guard != XCP_GUARD_ALLOW_DISCONNECTED)
                && (man->runtime_ptr->connection_state != XCP_CONNECTED))
            {
                return;
            }

            if ((entry->resource != 0U)
                && ((man->runtime_ptr->protection_status & entry->resource) != 0U))
            {
                Xcp_SendErrorResponse(XCP_ERR_ACCESS_LOCKED);
                return;
            }

            {
                sint16 len = entry->fn(man, cmd_data, cmd_length, man->storage_ptr->cto_buffer);
                if (len > 0)
                {
                    man->storage_ptr->cto_length = (uint8)len;
                    (void)Xcp_SendResponse(man->storage_ptr->cto_buffer, (uint8)len);
                }
            }
            return;
        }
    }

    if (man->runtime_ptr->connection_state != XCP_CONNECTED)
    {
        return;
    }

    Xcp_SendErrorResponse(XCP_ERR_CMD_UNKNOWN);
}
