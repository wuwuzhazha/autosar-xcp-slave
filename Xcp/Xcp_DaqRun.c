#include "Xcp_DaqInt.h"

#include <string.h>

#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Resume.h"

#if (XCP_DAQ_ENABLED == STD_ON)

uint16 Xcp_ActiveDaqIndices[XCP_MAX_DAQ];
uint16 Xcp_ActiveDaqCount = 0;

#if (XCP_ENABLE_DTO_DROP_STATS == STD_ON)
static uint32 dropped_dto_counter = 0;
#endif

static boolean dto_overload_pending = FALSE;
static uint16  overload_ev_cooldown = 0U;
static uint16  dto_busy_watchdog_cnt = 0U;

void Xcp_RebuildActiveDaqList(void)
{
    uint16 i;
    Xcp_ActiveDaqCount = 0;

    for (i = 0U; i < g_xcp_channel.runtime_ptr->daq_count; i++)
    {
        if (g_xcp_channel.storage_ptr->daq_list[i].state == (uint8) XCP_DAQ_STATE_RUNNING)
        {
            Xcp_ActiveDaqIndices[Xcp_ActiveDaqCount] = i;
            Xcp_ActiveDaqCount++;
        }
    }
}

void Xcp_ResetEventChannelCounters(void)
{
    uint16 i;

    for (i = 0U; i < XCP_MAX_EVENT_CHANNEL; i++)
    {
        g_xcp_channel.runtime_ptr->event_channel_counter[i] = 0U;
    }

#if (XCP_PRESCALER_SUPPORTED == STD_ON)
    for (i = 0U; i < g_xcp_channel.runtime_ptr->daq_count; i++)
    {
        g_xcp_channel.storage_ptr->daq_list[i].prescaler_counter = 0U;
    }
#endif
}

void Xcp_ProcessDaq(uint16 event_channel)
{
    uint16 i;
    uint16 daqIdx;
    uint8  odtIdx;

    for (i = 0U; i < Xcp_ActiveDaqCount; i++)
    {
        daqIdx = Xcp_ActiveDaqIndices[i];
        Xcp_DaqListType *daq = &g_xcp_channel.storage_ptr->daq_list[daqIdx];

        if ((daq->event_channel == event_channel) && (daq->state == XCP_DAQ_STATE_RUNNING)
            && (daq->direction == XCP_DAQ_LIST_TYPE_DAQ))
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
            for (odtIdx = 0U; odtIdx < daq->odt_count; odtIdx++)
            {
                Xcp_TransmitDto(daqIdx, odtIdx);
            }
        }
    }
}

void Xcp_TransmitDto(uint16 daq_list_no, uint8 odt_no)
{
    Xcp_OdtType      *odt;
    Xcp_OdtEntryType *entry;
    uint8             entry_idx;
    uint8             buffer_idx;
    uint8            *src_ptr;
    uint8             local_dto_buffer[XCP_MAX_DTO];
    uint8             local_buffer_length;

    if (daq_list_no >= g_xcp_channel.runtime_ptr->daq_count)
    {
        return;
    }

    if (odt_no >= g_xcp_channel.storage_ptr->daq_list[daq_list_no].odt_count)
    {
        return;
    }

    odt = &g_xcp_channel.storage_ptr->daq_list[daq_list_no].odt[odt_no];

    (void) memset(&local_dto_buffer[0], 0x00, XCP_MAX_DTO);

    buffer_idx = 0U;

#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)

    {
        local_dto_buffer[buffer_idx] = (uint8) (Xcp_AbsPidEncode(daq_list_no, odt_no) & 0xFFU);
        buffer_idx++;
    }
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_BYTE)

    local_dto_buffer[buffer_idx] = odt_no;
    buffer_idx++;
    local_dto_buffer[buffer_idx] = (uint8) daq_list_no;
    buffer_idx++;
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_WORD)

    local_dto_buffer[buffer_idx] = odt_no;
    buffer_idx++;
    local_dto_buffer[buffer_idx] = (uint8) (daq_list_no & 0xFFU);
    buffer_idx++;
    local_dto_buffer[buffer_idx] = (uint8) ((daq_list_no >> 8U) & 0xFFU);
    buffer_idx++;
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_WORD_ALIGNED)

    local_dto_buffer[buffer_idx] = odt_no;
    buffer_idx++;
    local_dto_buffer[buffer_idx] = 0x00U;
    buffer_idx++;
    local_dto_buffer[buffer_idx] = (uint8) (daq_list_no & 0xFFU);
    buffer_idx++;
    local_dto_buffer[buffer_idx] = (uint8) ((daq_list_no >> 8U) & 0xFFU);
    buffer_idx++;
#endif

#if (XCP_TIMESTAMP_TYPE != XCP_NO_TIME_STAMP)

    if ((odt_no == 0U) && XCP_DAQ_HAS_TIMESTAMP(&g_xcp_channel.storage_ptr->daq_list[daq_list_no]))
    {
        uint32 ts = Xcp_GetTimestamp();
#if (XCP_TIMESTAMP_TYPE == XCP_FOUR_BYTE)
        local_dto_buffer[buffer_idx++] = (uint8) (ts & 0xFFU);
        local_dto_buffer[buffer_idx++] = (uint8) ((ts >> 8U) & 0xFFU);
        local_dto_buffer[buffer_idx++] = (uint8) ((ts >> 16U) & 0xFFU);
        local_dto_buffer[buffer_idx++] = (uint8) ((ts >> 24U) & 0xFFU);
#elif (XCP_TIMESTAMP_TYPE == XCP_TWO_BYTE)
        local_dto_buffer[buffer_idx++] = (uint8) (ts & 0xFFU);
        local_dto_buffer[buffer_idx++] = (uint8) ((ts >> 8U) & 0xFFU);
#elif (XCP_TIMESTAMP_TYPE == XCP_ONE_BYTE)
        local_dto_buffer[buffer_idx++] = (uint8) (ts & 0xFFU);
#endif
    }
#endif

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

        if ((buffer_idx + elem_len) > XCP_MAX_DTO)
        {
            break;
        }

        src_ptr = (uint8 *) entry->address;

        if (is_bit != FALSE)
        {
            uint32 byte_addr = entry->address + ((uint32) entry->bit_offset >> 3U);

            local_dto_buffer[buffer_idx] = 0x00U;
            if (XCP_CHECK_READ_ACCESS_EX(entry->address_extension, byte_addr, 1U) == 0U)
            {
                local_dto_buffer[buffer_idx] =
                    (uint8) ((src_ptr[entry->bit_offset >> 3U] >> (entry->bit_offset & 0x07U)) & 0x01U);
            }
        }
        else if (XCP_CHECK_READ_ACCESS_EX(entry->address_extension, entry->address, entry->size) != 0U)
        {
            (void) memset(&local_dto_buffer[buffer_idx], 0x00, entry->size);
        }
        else
        {
            (void) memcpy(&local_dto_buffer[buffer_idx], src_ptr, entry->size);
        }

        buffer_idx += elem_len;
    }

    local_buffer_length = buffer_idx;

    if (local_buffer_length < XCP_MAX_DTO)
    {
        local_buffer_length = XCP_MAX_DTO;
    }

    Xcp_DtoSendOrQueue(local_dto_buffer, local_buffer_length);
}

void Xcp_DtoSendOrQueue(const uint8 *dto_data, uint8 dto_length)
{
    Xcp_StorageType *storage = g_xcp_channel.storage_ptr;
    Xcp_RuntimeType *runtime = g_xcp_channel.runtime_ptr;

    XCP_DTO_ENTER_CRITICAL();

    if (runtime->tx_mode == XCP_TX_OFF)
    {
        XCP_DTO_EXIT_CRITICAL();
        return;
    }

    if (runtime->tx.state == XCP_TX_IDLE)
    {

        runtime->tx.dto_latch = TRUE;
        runtime->tx.state     = XCP_TX_DTO;

        if (Xcp_Tl_SendDto(dto_data, dto_length) == E_OK)
        {
            XCP_DTO_EXIT_CRITICAL();
            return;
        }

        runtime->tx.dto_latch = FALSE;
        runtime->tx.state     = XCP_TX_IDLE;
    }

    if (storage->dto_queue_count < (uint8) XCP_MAX_ODT)
    {
        uint8 head = storage->dto_queue_head;
        (void) memcpy(storage->dto_queue[head], dto_data, (uint8) XCP_MAX_DTO);
        storage->dto_queue_head = (head + 1U) % (uint8) XCP_MAX_ODT;
        storage->dto_queue_count++;
    }
    else
    {
#if (XCP_ENABLE_DTO_DROP_STATS == STD_ON)
        dropped_dto_counter++;
#endif

        dto_overload_pending = TRUE;
    }

    XCP_DTO_EXIT_CRITICAL();
}

void Xcp_DtoSendNext(void)
{
    Xcp_StorageType *storage = g_xcp_channel.storage_ptr;
    Xcp_RuntimeType *runtime = g_xcp_channel.runtime_ptr;

    runtime->tx.dto_latch = FALSE;

    if (runtime->tx_mode == XCP_TX_OFF)
    {
        return;
    }

    if (runtime->tx.state != XCP_TX_IDLE)
    {
        return;
    }

    if (storage->dto_queue_count > 0U)
    {
        uint8 tail = storage->dto_queue_tail;
        uint8 dto_copy[XCP_MAX_DTO];

        (void)memcpy(dto_copy, storage->dto_queue[tail], (uint8) XCP_MAX_DTO);
        storage->dto_queue_tail = (tail + 1U) % (uint8) XCP_MAX_ODT;
        storage->dto_queue_count--;

        runtime->tx.dto_latch = TRUE;
        runtime->tx.state     = XCP_TX_DTO;

        if (Xcp_Tl_SendDto(dto_copy, (uint8) XCP_MAX_DTO) != E_OK)
        {
            runtime->tx.dto_latch = FALSE;
            runtime->tx.state     = XCP_TX_IDLE;
            storage->dto_queue_tail = tail;
            storage->dto_queue_count++;
        }
    }
}

void Xcp_ReportDtoOverload(void)
{
    uint16           i;
    Xcp_StorageType *storage = g_xcp_channel.storage_ptr;
    Xcp_RuntimeType *runtime = g_xcp_channel.runtime_ptr;

    XCP_DTO_ENTER_CRITICAL();

    if (runtime->tx.dto_latch != FALSE)
    {
        if (dto_busy_watchdog_cnt < XCP_DTO_BUSY_TIMEOUT_CYCLES)
        {
            dto_busy_watchdog_cnt++;
        }
        else
        {

            if (runtime->tx.state == XCP_TX_DTO)
            {
                runtime->tx.state = XCP_TX_IDLE;
            }

            runtime->tx.dto_latch = FALSE;
            dto_busy_watchdog_cnt = 0U;
        }
    }
    else
    {
        dto_busy_watchdog_cnt = 0U;
    }

    if ((runtime->tx_mode == XCP_TX_ON) &&
        (runtime->tx.parked != FALSE) && (runtime->tx.state == XCP_TX_IDLE))
    {
        Xcp_SendPendingCto();
    }

    if ((runtime->tx_mode == XCP_TX_ON) &&
        (runtime->tx.state == XCP_TX_IDLE) && (storage->dto_queue_count > 0U))
    {
        uint8 tail = storage->dto_queue_tail;
        uint8 dto_copy[XCP_MAX_DTO];

        (void)memcpy(dto_copy, storage->dto_queue[tail], (uint8) XCP_MAX_DTO);
        storage->dto_queue_tail = (tail + 1U) % (uint8) XCP_MAX_ODT;
        storage->dto_queue_count--;

        runtime->tx.dto_latch = TRUE;
        runtime->tx.state     = XCP_TX_DTO;

        if (Xcp_Tl_SendDto(dto_copy, (uint8) XCP_MAX_DTO) != E_OK)
        {
            runtime->tx.dto_latch = FALSE;
            runtime->tx.state     = XCP_TX_IDLE;
            storage->dto_queue_tail = tail;
            storage->dto_queue_count++;
        }
    }

    XCP_DTO_EXIT_CRITICAL();

    if (overload_ev_cooldown > 0U)
    {
        overload_ev_cooldown--;
        return;
    }

    if (dto_overload_pending != TRUE)
    {
        return;
    }

    for (i = 0U; i < XCP_MAX_DAQ; i++)
    {
        if (g_xcp_channel.storage_ptr->daq_list[i].state == (uint8) XCP_DAQ_STATE_RUNNING)
        {
            break;
        }
    }

    if (i >= XCP_MAX_DAQ)
    {

        dto_overload_pending = FALSE;
        return;
    }

    if (Xcp_SendDaqOverloadEvent(0xFFFFU) == E_OK)
    {
        dto_overload_pending = FALSE;

        overload_ev_cooldown = (uint16) (1000U / XCP_MAIN_FUNCTION_PERIOD_MS);
    }
    else
    {

        overload_ev_cooldown = 2U;
    }
}

#endif
