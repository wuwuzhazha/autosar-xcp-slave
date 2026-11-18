#include "Xcp_Resume.h"
#include "Xcp_Internal.h"
#include "Xcp.h"
#include <string.h>
#include <stddef.h>

#if (XCP_RESUME_MODE_ENABLED == STD_ON)

#ifdef NVM_H
#include "NvM.h"
#define XCP_NVM_AVAILABLE STD_ON
#else
#define XCP_NVM_AVAILABLE STD_OFF
#endif

Xcp_ResumeRuntimeType g_xcp_resume_runtime;

Xcp_ResumeDataType g_xcp_resume_data;

static uint16 Xcp_CalculateResumeChecksum(const Xcp_ResumeDataType *data);
static Std_ReturnType Xcp_ValidateResumeData(const Xcp_ResumeDataType *data);
static void Xcp_ApplyRestoredDaqConfig(Xcp_Channel *ch);

static uint16 Xcp_CalculateResumeChecksum(const Xcp_ResumeDataType *data)
{
    uint16 checksum = 0U;
    const uint8 *ptr = (const uint8 *)data;
    uint32 i;

    uint32 offset_checksum = offsetof(Xcp_ResumeDataType, checksum);

    for (i = 0U; i < offset_checksum; i++)
    {
        checksum += ptr[i];
    }

    uint32 offset_after_checksum = offset_checksum + sizeof(data->checksum);
    uint32 total_size = sizeof(Xcp_ResumeDataType);

    for (i = offset_after_checksum; i < total_size; i++)
    {
        checksum += ptr[i];
    }

    return checksum;
}

static Std_ReturnType Xcp_ValidateResumeData(const Xcp_ResumeDataType *data)
{
    uint16 calc_checksum;

    if (data->magic_number != XCP_RESUME_MAGIC_NUMBER)
    {
        return E_NOT_OK;
    }

    if (data->version != XCP_RESUME_DATA_VERSION)
    {
        return E_NOT_OK;
    }

    if (data->daq_count > XCP_MAX_DAQ)
    {
        return E_NOT_OK;
    }

    calc_checksum = Xcp_CalculateResumeChecksum(data);

    if (calc_checksum != data->checksum)
    {
        return E_NOT_OK;
    }

    return E_OK;
}

#if (XCP_DAQ_ENABLED == STD_ON)
static void Xcp_ApplyRestoredDaqConfig(Xcp_Channel *ch)
{
    uint16 i;
    const Xcp_DaqResumeConfigType *resume_cfg;
    Xcp_DaqListType *daq_list;

    for (i = 0U; i < g_xcp_resume_data.daq_count; i++)
    {
        if (i >= XCP_MAX_DAQ)
        {
            break;
        }

        resume_cfg = &g_xcp_resume_data.daq_config[i];

        if (resume_cfg->resume_enabled == TRUE)
        {

            if ((resume_cfg->event_channel >= XCP_MAX_EVENT_CHANNEL) ||
                (resume_cfg->odt_count > XCP_MAX_ODT))
            {
                continue;
            }

            daq_list = &ch->storage_ptr->daq_list[i];

            daq_list->event_channel = resume_cfg->event_channel;
            daq_list->odt_count = (resume_cfg->odt_count <= daq_list->odt_max_count)
                                  ? resume_cfg->odt_count : daq_list->odt_max_count;
            daq_list->mode = resume_cfg->mode;
            daq_list->direction = resume_cfg->direction;
            daq_list->prescaler = resume_cfg->prescaler;
            daq_list->priority = resume_cfg->priority;
            daq_list->state = XCP_DAQ_STATE_CONFIGURED;
            daq_list->resume_pending = TRUE;

            {
                uint8 o, e;
                uint8 max_odts = (daq_list->odt_count <= daq_list->odt_max_count)
                                 ? daq_list->odt_count : daq_list->odt_max_count;

                for (o = 0U; o < max_odts; o++)
                {
                    uint8 stored_entries = g_xcp_resume_data.odt_entry_counts[i][o];
                    daq_list->odt[o].odt_entry_count =
                        (stored_entries <= daq_list->odt[o].odt_entry_max_count)
                        ? stored_entries : daq_list->odt[o].odt_entry_max_count;

                    for (e = 0U; e < daq_list->odt[o].odt_entry_count; e++)
                    {
                        daq_list->odt[o].odt_entry[e].address =
                            g_xcp_resume_data.odt_entries[i][o][e].address;
                        daq_list->odt[o].odt_entry[e].address_extension =
                            g_xcp_resume_data.odt_entries[i][o][e].address_extension;
                        daq_list->odt[o].odt_entry[e].size =
                            g_xcp_resume_data.odt_entries[i][o][e].size;
                        daq_list->odt[o].odt_entry[e].bit_offset =
                            g_xcp_resume_data.odt_entries[i][o][e].bit_offset;
                    }
                }
            }
        }
    }

    ch->runtime_ptr->daq_count = g_xcp_resume_data.daq_count;
}
#endif

void Xcp_InitResumeMode(void)
{
    (void)memset(&g_xcp_resume_runtime, 0, sizeof(Xcp_ResumeRuntimeType));
    g_xcp_resume_runtime.state = XCP_RESUME_STATE_IDLE;

    (void)memset(&g_xcp_resume_data, 0, sizeof(Xcp_ResumeDataType));

#if (XCP_NVM_AVAILABLE == STD_ON)
    g_xcp_resume_runtime.restore_pending = TRUE;
    g_xcp_resume_runtime.state = XCP_RESUME_STATE_PENDING;

    if (NvM_ReadBlock(XCP_NVM_BLOCK_ID_RESUME, &g_xcp_resume_data) == E_OK)
    {

        if (g_xcp_resume_runtime.restore_pending)
        {
            (void)Xcp_RestoreDaqConfig();
            g_xcp_resume_runtime.restore_pending = FALSE;
        }
    }
    else
    {
        g_xcp_resume_runtime.restore_pending = FALSE;
        g_xcp_resume_runtime.state = XCP_RESUME_STATE_IDLE;
    }
#else

    if (Xcp_ValidateResumeData(&g_xcp_resume_data) == E_OK)
    {
        g_xcp_resume_runtime.data_valid = TRUE;
        (void)Xcp_RestoreDaqConfig();
    }
#endif
}

Std_ReturnType Xcp_StoreDaqConfig(Xcp_Channel *ch, uint16 session_cfg_id, boolean resume)
{
    uint16 i;
    Xcp_DaqResumeConfigType *resume_cfg;
    const Xcp_DaqListType *daq_list;

    if ((ch->runtime_ptr == NULL) ||
        (ch->runtime_ptr->state != XCP_INIT))
    {
        return E_NOT_OK;
    }

    g_xcp_resume_data.magic_number = XCP_RESUME_MAGIC_NUMBER;
    g_xcp_resume_data.version = XCP_RESUME_DATA_VERSION;
    g_xcp_resume_data.session_cfg_id = session_cfg_id;
#if (XCP_DAQ_ENABLED == STD_ON)
    g_xcp_resume_data.daq_count = ch->runtime_ptr->daq_count;
    g_xcp_resume_data.resume_active = resume;

    for (i = 0U; i < ch->runtime_ptr->daq_count; i++)
    {
        if (i >= XCP_MAX_DAQ)
        {
            break;
        }

        daq_list = &ch->storage_ptr->daq_list[i];
        resume_cfg = &g_xcp_resume_data.daq_config[i];

        resume_cfg->event_channel = daq_list->event_channel;
        resume_cfg->odt_count = daq_list->odt_count;
        resume_cfg->mode = daq_list->mode;
        resume_cfg->direction = daq_list->direction;
        resume_cfg->prescaler = daq_list->prescaler;
        resume_cfg->priority = daq_list->priority;

        resume_cfg->resume_enabled = ((resume != FALSE) && (daq_list->resume != FALSE)) ? TRUE : FALSE;

        {
            uint8 o, e;

            for (o = 0U; o < XCP_MAX_ODT; o++)
            {
                g_xcp_resume_data.odt_entry_counts[i][o] = daq_list->odt[o].odt_entry_count;

                for (e = 0U; e < XCP_MAX_ODT_ENTRIES; e++)
                {
                    g_xcp_resume_data.odt_entries[i][o][e].address =
                        daq_list->odt[o].odt_entry[e].address;
                    g_xcp_resume_data.odt_entries[i][o][e].address_extension =
                        daq_list->odt[o].odt_entry[e].address_extension;
                    g_xcp_resume_data.odt_entries[i][o][e].size =
                        daq_list->odt[o].odt_entry[e].size;
                    g_xcp_resume_data.odt_entries[i][o][e].bit_offset =
                        daq_list->odt[o].odt_entry[e].bit_offset;
                }
            }
        }
    }
#endif

    g_xcp_resume_data.checksum = Xcp_CalculateResumeChecksum(&g_xcp_resume_data);

#if (XCP_NVM_AVAILABLE == STD_ON)
    if (NvM_WriteBlock(XCP_NVM_BLOCK_ID_RESUME, &g_xcp_resume_data) == E_OK)
    {
        g_xcp_resume_runtime.store_pending = TRUE;
        g_xcp_resume_runtime.state = XCP_RESUME_STATE_PENDING;

        ch->runtime_ptr->session_status |= XCP_SS_STORE_DAQ_REQ;
        return E_OK;
    }

    return E_NOT_OK;
#else

    g_xcp_resume_runtime.data_valid = TRUE;
    (void)Xcp_SendEventPacket(XCP_EV_STORE_DAQ, NULL, 0U);
    return E_OK;
#endif
}

Std_ReturnType Xcp_RestoreDaqConfig(void)
{
    if (Xcp_ValidateResumeData(&g_xcp_resume_data) != E_OK)
    {
        g_xcp_resume_runtime.data_valid = FALSE;
        g_xcp_resume_runtime.state = XCP_RESUME_STATE_IDLE;
        return E_NOT_OK;
    }

    if (g_xcp_resume_data.resume_active != TRUE)
    {
        g_xcp_resume_runtime.state = XCP_RESUME_STATE_IDLE;
        return E_NOT_OK;
    }

#if (XCP_DAQ_ENABLED == STD_ON)
    Xcp_ApplyRestoredDaqConfig(&g_xcp_channel);
#endif

    g_xcp_resume_runtime.data_valid = TRUE;
    g_xcp_resume_runtime.state = XCP_RESUME_STATE_ACTIVE;

    (void)Xcp_SendEventPacket(XCP_EV_RESUME_MODE, NULL, 0U);
    return E_OK;
}

Std_ReturnType Xcp_ClearStoredDaqConfig(Xcp_Channel *ch)
{
    (void)memset(&g_xcp_resume_data, 0, sizeof(Xcp_ResumeDataType));

    g_xcp_resume_runtime.data_valid = FALSE;
    g_xcp_resume_runtime.state = XCP_RESUME_STATE_IDLE;

#if (XCP_NVM_AVAILABLE == STD_ON)
    if (NvM_InvalidateNvBlock(XCP_NVM_BLOCK_ID_RESUME) == E_OK)
    {
        return E_OK;
    }

    return E_NOT_OK;
#else
    return E_OK;
#endif
}

boolean Xcp_IsDaqListResume(Xcp_Channel *ch, uint16 daq_list_no)
{
    return ((g_xcp_resume_runtime.data_valid != FALSE)
            && (g_xcp_resume_data.resume_active == TRUE)
            && (daq_list_no < g_xcp_resume_data.daq_count)
            && (daq_list_no < XCP_MAX_DAQ)
            && (g_xcp_resume_data.daq_config[daq_list_no].resume_enabled == TRUE)) ? TRUE : FALSE;
}

boolean Xcp_IsResumeModeActive(Xcp_Channel *ch)
{
    return (g_xcp_resume_runtime.state == XCP_RESUME_STATE_ACTIVE);
}

void Xcp_ResumeMainFunction(void)
{

    if (g_xcp_resume_runtime.restore_pending)
    {
        g_xcp_resume_runtime.restore_pending = FALSE;
        (void)Xcp_RestoreDaqConfig();
    }

}

void Xcp_NvMReadRamBlockFromNvmCallback(uint16 block_id, uint8 result)
{
    if (block_id != XCP_NVM_BLOCK_ID_RESUME)
    {
        return;
    }

    g_xcp_resume_runtime.restore_pending = FALSE;

    if (result == 0U)
    {
        (void)Xcp_RestoreDaqConfig();
    }
    else
    {
        g_xcp_resume_runtime.data_valid = FALSE;
        g_xcp_resume_runtime.state = XCP_RESUME_STATE_IDLE;
    }
}

void Xcp_NvMWriteRamBlockToNvmCallback(uint16 block_id, uint8 result)
{
    if (block_id != XCP_NVM_BLOCK_ID_RESUME)
    {
        return;
    }

    g_xcp_resume_runtime.store_pending = FALSE;
    if (g_xcp_channel.runtime_ptr != NULL_PTR)
    {
        g_xcp_channel.runtime_ptr->session_status &= (uint8) ~XCP_SS_STORE_DAQ_REQ;
    }

    if (result == 0U)
    {
        g_xcp_resume_runtime.data_valid = TRUE;

        (void)Xcp_SendEventPacket(XCP_EV_STORE_DAQ, NULL, 0U);
    }
    else
    {
        g_xcp_resume_runtime.state = XCP_RESUME_STATE_ERROR;
    }
}

#endif
