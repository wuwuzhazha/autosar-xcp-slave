#include "Xcp_DaqInt.h"

#include <string.h>

#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Resume.h"

#if (XCP_DAQ_ENABLED == STD_ON)

void Xcp_ProcessStim(uint16 event_channel)
{
    uint16           i;
    uint16           daqIdx;
    Xcp_DaqListType *daq;

    for (i = 0U; i < Xcp_ActiveDaqCount; i++)
    {
        daqIdx = Xcp_ActiveDaqIndices[i];
        daq = &g_xcp_channel.storage_ptr->daq_list[daqIdx];

        if ((daq->event_channel == event_channel) && (daq->state == XCP_DAQ_STATE_RUNNING)
            && (daq->direction == XCP_DAQ_LIST_TYPE_STIM))
        {
#if (XCP_PRESCALER_SUPPORTED == STD_ON)
            uint8 prescaler = daq->prescaler;

            if (prescaler > 1U)
            {
                daq->prescaler_counter++;

                if (daq->prescaler_counter < (uint32) prescaler)
                {
                    continue;
                }

                daq->prescaler_counter = 0U;
            }
#endif

        }
    }
}

Std_ReturnType Xcp_ReceiveStimDto(const uint8 *dto_data, uint8 dto_length)
{
    uint16            daq_list_no;
    uint8             odt_no;
    uint8             data_idx;
    uint8             entry_idx;
    Xcp_OdtType      *odt;
    Xcp_OdtEntryType *entry;
    uint8            *dst_ptr;

    if (dto_data == NULL)
    {
        return E_NOT_OK;
    }

    if (Xcp_PidFieldDecode(&g_xcp_channel, dto_data, dto_length, &daq_list_no, &odt_no, &data_idx) != E_OK)
    {
        return E_NOT_OK;
    }

    if (daq_list_no >= g_xcp_channel.runtime_ptr->daq_count)
    {
        return E_NOT_OK;
    }

    if (g_xcp_channel.storage_ptr->daq_list[daq_list_no].direction != XCP_DAQ_LIST_TYPE_STIM)
    {
        return E_NOT_OK;
    }

    if (g_xcp_channel.storage_ptr->daq_list[daq_list_no].state != XCP_DAQ_STATE_RUNNING)
    {
        return E_NOT_OK;
    }

    if (odt_no >= g_xcp_channel.storage_ptr->daq_list[daq_list_no].odt_count)
    {
        return E_NOT_OK;
    }

    odt = &g_xcp_channel.storage_ptr->daq_list[daq_list_no].odt[odt_no];

    if ((odt_no == 0U) && XCP_DAQ_HAS_TIMESTAMP(&g_xcp_channel.storage_ptr->daq_list[daq_list_no]))
    {
        data_idx += Xcp_TimestampSize();
    }
    for (entry_idx = 0U; entry_idx < odt->odt_entry_count; entry_idx++)
    {
        boolean is_bit;
        uint8   elem_len;

        entry = &odt->odt_entry[entry_idx];

        if (entry->size == 0U)
        {
            continue;
        }

        is_bit   = (entry->bit_offset <= XCP_BIT_OFFSET_MAX) ? TRUE : FALSE;
        elem_len = (is_bit != FALSE) ? 1U : entry->size;

        if ((data_idx + elem_len) > dto_length)
        {
            break;
        }

        dst_ptr = (uint8 *) entry->address;

        if (is_bit != FALSE)
        {

            uint8  byte_idx  = (uint8) (entry->bit_offset >> 3U);
            uint8  mask      = (uint8) (1U << (entry->bit_offset & 0x07U));
            uint32 byte_addr = entry->address + byte_idx;

            if (XCP_CHECK_WRITE_ACCESS_EX(entry->address_extension, byte_addr, 1U) == 0U)
            {
                if ((dto_data[data_idx] & 0x01U) != 0U)
                {
                    dst_ptr[byte_idx] = (uint8) (dst_ptr[byte_idx] | mask);
                }
                else
                {
                    dst_ptr[byte_idx] = (uint8) (dst_ptr[byte_idx] & (uint8)(~mask));
                }
            }
        }
        else if (XCP_CHECK_WRITE_ACCESS_EX(entry->address_extension, entry->address, entry->size) == 0U)
        {
            (void) memcpy(dst_ptr, &dto_data[data_idx], entry->size);
        }
        else
        {

        }

        data_idx += elem_len;
    }

#if (XCP_STIM_TIMEOUT_ENABLED == STD_ON)
    Xcp_ResetStimTimeout(daq_list_no);
#endif

    return E_OK;
}

#endif

#if (XCP_DAQ_ENABLED == STD_ON) && (XCP_STIM_TIMEOUT_ENABLED == STD_ON)

void Xcp_InitStimTimeout(void)
{
    uint16 i;

    for (i = 0U; i < XCP_MAX_DAQ; i++)
    {
        g_xcp_channel.runtime_ptr->stim_last_received[i] = 0U;
        g_xcp_channel.runtime_ptr->stim_timeout_flag[i] = FALSE;
    }
}

void Xcp_ResetStimTimeout(uint16 daq_list_no)
{
    if (daq_list_no >= XCP_MAX_DAQ)
    {
        return;
    }

    g_xcp_channel.runtime_ptr->stim_last_received[daq_list_no] = g_xcp_channel.runtime_ptr->timestamp;

    g_xcp_channel.runtime_ptr->stim_timeout_flag[daq_list_no] = FALSE;
}

void Xcp_CheckStimTimeout(void)
{
    uint16           i;
    uint32           current_timestamp;
    uint32           elapsed_time;
    uint32           timeout_ticks;
    Xcp_DaqListType *daq;

    if ((g_xcp_channel.runtime_ptr == NULL) || (g_xcp_channel.runtime_ptr->state != XCP_INIT)
        || (g_xcp_channel.runtime_ptr->connection_state != XCP_CONNECTED))
    {
        return;
    }

    current_timestamp = g_xcp_channel.runtime_ptr->timestamp;

    timeout_ticks = XCP_STIM_TIMEOUT_MS / XCP_MAIN_FUNCTION_PERIOD_MS;

    if (timeout_ticks == 0U)
    {
        timeout_ticks = 1U;
    }

    for (i = 0U; i < g_xcp_channel.runtime_ptr->daq_count; i++)
    {
        daq = &g_xcp_channel.storage_ptr->daq_list[i];

        if ((daq->state == XCP_DAQ_STATE_RUNNING) && (daq->direction == XCP_DAQ_LIST_TYPE_STIM))
        {
            if (current_timestamp >= g_xcp_channel.runtime_ptr->stim_last_received[i])
            {
                elapsed_time = current_timestamp - g_xcp_channel.runtime_ptr->stim_last_received[i];
            }
            else
            {
                elapsed_time
                    = (0xFFFFFFFFU - g_xcp_channel.runtime_ptr->stim_last_received[i]) + current_timestamp + 1U;
            }

            if ((elapsed_time >= timeout_ticks) && (g_xcp_channel.runtime_ptr->stim_timeout_flag[i] == FALSE))
            {

                g_xcp_channel.runtime_ptr->stim_timeout_flag[i] = TRUE;
                {
                    uint8 event_info[2];
                    event_info[0] = (uint8) (i & 0xFFU);
                    event_info[1] = (uint8) ((i >> 8U) & 0xFFU);

                    (void) Xcp_SendEventPacket(XCP_EV_STIM_TIMEOUT, event_info, 2U);
                }

            }
            else
            {
            }
        }
    }
}

#endif
