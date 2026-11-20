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

sint16 Xcp_CmdConnect(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
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

            for (ri = 0U; ri < ch->runtime_ptr->daq_count; ri++)
            {
                if (ch->storage_ptr->daq_list[ri].resume_pending)
                {
                    ch->storage_ptr->daq_list[ri].state = XCP_DAQ_STATE_RUNNING;
                    ch->storage_ptr->daq_list[ri].resume_pending = FALSE;
                    resume_kept = TRUE;
                }
            }
        }
#endif
        if (resume_kept)
        {

            Xcp_RebuildActiveDaqList(&g_xcp_channel);
        }
        else
        {

            Xcp_FreeDaqLists(&g_xcp_channel);
        }
    }

    Xcp_ResetEventChannelCounters(&g_xcp_channel);
#endif

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;

    ch->storage_ptr->cto_buffer[1] = 0x00U;
#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)
    ch->storage_ptr->cto_buffer[1] |= XCP_RESOURCE_CAL_PAG;
#endif
#if (XCP_DAQ_ENABLED == STD_ON)
    ch->storage_ptr->cto_buffer[1] |= (XCP_RESOURCE_DAQ | XCP_RESOURCE_STIM);
#endif
#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)
    ch->storage_ptr->cto_buffer[1] |= XCP_RESOURCE_PGM;
#endif

    ch->storage_ptr->cto_buffer[2] = (uint8)(XCP_COMM_BASIC_OPTIONAL
        | ((XCP_BLOCK_TRANSFER_ENABLED == STD_ON) ? XCP_COMM_BASIC_SLAVE_BLOCK_MODE : 0U));

    ch->storage_ptr->cto_buffer[3] = XCP_MAX_CTO;

    ch->storage_ptr->cto_buffer[4] = (uint8) (XCP_MAX_DTO & 0xFFU);
    ch->storage_ptr->cto_buffer[5] = (uint8) ((XCP_MAX_DTO >> 8U) & 0xFFU);

    ch->storage_ptr->cto_buffer[6] = 0x01U;
    ch->storage_ptr->cto_buffer[7] = 0x01U;

    return (sint16)(8U);
}

sint16 Xcp_CmdDisconnect(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_data;
    (void) cmd_length;

    Xcp_SessionClose();

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdGetStatus(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_data;
    (void) cmd_length;

    uint8  session_status = ch->runtime_ptr->session_status;
    uint16 session_cfg_id = 0U;

#if (XCP_DAQ_ENABLED == STD_ON)
    {
        uint16 i;

        for (i = 0U; i < ch->runtime_ptr->daq_count; i++)
        {
            if (ch->storage_ptr->daq_list[i].state == (uint8) XCP_DAQ_STATE_RUNNING)
            {
                session_status |= XCP_SS_DAQ_RUNNING;
                break;
            }
        }
    }
#endif
#if (XCP_RESUME_MODE_ENABLED == STD_ON)
    if (Xcp_IsResumeModeActive(&g_xcp_channel))
    {
        session_status |= XCP_SS_RESUME;
    }
    if (g_xcp_resume_runtime.data_valid)
    {
        session_cfg_id = g_xcp_resume_data.session_cfg_id;
    }
#endif

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = session_status;
    ch->storage_ptr->cto_buffer[2] = ch->runtime_ptr->protection_status;
    ch->storage_ptr->cto_buffer[3] = 0x00U;
    ch->storage_ptr->cto_buffer[4] = (uint8) (session_cfg_id & 0xFFU);
    ch->storage_ptr->cto_buffer[5] = (uint8) ((session_cfg_id >> 8U) & 0xFFU);

    return (sint16)(6U);
}

sint16 Xcp_CmdSynch(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_data;
    (void) cmd_length;

    return Xcp_ResErr(res, XCP_ERR_CMD_SYNCH);
}

sint16 Xcp_CmdGetCommModeInfo(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_data;
    (void) cmd_length;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = 0x00U;

    ch->storage_ptr->cto_buffer[2] = 0x00U;
#if (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)
    ch->storage_ptr->cto_buffer[2] |= XCP_COMM_OPT_MASTER_BLOCK_MODE;
#endif
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
    ch->storage_ptr->cto_buffer[2] |= XCP_COMM_OPT_INTERLEAVED_MODE;
#endif

    ch->storage_ptr->cto_buffer[3] = 0x00U;
    ch->storage_ptr->cto_buffer[4] = XCP_MAX_BS;
    ch->storage_ptr->cto_buffer[5] = XCP_MIN_ST;
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
    ch->storage_ptr->cto_buffer[6] = XCP_CMD_QUEUE_SIZE;
#else
    ch->storage_ptr->cto_buffer[6] = 0x00U;
#endif

    ch->storage_ptr->cto_buffer[7] = (uint8) (((XCP_SW_MAJOR_VERSION & 0x0FU) << 4U)
                                                        | (XCP_SW_MINOR_VERSION & 0x0FU));

    return (sint16)(8U);
}

sint16 Xcp_CmdGetId(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
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

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = 0x00U;
    ch->storage_ptr->cto_buffer[2] = 0x00U;
    ch->storage_ptr->cto_buffer[3] = 0x00U;

    ch->storage_ptr->cto_buffer[4] = (uint8) (id_len & 0xFFU);
    ch->storage_ptr->cto_buffer[5] = (uint8) ((id_len >> 8U) & 0xFFU);
    ch->storage_ptr->cto_buffer[6] = (uint8) ((id_len >> 16U) & 0xFFU);
    ch->storage_ptr->cto_buffer[7] = (uint8) ((id_len >> 24U) & 0xFFU);

    if (id_len > 0U)
    {
        ch->runtime_ptr->mta = XCP_MEM_ADDR(id_ptr);
        ch->runtime_ptr->mta_extension = 0U;
    }

    return (sint16)(8U);
}

sint16 Xcp_CmdSetMta(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8 ext = cmd_data[3];

    (void) cmd_length;

    if (Xcp_ExtValid(ch, ext) == FALSE)
    {
        return Xcp_ResErr(res, XCP_ERR_SEGMENT_NOT_VALID);
    }

    ch->runtime_ptr->mta_extension = ext;

    ch->runtime_ptr->mta = Xcp_LeRead32(&cmd_data[4U]);

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdUpload(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  num_elements;
    uint8  first_chunk;
    uint8 *src_ptr;

    (void) cmd_length;

    num_elements = cmd_data[1];

    if (num_elements > (XCP_MAX_CTO - 1U))
    {
        if (XCP_BLOCK_TRANSFER_ENABLED != STD_ON)
        {
            return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
        }

        first_chunk = (uint8)(XCP_MAX_CTO - 1U);
    }
    else
    {
        first_chunk = num_elements;
    }

    src_ptr = Xcp_MemPtrRead(ch, ch->runtime_ptr->mta_extension, ch->runtime_ptr->mta, num_elements);

    if (src_ptr == NULL_PTR)
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
    }

    if (num_elements > first_chunk)
    {
        if (Xcp_StartBlockTransfer(ch, XCP_BLOCK_TYPE_UPLOAD, num_elements, first_chunk) != E_OK)
        {
            return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
        }
    }

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;

    (void) memcpy(&ch->storage_ptr->cto_buffer[1U], src_ptr, first_chunk);

    ch->runtime_ptr->mta += first_chunk;

    return (sint16)(1U + first_chunk);
}

boolean Xcp_ContinueBlockUpload(Xcp_Channel *ch)
{
    Xcp_RuntimeType *runtime = ch->runtime_ptr;
    uint32          chunk;
    uint8          *src_ptr;

    if ((runtime == NULL_PTR)
        || (runtime->block_transfer_active == FALSE)
        || (runtime->block_transfer_type != XCP_BLOCK_TYPE_UPLOAD))
    {
        return FALSE;
    }

    if ((runtime->tx.state != XCP_TX_IDLE) || (runtime->tx.parked != FALSE))
    {
        return FALSE;
    }

    chunk = runtime->block_transfer_remaining;
    if (chunk > (XCP_MAX_CTO - 1U))
    {
        chunk = (XCP_MAX_CTO - 1U);
    }

    if (chunk == 0U)
    {
        Xcp_InitBlockTransfer(ch);
        return FALSE;
    }

    src_ptr = Xcp_MemPtrRead(ch, runtime->mta_extension, runtime->mta, chunk);
    if (src_ptr == NULL_PTR)
    {
        Xcp_InitBlockTransfer(ch);
        return FALSE;
    }

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    (void) memcpy(&ch->storage_ptr->cto_buffer[1U], src_ptr, chunk);

    if (Xcp_SendResponse(ch->storage_ptr->cto_buffer, (uint8)(1U + chunk)) != E_OK)
    {
        Xcp_InitBlockTransfer(ch);
        return FALSE;
    }

    runtime->mta += chunk;
    runtime->block_transfer_remaining -= chunk;

    if (runtime->block_transfer_remaining == 0U)
    {
        runtime->block_transfer_active = FALSE;
        runtime->block_transfer_type = XCP_BLOCK_TYPE_NONE;
    }

    return TRUE;
}

sint16 Xcp_CmdShortUpload(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  num_elements;
    uint8  address_extension;
    uint32 address;
    uint8 *src_ptr;

    (void) cmd_length;

    num_elements = cmd_data[1];
    address_extension = cmd_data[3];
    address = Xcp_LeRead32(&cmd_data[4U]);

    if (num_elements > (XCP_MAX_CTO - 1U))
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    src_ptr = Xcp_MemPtrRead(ch, address_extension, address, num_elements);

    if (src_ptr == NULL_PTR)
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
    }

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;

    (void) memcpy(&ch->storage_ptr->cto_buffer[1U], src_ptr, num_elements);

    ch->runtime_ptr->mta = address + num_elements;
    ch->runtime_ptr->mta_extension = address_extension;

    return (sint16)(1U + num_elements);
}

sint16 Xcp_CmdDownload(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  num_elements;
    uint8 *dst_ptr;

    num_elements = cmd_data[1];

    if ((2U + num_elements) > cmd_length)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    dst_ptr = Xcp_MemPtrWrite(ch, ch->runtime_ptr->mta_extension, ch->runtime_ptr->mta, num_elements);

    if (dst_ptr == NULL_PTR)
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
    }

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(ch, XCP_RESOURCE_CAL_PAG))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    (void) memcpy(dst_ptr, &cmd_data[2U], num_elements);

    ch->runtime_ptr->mta += num_elements;

#if (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)

    ch->runtime_ptr->block_transfer_active = TRUE;
    ch->runtime_ptr->block_transfer_type   = XCP_BLOCK_TYPE_DOWNLOAD;
    ch->runtime_ptr->block_transfer_remaining = 0xFFFFFFFFUL;
#endif

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdDownloadNext(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
#if (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)
    uint8  num_elements;
    uint8 *dst_ptr;

    num_elements = cmd_data[1];

    if ((2U + num_elements) > cmd_length)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if ((ch->runtime_ptr->block_transfer_active == FALSE)
        || (ch->runtime_ptr->block_transfer_type != XCP_BLOCK_TYPE_DOWNLOAD))
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    if (Xcp_ValidateBlockSequence(&g_xcp_channel, XCP_BLOCK_TYPE_DOWNLOAD, num_elements) != E_OK)
    {
        return Xcp_ResErr(res, XCP_ERR_SEQUENCE);
    }

    dst_ptr = Xcp_MemPtrWrite(ch, ch->runtime_ptr->mta_extension, ch->runtime_ptr->mta, num_elements);

    if (dst_ptr == NULL_PTR)
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
    }

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(ch, XCP_RESOURCE_CAL_PAG))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    (void) memcpy(dst_ptr, &cmd_data[2U], num_elements);

    ch->runtime_ptr->mta += num_elements;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
#else
    (void)ch; (void)cmd_data; (void)cmd_length;
    return Xcp_ResErr(res, XCP_ERR_CMD_UNKNOWN);
#endif
}

sint16 Xcp_CmdShortDownload(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  num_elements;
    uint8  address_extension;
    uint32 address;
    uint8 *dst_ptr;

    num_elements = cmd_data[1];
    address_extension = cmd_data[3];
    address = Xcp_LeRead32(&cmd_data[4U]);

    if ((8U + num_elements) > cmd_length)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    dst_ptr = Xcp_MemPtrWrite(ch, address_extension, address, num_elements);

    if (dst_ptr == NULL_PTR)
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
    }

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(ch, XCP_RESOURCE_CAL_PAG))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    (void) memcpy(dst_ptr, &cmd_data[8U], num_elements);

    ch->runtime_ptr->mta = address + num_elements;
    ch->runtime_ptr->mta_extension = address_extension;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdDownloadMax(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  num_elements;
    uint8 *dst_ptr;

    if (cmd_length < 1U)
    {
        return Xcp_ResErr(res, XCP_ERR_CMD_SYNTAX);
    }

    num_elements = cmd_length - 1U;

    dst_ptr = Xcp_MemPtrWrite(ch, ch->runtime_ptr->mta_extension, ch->runtime_ptr->mta, num_elements);

    if (dst_ptr == NULL_PTR)
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
    }

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(ch, XCP_RESOURCE_CAL_PAG))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    (void) memcpy(dst_ptr, &cmd_data[1U], num_elements);

    ch->runtime_ptr->mta += num_elements;

#if (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)

    ch->runtime_ptr->block_transfer_active = TRUE;
    ch->runtime_ptr->block_transfer_type   = XCP_BLOCK_TYPE_DOWNLOAD;
    ch->runtime_ptr->block_transfer_remaining = 0xFFFFFFFFUL;
#endif

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdBuildChecksum(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint32 block_size;
    uint32 checksum = 0U;
    uint8  checksum_len = 0U;

    (void) cmd_length;

    block_size = Xcp_LeRead32(&cmd_data[4U]);

    if (Xcp_BuildChecksum_Callout(
            Xcp_MemTranslate(ch, ch->runtime_ptr->mta_extension, ch->runtime_ptr->mta),
            block_size, &checksum, &checksum_len) == E_OK)
    {
        ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;

        if (checksum_len == 4U)
        {
            ch->storage_ptr->cto_buffer[1] = 0x03U;
            ch->storage_ptr->cto_buffer[4] = (uint8) (checksum & 0xFFU);
            ch->storage_ptr->cto_buffer[5] = (uint8) ((checksum >> 8U) & 0xFFU);
            ch->storage_ptr->cto_buffer[6] = (uint8) ((checksum >> 16U) & 0xFFU);
            ch->storage_ptr->cto_buffer[7] = (uint8) ((checksum >> 24U) & 0xFFU);
        }
        else if (checksum_len == 2U)
        {
            ch->storage_ptr->cto_buffer[1] = 0x02U;
            ch->storage_ptr->cto_buffer[4] = (uint8) (checksum & 0xFFU);
            ch->storage_ptr->cto_buffer[5] = (uint8) ((checksum >> 8U) & 0xFFU);
            ch->storage_ptr->cto_buffer[6] = 0x00U;
            ch->storage_ptr->cto_buffer[7] = 0x00U;
        }
        else if (checksum_len == 1U)
        {
            ch->storage_ptr->cto_buffer[1] = 0x01U;
            ch->storage_ptr->cto_buffer[4] = (uint8) (checksum & 0xFFU);
            ch->storage_ptr->cto_buffer[5] = 0x00U;
            ch->storage_ptr->cto_buffer[6] = 0x00U;
            ch->storage_ptr->cto_buffer[7] = 0x00U;
        }
        else
        {

            return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
        }

        ch->storage_ptr->cto_buffer[2] = 0x00U;
        ch->storage_ptr->cto_buffer[3] = 0x00U;

        return (sint16)(8U);
    }
    else
    {
        return Xcp_ResErr(res, XCP_ERR_GENERIC);
    }
}

sint16 Xcp_CmdModifyBits(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8   shift_value;
    uint16  and_mask;
    uint16  xor_mask;
    uint32  old_value;
    uint32  new_value;
    uint8  *target_ptr;

    (void) cmd_length;

    shift_value = cmd_data[1];
    and_mask = Xcp_LeRead16(&cmd_data[2U]);
    xor_mask = Xcp_LeRead16(&cmd_data[4U]);

    if (shift_value > 16U)
    {
        return Xcp_ResErr(res, XCP_ERR_CMD_SYNTAX);
    }

    target_ptr = Xcp_MemPtrWrite(ch, ch->runtime_ptr->mta_extension, ch->runtime_ptr->mta, 4U);

    if (target_ptr == NULL_PTR)
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_DENIED);
    }

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(ch, XCP_RESOURCE_CAL_PAG))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif
    (void) memcpy(&old_value, target_ptr, 4U);

    new_value = (old_value & ~(((uint32) 0xFFFFU) << shift_value)) | (((uint32) and_mask) << shift_value);
    new_value ^= ((uint32) xor_mask << shift_value);

    (void) memcpy(target_ptr, &new_value, 4U);

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdSetRequest(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8 mode;

    (void)cmd_length;
    mode = cmd_data[1];

    if ((mode & XCP_SET_REQ_CLEAR_DAQ_REQ) != 0U)
    {
#if (XCP_RESUME_MODE_ENABLED == STD_ON)
        if (Xcp_ClearStoredDaqConfig(&g_xcp_channel) != E_OK)
        {
            return Xcp_ResErr(res, XCP_ERR_GENERIC);
        }
#else
        return Xcp_ResErr(res, XCP_ERR_CMD_UNKNOWN);
#endif
    }

#if (XCP_RESUME_MODE_ENABLED == STD_ON)
    if ((mode & (XCP_SET_REQ_STORE_DAQ_NO_RESUME | XCP_SET_REQ_STORE_DAQ_RESUME)) != 0U)
    {
        boolean resume = ((mode & XCP_SET_REQ_STORE_DAQ_RESUME) != 0U)
                      && ((mode & XCP_SET_REQ_STORE_DAQ_NO_RESUME) == 0U);

        if (Xcp_StoreDaqConfig(&g_xcp_channel, Xcp_LeRead16(&cmd_data[2U]), resume) != E_OK)
        {
            return Xcp_ResErr(res, XCP_ERR_GENERIC);
        }
    }
#else
    if ((mode & (XCP_SET_REQ_STORE_DAQ_NO_RESUME | XCP_SET_REQ_STORE_DAQ_RESUME)) != 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_CMD_UNKNOWN);
    }
#endif

    if ((mode & XCP_SET_REQ_STORE_CAL_REQ) != 0U)
    {
        ch->runtime_ptr->session_status |= XCP_SS_STORE_CAL_REQ;
    }

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdTransportLayerCmd(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    if (cmd_length < 2U)
    {
        return Xcp_ResErr(res, XCP_ERR_CMD_SYNTAX);
    }

    switch (cmd_data[1])
    {
#if (XCP_ON_CAN_ENABLED == STD_ON)
    case 0xFFU:
        ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
        ch->storage_ptr->cto_buffer[1] = 0x00U;
        ch->storage_ptr->cto_buffer[2] = 0x00U;
        ch->storage_ptr->cto_buffer[3] = 0x00U;
        ch->storage_ptr->cto_buffer[4] = 0x00U;
        ch->storage_ptr->cto_buffer[5] = (uint8)'X';
        ch->storage_ptr->cto_buffer[6] = (uint8)'C';
        ch->storage_ptr->cto_buffer[7] = (uint8)'P';
        return (sint16)(8U);
        break;
#endif
    default:
        return Xcp_ResErr(res, XCP_ERR_CMD_UNKNOWN);
        break;
    }
}

void Xcp_InitBlockTransfer(Xcp_Channel *ch)
{
    if (ch->runtime_ptr == NULL)
    {
        return;
    }

    ch->runtime_ptr->block_transfer_active = FALSE;
    ch->runtime_ptr->block_transfer_remaining = 0U;
    ch->runtime_ptr->block_transfer_type = XCP_BLOCK_TYPE_NONE;
}

Std_ReturnType Xcp_StartBlockTransfer(Xcp_Channel *ch, uint8 block_type, uint32 total_size, uint32 first_packet_size)
{
    if ((ch->runtime_ptr == NULL) || (XCP_BLOCK_TRANSFER_ENABLED != STD_ON))
    {
        return E_NOT_OK;
    }

    if (ch->runtime_ptr->block_transfer_active == TRUE)
    {
        return E_NOT_OK;
    }

    ch->runtime_ptr->block_transfer_active = TRUE;
    ch->runtime_ptr->block_transfer_type = block_type;

    if (total_size > first_packet_size)
    {
        ch->runtime_ptr->block_transfer_remaining = total_size - first_packet_size;
    }
    else
    {
        ch->runtime_ptr->block_transfer_remaining = 0U;
        ch->runtime_ptr->block_transfer_active = FALSE;
    }

    return E_OK;
}

Std_ReturnType Xcp_ValidateBlockSequence(Xcp_Channel *ch, uint8 block_type, uint32 data_size)
{
    if (ch->runtime_ptr == NULL)
    {
        return E_NOT_OK;
    }

    if (ch->runtime_ptr->block_transfer_active == FALSE)
    {
        return E_NOT_OK;
    }

    if (ch->runtime_ptr->block_transfer_type != block_type)
    {
        Xcp_InitBlockTransfer(ch);
        return E_NOT_OK;
    }

    if (ch->runtime_ptr->block_transfer_remaining >= data_size)
    {
        ch->runtime_ptr->block_transfer_remaining -= data_size;
    }
    else
    {
        ch->runtime_ptr->block_transfer_remaining = 0U;
    }

    if (ch->runtime_ptr->block_transfer_remaining == 0U)
    {
        ch->runtime_ptr->block_transfer_active = FALSE;
        ch->runtime_ptr->block_transfer_type = XCP_BLOCK_TYPE_NONE;
    }

    return E_OK;
}

void Xcp_CancelBlockTransfer(Xcp_Channel *ch)
{
    Xcp_InitBlockTransfer(ch);
}

boolean Xcp_IsBlockTransferActive(Xcp_Channel *ch)
{
    if (ch->runtime_ptr == NULL)
    {
        return FALSE;
    }

    return ch->runtime_ptr->block_transfer_active;
}

boolean Xcp_IsBlockUploadActive(Xcp_Channel *ch)
{
    if (ch->runtime_ptr == NULL)
    {
        return FALSE;
    }

    return (boolean)((ch->runtime_ptr->block_transfer_active != FALSE)
                     && (ch->runtime_ptr->block_transfer_type == XCP_BLOCK_TYPE_UPLOAD));
}

uint32 Xcp_GetBlockTransferRemaining(Xcp_Channel *ch)
{
    if (ch->runtime_ptr == NULL)
    {
        return 0U;
    }

    return ch->runtime_ptr->block_transfer_remaining;
}
