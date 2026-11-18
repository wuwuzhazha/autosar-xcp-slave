#include <string.h>
#include "StandardTypes.h"

#define XCP_BLOCK_TYPE_NONE     (0U)
#define XCP_BLOCK_TYPE_DOWNLOAD (1U)
#define XCP_BLOCK_TYPE_UPLOAD   (2U)
#define XCP_BLOCK_TYPE_PROGRAM  (3U)
#include "Xcp.h"
#include "Xcp_CalPage.h"
#include "Xcp_Callback.h"
#include "Xcp_Daq.h"
#include "Xcp_FlashProg.h"
#include "Xcp_Internal.h"
#include "Xcp_SeedKey.h"
#include "Xcp_Resume.h"
#include "Xcp_Std.h"

sint16 Xcp_CmdConnect(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8 mode;

    (void) cmd_length;

    mode = cmd_data[1];

    if ((mode != XCP_CONNECT_MODE_NORMAL) && (mode != XCP_CONNECT_MODE_USER_DEFINED))
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

    Xcp_SessionOpen();

#if (XCP_DAQ_ENABLED == STD_ON)
    {

        boolean resume_kept = FALSE;
#if (XCP_RESUME_MODE_ENABLED == STD_ON)
        if (g_xcp_resume_runtime.data_valid)
        {
            uint16 ri;

            for (ri = 0U; ri < g_xcp_manager.runtime_ptr->daq_count; ri++)
            {
                if (g_xcp_manager.storage_ptr->daq_list[ri].resume_pending)
                {
                    g_xcp_manager.storage_ptr->daq_list[ri].state = XCP_DAQ_STATE_RUNNING;
                    g_xcp_manager.storage_ptr->daq_list[ri].resume_pending = FALSE;
                    resume_kept = TRUE;
                }
            }
        }
#endif
        if (resume_kept)
        {

            Xcp_RebuildActiveDaqList();
        }
        else
        {

            Xcp_FreeDaqLists();
        }
    }

    Xcp_ResetEventChannelCounters();
#endif

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;

    g_xcp_manager.storage_ptr->cto_buffer[1] = 0x00U;
#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)
    g_xcp_manager.storage_ptr->cto_buffer[1] |= XCP_RESOURCE_CAL_PAG;
#endif
#if (XCP_DAQ_ENABLED == STD_ON)
    g_xcp_manager.storage_ptr->cto_buffer[1] |= (XCP_RESOURCE_DAQ | XCP_RESOURCE_STIM);
#endif
#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)
    g_xcp_manager.storage_ptr->cto_buffer[1] |= XCP_RESOURCE_PGM;
#endif

    /* COMM_MODE_BASIC: Intel 字节序、BYTE 地址粒度、无从块模式，GET_COMM_MODE_INFO 可用 */
    g_xcp_manager.storage_ptr->cto_buffer[2] = XCP_COMM_BASIC_OPTIONAL;

    g_xcp_manager.storage_ptr->cto_buffer[3] = XCP_MAX_CTO;

    g_xcp_manager.storage_ptr->cto_buffer[4] = (uint8) (XCP_MAX_DTO & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[5] = (uint8) ((XCP_MAX_DTO >> 8U) & 0xFFU);

    g_xcp_manager.storage_ptr->cto_buffer[6] = 0x01U;   /* 协议层版本 */
    g_xcp_manager.storage_ptr->cto_buffer[7] = 0x01U;   /* 传输层版本 */

    return (sint16)(8U);
}

sint16 Xcp_CmdDisconnect(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_data;
    (void) cmd_length;

    Xcp_SessionClose();

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdGetStatus(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_data;
    (void) cmd_length;

    uint8  session_status = g_xcp_manager.runtime_ptr->session_status;
    uint16 session_cfg_id = 0U;

#if (XCP_DAQ_ENABLED == STD_ON)
    {
        uint16 i;

        for (i = 0U; i < g_xcp_manager.runtime_ptr->daq_count; i++)
        {
            if (g_xcp_manager.storage_ptr->daq_list[i].state == (uint8) XCP_DAQ_STATE_RUNNING)
            {
                session_status |= XCP_SS_DAQ_RUNNING;
                break;
            }
        }
    }
#endif
#if (XCP_RESUME_MODE_ENABLED == STD_ON)
    if (Xcp_IsResumeModeActive())
    {
        session_status |= XCP_SS_RESUME;
    }
    if (g_xcp_resume_runtime.data_valid)
    {
        session_cfg_id = g_xcp_resume_data.session_cfg_id;
    }
#endif

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_manager.storage_ptr->cto_buffer[1] = session_status;
    g_xcp_manager.storage_ptr->cto_buffer[2] = g_xcp_manager.runtime_ptr->protection_status;
    g_xcp_manager.storage_ptr->cto_buffer[3] = 0x00U;
    g_xcp_manager.storage_ptr->cto_buffer[4] = (uint8) (session_cfg_id & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[5] = (uint8) ((session_cfg_id >> 8U) & 0xFFU);

    return (sint16)(6U);
}

sint16 Xcp_CmdSynch(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_data;
    (void) cmd_length;

    return Xcp_ResErr(res, XCP_ERR_CMD_SYNCH);
}

sint16 Xcp_CmdGetCommModeInfo(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_data;
    (void) cmd_length;

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_manager.storage_ptr->cto_buffer[1] = 0x00U;

    /* COMM_MODE_OPTIONAL: 主块模式（DOWNLOAD_NEXT）未按 ASAM 实现，不宣称 */
    g_xcp_manager.storage_ptr->cto_buffer[2] = 0x00U;
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
    g_xcp_manager.storage_ptr->cto_buffer[2] |= XCP_COMM_OPT_INTERLEAVED_MODE;
#endif

    g_xcp_manager.storage_ptr->cto_buffer[3] = 0x00U;
    g_xcp_manager.storage_ptr->cto_buffer[4] = XCP_MAX_BS;
    g_xcp_manager.storage_ptr->cto_buffer[5] = XCP_MIN_ST;
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
    g_xcp_manager.storage_ptr->cto_buffer[6] = XCP_CMD_QUEUE_SIZE;
#else
    g_xcp_manager.storage_ptr->cto_buffer[6] = 0x00U;
#endif
    /* XCP 驱动版本: 高半字节主版本、低半字节次版本 */
    g_xcp_manager.storage_ptr->cto_buffer[7] = (uint8) (((XCP_SW_MAJOR_VERSION & 0x0FU) << 4U)
                                                        | (XCP_SW_MINOR_VERSION & 0x0FU));

    return (sint16)(8U);
}

sint16 Xcp_CmdGetId(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_length;

    uint8        id_type = cmd_data[1];
    uint32       id_len = 0U;
    const uint8 *id_ptr = NULL;

    if (id_type == 0x00U)
    {
        id_ptr = (const uint8 *) XCP_ID_TEXT;
        id_len = (uint32) strlen(XCP_ID_TEXT);
    }
    else
    {

    }

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_manager.storage_ptr->cto_buffer[1] = 0x00U;
    g_xcp_manager.storage_ptr->cto_buffer[2] = 0x00U;
    g_xcp_manager.storage_ptr->cto_buffer[3] = 0x00U;

    g_xcp_manager.storage_ptr->cto_buffer[4] = (uint8) (id_len & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[5] = (uint8) ((id_len >> 8U) & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[6] = (uint8) ((id_len >> 16U) & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[7] = (uint8) ((id_len >> 24U) & 0xFFU);

    if (id_len > 0U)
    {
        g_xcp_manager.runtime_ptr->mta = (uint32) id_ptr;
        g_xcp_manager.runtime_ptr->mta_extension = 0U;
    }

    return (sint16)(8U);
}

sint16 Xcp_CmdSetMta(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_length;

    g_xcp_manager.runtime_ptr->mta_extension = cmd_data[3];

    g_xcp_manager.runtime_ptr->mta = Xcp_LeRead32(&cmd_data[4U]);

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdUpload(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  num_elements = 0;
    uint8 *src_ptr = NULL;

    (void) cmd_length;

    num_elements = cmd_data[1];

    if (num_elements > (XCP_MAX_CTO - 1U))
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (XCP_CHECK_READ_ACCESS(g_xcp_manager.runtime_ptr->mta, num_elements) != 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
    }

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;

    src_ptr = (uint8 *) g_xcp_manager.runtime_ptr->mta;

    (void) memcpy(&g_xcp_manager.storage_ptr->cto_buffer[1U], src_ptr, num_elements);

    g_xcp_manager.runtime_ptr->mta += num_elements;

    return (sint16)(1U + num_elements);
}

sint16 Xcp_CmdShortUpload(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  num_elements;
    uint8  address_extension;
    uint32 address;
    uint8 *src_ptr;

    (void) cmd_length;

    num_elements = cmd_data[1];
    address_extension = cmd_data[3];
    address = Xcp_LeRead32(&cmd_data[4U]);

    (void) address_extension;

    if (num_elements > (XCP_MAX_CTO - 1U))
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (XCP_CHECK_READ_ACCESS(address, num_elements) != 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
    }

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;

    src_ptr = (uint8 *) address;

    (void) memcpy(&g_xcp_manager.storage_ptr->cto_buffer[1U], src_ptr, num_elements);

    g_xcp_manager.runtime_ptr->mta = address + num_elements;
    g_xcp_manager.runtime_ptr->mta_extension = address_extension;

    return (sint16)(1U + num_elements);
}

sint16 Xcp_CmdDownload(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  num_elements;
    uint8 *dst_ptr;

    num_elements = cmd_data[1];

    if ((2U + num_elements) > cmd_length)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (XCP_CHECK_WRITE_ACCESS(g_xcp_manager.runtime_ptr->mta, num_elements) != 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
    }

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(XCP_RESOURCE_CAL_PAG))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    dst_ptr = (uint8 *) g_xcp_manager.runtime_ptr->mta;

    (void) memcpy(dst_ptr, &cmd_data[2U], num_elements);

    g_xcp_manager.runtime_ptr->mta += num_elements;

#if (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)

    g_xcp_manager.runtime_ptr->block_transfer_active = TRUE;
    g_xcp_manager.runtime_ptr->block_transfer_type   = XCP_BLOCK_TYPE_DOWNLOAD;
    g_xcp_manager.runtime_ptr->block_transfer_remaining = 0xFFFFFFFFUL;
#endif

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdDownloadNext(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{

#if (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)
    if ((g_xcp_manager.runtime_ptr->block_transfer_active == FALSE)
        || (g_xcp_manager.runtime_ptr->block_transfer_type != XCP_BLOCK_TYPE_DOWNLOAD))
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    if (Xcp_ValidateBlockSequence(XCP_BLOCK_TYPE_DOWNLOAD, cmd_data[1]) != E_OK)
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }
#endif

    return Xcp_CmdDownload(cmd_data, cmd_length, res);
}

sint16 Xcp_CmdShortDownload(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  num_elements;
    uint8  address_extension;
    uint32 address;
    uint8 *dst_ptr;

    num_elements = cmd_data[1];
    address_extension = cmd_data[3];
    address = Xcp_LeRead32(&cmd_data[4U]);

    (void) address_extension;

    if ((8U + num_elements) > cmd_length)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (XCP_CHECK_WRITE_ACCESS(address, num_elements) != 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
    }

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(XCP_RESOURCE_CAL_PAG))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    dst_ptr = (uint8 *) address;

    (void) memcpy(dst_ptr, &cmd_data[8U], num_elements);

    g_xcp_manager.runtime_ptr->mta = address + num_elements;
    g_xcp_manager.runtime_ptr->mta_extension = address_extension;

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdDownloadMax(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  num_elements;
    uint8 *dst_ptr;

    if (cmd_length < 1U)
    {
        return Xcp_ResErr(res, XCP_ERR_CMD_SYNTAX);
    }

    num_elements = cmd_length - 1U;

    if (XCP_CHECK_WRITE_ACCESS(g_xcp_manager.runtime_ptr->mta, num_elements) != 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
    }

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(XCP_RESOURCE_CAL_PAG))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    dst_ptr = (uint8 *) g_xcp_manager.runtime_ptr->mta;

    (void) memcpy(dst_ptr, &cmd_data[1U], num_elements);

    g_xcp_manager.runtime_ptr->mta += num_elements;

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdBuildChecksum(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint32 block_size;
    uint32 checksum = 0U;
    uint8  checksum_len = 0U;

    (void) cmd_length;

    block_size = Xcp_LeRead32(&cmd_data[4U]);

    if (Xcp_BuildChecksum_Callout(g_xcp_manager.runtime_ptr->mta, block_size, &checksum, &checksum_len) == E_OK)
    {
        g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;

        if (checksum_len == 4U)
        {
            g_xcp_manager.storage_ptr->cto_buffer[1] = 0x03U;
            g_xcp_manager.storage_ptr->cto_buffer[4] = (uint8) (checksum & 0xFFU);
            g_xcp_manager.storage_ptr->cto_buffer[5] = (uint8) ((checksum >> 8U) & 0xFFU);
            g_xcp_manager.storage_ptr->cto_buffer[6] = (uint8) ((checksum >> 16U) & 0xFFU);
            g_xcp_manager.storage_ptr->cto_buffer[7] = (uint8) ((checksum >> 24U) & 0xFFU);
        }
        else if (checksum_len == 2U)
        {
            g_xcp_manager.storage_ptr->cto_buffer[1] = 0x02U;
            g_xcp_manager.storage_ptr->cto_buffer[4] = (uint8) (checksum & 0xFFU);
            g_xcp_manager.storage_ptr->cto_buffer[5] = (uint8) ((checksum >> 8U) & 0xFFU);
            g_xcp_manager.storage_ptr->cto_buffer[6] = 0x00U;
            g_xcp_manager.storage_ptr->cto_buffer[7] = 0x00U;
        }
        else if (checksum_len == 1U)
        {
            g_xcp_manager.storage_ptr->cto_buffer[1] = 0x01U;
            g_xcp_manager.storage_ptr->cto_buffer[4] = (uint8) (checksum & 0xFFU);
            g_xcp_manager.storage_ptr->cto_buffer[5] = 0x00U;
            g_xcp_manager.storage_ptr->cto_buffer[6] = 0x00U;
            g_xcp_manager.storage_ptr->cto_buffer[7] = 0x00U;
        }
        else
        {

            return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
        }

        g_xcp_manager.storage_ptr->cto_buffer[2] = 0x00U;
        g_xcp_manager.storage_ptr->cto_buffer[3] = 0x00U;

        return (sint16)(8U);
    }
    else
    {
        return Xcp_ResErr(res, XCP_ERR_GENERIC);
    }
}

sint16 Xcp_CmdModifyBits(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8   shift_value;
    uint16  and_mask;
    uint16  xor_mask;
    uint32  old_value;
    uint32  new_value;
    uint32 *target_ptr;

    (void) cmd_length;

    shift_value = cmd_data[1];
    and_mask = Xcp_LeRead16(&cmd_data[2U]);
    xor_mask = Xcp_LeRead16(&cmd_data[4U]);

    if (shift_value > 16U)
    {
        return Xcp_ResErr(res, XCP_ERR_CMD_SYNTAX);
    }

    if (XCP_CHECK_READ_ACCESS(g_xcp_manager.runtime_ptr->mta, 4U) != 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
    }

    if (XCP_CHECK_WRITE_ACCESS(g_xcp_manager.runtime_ptr->mta, 4U) != 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
    }

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(XCP_RESOURCE_CAL_PAG))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    target_ptr = (uint32 *) g_xcp_manager.runtime_ptr->mta;
    old_value = *target_ptr;

    new_value = (old_value & ~(((uint32) 0xFFFFU) << shift_value)) | (((uint32) and_mask) << shift_value);
    new_value ^= ((uint32) xor_mask << shift_value);

    *target_ptr = new_value;

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdSetRequest(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8 mode;

    (void)cmd_length;
    mode = cmd_data[1];

    if ((mode & XCP_SET_REQ_STORE_DAQ_NO_RESUME) != 0U)
    {
        /* 无 RESUME 的 DAQ 配置存储不支持：显式拒绝 */
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if ((mode & XCP_SET_REQ_CLEAR_DAQ_REQ) != 0U)
    {
#if (XCP_RESUME_MODE_ENABLED == STD_ON)
        if (Xcp_ClearStoredDaqConfig() != E_OK)
        {
            return Xcp_ResErr(res, XCP_ERR_GENERIC);
        }
#else
        return Xcp_ResErr(res, XCP_ERR_CMD_UNKNOWN);
#endif
    }

    if ((mode & XCP_SET_REQ_STORE_DAQ_RESUME) != 0U)
    {
#if (XCP_RESUME_MODE_ENABLED == STD_ON)
        if (Xcp_StoreDaqConfig(Xcp_LeRead16(&cmd_data[2U])) != E_OK)
        {
            return Xcp_ResErr(res, XCP_ERR_GENERIC);
        }
#else
        return Xcp_ResErr(res, XCP_ERR_CMD_UNKNOWN);
#endif
    }

    if ((mode & XCP_SET_REQ_STORE_CAL_REQ) != 0U)
    {
        g_xcp_manager.runtime_ptr->session_status |= XCP_SS_STORE_CAL_REQ;
    }

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdTransportLayerCmd(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    if (cmd_length < 2U)
    {
        return Xcp_ResErr(res, XCP_ERR_CMD_SYNTAX);
    }

    switch (cmd_data[1])
    {
#if (XCP_ON_CAN_ENABLED == STD_ON)
    case 0xFFU:
        g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
        g_xcp_manager.storage_ptr->cto_buffer[1] = 0x00U;
        g_xcp_manager.storage_ptr->cto_buffer[2] = 0x00U;
        g_xcp_manager.storage_ptr->cto_buffer[3] = 0x00U;
        g_xcp_manager.storage_ptr->cto_buffer[4] = 0x00U;
        g_xcp_manager.storage_ptr->cto_buffer[5] = (uint8)'X';
        g_xcp_manager.storage_ptr->cto_buffer[6] = (uint8)'C';
        g_xcp_manager.storage_ptr->cto_buffer[7] = (uint8)'P';
        return (sint16)(8U);
        break;
#endif
    default:
        return Xcp_ResErr(res, XCP_ERR_CMD_UNKNOWN);
        break;
    }
}

#if (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)

void Xcp_InitBlockTransfer(void)
{
    if (g_xcp_manager.runtime_ptr == NULL)
    {
        return;
    }

    g_xcp_manager.runtime_ptr->block_sequence_counter = 0U;
    g_xcp_manager.runtime_ptr->expected_block_sequence = 0U;
    g_xcp_manager.runtime_ptr->block_transfer_active = FALSE;
    g_xcp_manager.runtime_ptr->block_transfer_remaining = 0U;
    g_xcp_manager.runtime_ptr->block_transfer_type = XCP_BLOCK_TYPE_NONE;
}

Std_ReturnType Xcp_StartBlockTransfer(uint8 block_type, uint32 total_size, uint32 first_packet_size)
{
    if (g_xcp_manager.runtime_ptr == NULL)
    {
        return E_NOT_OK;
    }

    if (g_xcp_manager.runtime_ptr->block_transfer_active == TRUE)
    {
        return E_NOT_OK;
    }

    g_xcp_manager.runtime_ptr->block_transfer_active = TRUE;
    g_xcp_manager.runtime_ptr->block_transfer_type = block_type;
    g_xcp_manager.runtime_ptr->block_sequence_counter = 0U;
    g_xcp_manager.runtime_ptr->expected_block_sequence = 1U;

    if (total_size > first_packet_size)
    {
        g_xcp_manager.runtime_ptr->block_transfer_remaining = total_size - first_packet_size;
    }
    else
    {
        g_xcp_manager.runtime_ptr->block_transfer_remaining = 0U;
        g_xcp_manager.runtime_ptr->block_transfer_active = FALSE;
    }

    return E_OK;
}

Std_ReturnType Xcp_ValidateBlockSequence(uint8 block_type, uint32 data_size)
{
    if (g_xcp_manager.runtime_ptr == NULL)
    {
        return E_NOT_OK;
    }

    if (g_xcp_manager.runtime_ptr->block_transfer_active == FALSE)
    {
        return E_NOT_OK;
    }

    if (g_xcp_manager.runtime_ptr->block_transfer_type != block_type)
    {
        Xcp_InitBlockTransfer();
        return E_NOT_OK;
    }

    g_xcp_manager.runtime_ptr->block_sequence_counter++;
    g_xcp_manager.runtime_ptr->expected_block_sequence++;

    if (g_xcp_manager.runtime_ptr->block_transfer_remaining >= data_size)
    {
        g_xcp_manager.runtime_ptr->block_transfer_remaining -= data_size;
    }
    else
    {
        g_xcp_manager.runtime_ptr->block_transfer_remaining = 0U;
    }

    if (g_xcp_manager.runtime_ptr->block_transfer_remaining == 0U)
    {
        g_xcp_manager.runtime_ptr->block_transfer_active = FALSE;
        g_xcp_manager.runtime_ptr->block_transfer_type = XCP_BLOCK_TYPE_NONE;
    }

    return E_OK;
}

void Xcp_CancelBlockTransfer(void)
{
    Xcp_InitBlockTransfer();
}

boolean Xcp_IsBlockTransferActive(void)
{
    if (g_xcp_manager.runtime_ptr == NULL)
    {
        return FALSE;
    }

    return g_xcp_manager.runtime_ptr->block_transfer_active;
}

uint32 Xcp_GetBlockTransferRemaining(void)
{
    if (g_xcp_manager.runtime_ptr == NULL)
    {
        return 0U;
    }

    return g_xcp_manager.runtime_ptr->block_transfer_remaining;
}
#endif
