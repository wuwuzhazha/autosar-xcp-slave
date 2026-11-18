#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Std.h"
#include "Xcp_Daq.h"
#include "Xcp_SeedKey.h"
#include "Xcp_Callback.h"
#include "Xcp_CalPage.h"
#include "Xcp_FlashProg.h"

typedef void (*Xcp_CmdHandlerType)(const uint8 *cmd_data, uint8 cmd_length);

static void Xcp_DispatchUserCmd(const uint8 *cmd_data, uint8 cmd_length)
{
    if (Xcp_UserCommand_Callout(cmd_data, cmd_length) != E_OK)
    {
        Xcp_SendErrorResponse(XCP_ERR_CMD_UNKNOWN);
    }
}

static const struct
{
    uint8              cmd;
    uint8              min_len;
    Xcp_CmdHandlerType handler;
} XCP_CMD_TABLE[] = {
    { XCP_CMD_DISCONNECT,            1U, Xcp_CmdDisconnect },
    { XCP_CMD_GET_STATUS,            1U, Xcp_CmdGetStatus },
    { XCP_CMD_SYNCH,                 1U, Xcp_CmdSynch },
    { XCP_CMD_GET_COMM_MODE_INFO,    1U, Xcp_CmdGetCommModeInfo },
    { XCP_CMD_GET_ID,                2U, Xcp_CmdGetId },
    { XCP_CMD_SET_MTA,               8U, Xcp_CmdSetMta },
    { XCP_CMD_UPLOAD,                2U, Xcp_CmdUpload },
    { XCP_CMD_SHORT_UPLOAD,          8U, Xcp_CmdShortUpload },
    { XCP_CMD_SET_REQUEST,           4U, Xcp_CmdSetRequest },
    { XCP_CMD_BUILD_CHECKSUM,        8U, Xcp_CmdBuildChecksum },
    { XCP_CMD_TRANSPORT_LAYER_CMD,   2U, Xcp_CmdTransportLayerCmd },
    { XCP_CMD_USER_CMD,              1U, Xcp_DispatchUserCmd },
    { XCP_CMD_DOWNLOAD,              2U, Xcp_CmdDownload },
    { XCP_CMD_DOWNLOAD_NEXT,         2U, Xcp_CmdDownloadNext },
    { XCP_CMD_DOWNLOAD_MAX,          1U, Xcp_CmdDownloadMax },
    { XCP_CMD_SHORT_DOWNLOAD,        8U, Xcp_CmdShortDownload },
    { XCP_CMD_MODIFY_BITS,           6U, Xcp_CmdModifyBits },
#if (XCP_DAQ_ENABLED == STD_ON)
    { XCP_CMD_GET_DAQ_PROCESSOR_INFO,  1U, Xcp_CmdGetDaqProcessorInfo },
    { XCP_CMD_GET_DAQ_RESOLUTION_INFO, 1U, Xcp_CmdGetDaqResolutionInfo },
    { XCP_CMD_GET_DAQ_EVENT_INFO,      4U, Xcp_CmdGetDaqEventInfo },
    { XCP_CMD_FREE_DAQ,                1U, Xcp_CmdFreeDaq },
    { XCP_CMD_ALLOC_DAQ,               3U, Xcp_CmdAllocDaq },
    { XCP_CMD_ALLOC_ODT,               5U, Xcp_CmdAllocOdt },
    { XCP_CMD_ALLOC_ODT_ENTRY,         6U, Xcp_CmdAllocOdtEntry },
    { XCP_CMD_SET_DAQ_PTR,             6U, Xcp_CmdSetDaqPtr },
    { XCP_CMD_WRITE_DAQ,               8U, Xcp_CmdWriteDaq },
    { XCP_CMD_SET_DAQ_LIST_MODE,       8U, Xcp_CmdSetDaqListMode },
    { XCP_CMD_GET_DAQ_LIST_MODE,       4U, Xcp_CmdGetDaqListMode },
    { XCP_CMD_START_STOP_DAQ_LIST,     4U, Xcp_CmdStartStopDaqList },
    { XCP_CMD_START_STOP_SYNCH,        2U, Xcp_CmdStartStopSynch },
    { XCP_CMD_GET_DAQ_CLOCK,           1U, Xcp_CmdGetDaqClock },
    { XCP_CMD_GET_DAQ_LIST_INFO,       4U, Xcp_CmdGetDaqListInfo },
    { XCP_CMD_CLEAR_DAQ_LIST,          4U, Xcp_CmdClearDaqList },
    { XCP_CMD_READ_DAQ,                1U, Xcp_CmdReadDaq },
#endif
#if (XCP_SEED_KEY_ENABLED == STD_ON)
    { XCP_CMD_GET_SEED,              3U, Xcp_CmdGetSeed },
    { XCP_CMD_UNLOCK,                3U, Xcp_CmdUnlock },
#endif
#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)
    { XCP_CMD_SET_CAL_PAGE,          4U, Xcp_CmdSetCalPage },
    { XCP_CMD_GET_CAL_PAGE,          3U, Xcp_CmdGetCalPage },
    { XCP_CMD_GET_PAG_PROCESSOR_INFO,1U, Xcp_CmdGetPagProcessorInfo },
    { XCP_CMD_GET_SEGMENT_INFO,      5U, Xcp_CmdGetSegmentInfo },
    { XCP_CMD_GET_PAGE_INFO,         4U, Xcp_CmdGetPageInfo },
    { XCP_CMD_SET_SEGMENT_MODE,      3U, Xcp_CmdSetSegmentMode },
    { XCP_CMD_GET_SEGMENT_MODE,      3U, Xcp_CmdGetSegmentMode },
    { XCP_CMD_COPY_CAL_PAGE,         5U, Xcp_CmdCopyCalPage },
#endif
#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)
    { XCP_CMD_PROGRAM_START,         1U, Xcp_CmdProgramStart },
    { XCP_CMD_PROGRAM_CLEAR,         8U, Xcp_CmdProgramClear },
    { XCP_CMD_PROGRAM,               2U, Xcp_CmdProgram },
    { XCP_CMD_PROGRAM_RESET,         1U, Xcp_CmdProgramReset },
    { XCP_CMD_GET_PGM_PROCESSOR_INFO,1U, Xcp_CmdGetPgmProcessorInfo },
    { XCP_CMD_GET_SECTOR_INFO,       3U, Xcp_CmdGetSectorInfo },
    { XCP_CMD_PROGRAM_PREPARE,       4U, Xcp_CmdProgramPrepare },
    { XCP_CMD_PROGRAM_FORMAT,        5U, Xcp_CmdProgramFormat },
    { XCP_CMD_PROGRAM_NEXT,          2U, Xcp_CmdProgramNext },
    { XCP_CMD_PROGRAM_MAX,           1U, Xcp_CmdProgramMax },
    { XCP_CMD_PROGRAM_VERIFY,        8U, Xcp_CmdProgramVerify },
#endif
};

#define XCP_CMD_TABLE_SIZE ((uint8)(sizeof(XCP_CMD_TABLE) / sizeof(XCP_CMD_TABLE[0])))

void Xcp_ProcessCommand(const uint8 *cmd_data, uint8 cmd_length)
{
    uint8 cmd;
    uint8 i;

    if ((cmd_data == NULL) || (cmd_length == 0U))
    {
        return;
    }

    cmd = cmd_data[0];

    if (cmd == XCP_CMD_CONNECT)
    {
        if (cmd_length < 2U)
        {
            Xcp_SendErrorResponse(XCP_ERR_CMD_SYNTAX);
            return;
        }

        Xcp_CmdConnect(cmd_data, cmd_length);
        return;
    }

    if (g_xcp_manager.runtime_ptr->connection_state != XCP_CONNECTED)
    {

        return;
    }

    for (i = 0U; i < XCP_CMD_TABLE_SIZE; i++)
    {
        if (XCP_CMD_TABLE[i].cmd == cmd)
        {
            if (cmd_length < XCP_CMD_TABLE[i].min_len)
            {
                Xcp_SendErrorResponse(XCP_ERR_CMD_SYNTAX);
                return;
            }

            XCP_CMD_TABLE[i].handler(cmd_data, cmd_length);
            return;
        }
    }

    Xcp_SendErrorResponse(XCP_ERR_CMD_UNKNOWN);
}
