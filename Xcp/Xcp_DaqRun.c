#include "Xcp_DaqInt.h"

#include <string.h>

#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Resume.h"

#if (XCP_DAQ_ENABLED == STD_ON)

void Xcp_RebuildActiveDaqList(Xcp_Channel *ch)
{
    uint16 i;
    ch->storage_ptr->active_daq_count = 0U;

    for (i = 0U; i < ch->runtime_ptr->daq_count; i++)
    {
        if (ch->storage_ptr->daq_list[i].state == (uint8) XCP_DAQ_STATE_RUNNING)
        {
            ch->storage_ptr->active_daq_idx[ch->storage_ptr->active_daq_count] = i;
            ch->storage_ptr->active_daq_count++;
        }
    }
}

void Xcp_ResetEventChannelCounters(Xcp_Channel *ch)
{
    uint16 i;

    for (i = 0U; i < XCP_MAX_EVENT_CHANNEL; i++)
    {
        ch->runtime_ptr->event_channel_counter[i] = 0U;
    }

#if (XCP_PRESCALER_SUPPORTED == STD_ON)
    for (i = 0U; i < ch->runtime_ptr->daq_count; i++)
    {
        ch->storage_ptr->daq_list[i].prescaler_counter = 0U;
    }
#endif
}

void Xcp_ProcessDaq(Xcp_Channel *ch, uint16 event_channel)
{
    uint16 i;
    uint16 daqIdx;
    uint8  odtIdx;

    for (i = 0U; i < ch->storage_ptr->active_daq_count; i++)
    {
        daqIdx = ch->storage_ptr->active_daq_idx[i];
        Xcp_DaqListType *daq = &ch->storage_ptr->daq_list[daqIdx];

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
            /* An absolute PID carries no cycle number, so a second copy of the
               same ODT set in the same window reads as a duplicate to the
               master. Hold the list until its previous burst has drained. */
            if (ch->storage_ptr->dto_queued[daqIdx] != 0U)
            {
                continue;
            }

            for (odtIdx = 0U; odtIdx < daq->odt_count; odtIdx++)
            {
                Xcp_TransmitDto(&g_xcp_channel, daqIdx, odtIdx);
            }
        }
    }
}

void Xcp_TransmitDto(Xcp_Channel *ch, uint16 daq_list_no, uint8 odt_no)
{
    Xcp_OdtType      *odt;
    Xcp_OdtEntryType *entry;
    uint8             entry_idx;
    uint8             buffer_idx;
    uint8            *src_ptr;
    uint8             local_dto_buffer[XCP_MAX_DTO];
    uint8             local_buffer_length;

    if (daq_list_no >= ch->runtime_ptr->daq_count)
    {
        return;
    }

    if (odt_no >= ch->storage_ptr->daq_list[daq_list_no].odt_count)
    {
        return;
    }

    odt = &ch->storage_ptr->daq_list[daq_list_no].odt[odt_no];

    (void) memset(&local_dto_buffer[0], 0x00, XCP_MAX_DTO);

    buffer_idx = Xcp_PidFieldEncode(&g_xcp_channel, daq_list_no, odt_no, &local_dto_buffer[0]);

    if ((odt_no == 0U) && XCP_DAQ_HAS_TIMESTAMP(&ch->storage_ptr->daq_list[daq_list_no]))
    {
        buffer_idx += Xcp_WriteTimestamp(Xcp_GetTimestamp(), &local_dto_buffer[buffer_idx]);
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

        if ((buffer_idx + elem_len) > XCP_MAX_DTO)
        {
            break;
        }

        if (is_bit != FALSE)
        {
            uint8 byte_idx = (uint8) (entry->bit_offset >> 3U);

            src_ptr = Xcp_MemPtrRead(ch, entry->address_extension,
                                     entry->address + byte_idx, 1U);
            local_dto_buffer[buffer_idx] = 0x00U;
            if (src_ptr != NULL_PTR)
            {
                local_dto_buffer[buffer_idx] =
                    (uint8) ((src_ptr[0] >> (entry->bit_offset & 0x07U)) & 0x01U);
            }
        }
        else
        {
            src_ptr = Xcp_MemPtrRead(ch, entry->address_extension, entry->address, entry->size);
            if (src_ptr != NULL_PTR)
            {
                (void) memcpy(&local_dto_buffer[buffer_idx], src_ptr, entry->size);
            }
            else
            {
                (void) memset(&local_dto_buffer[buffer_idx], 0x00, entry->size);
            }
        }

        buffer_idx += elem_len;
    }

    local_buffer_length = buffer_idx;

    Xcp_DtoSendOrQueue(&g_xcp_channel, daq_list_no, local_dto_buffer, local_buffer_length);
}

void Xcp_DtoSendOrQueue(Xcp_Channel *ch, uint16 daq_list_no, const uint8 *dto_data, uint8 dto_length)
{
    Xcp_StorageType *storage = ch->storage_ptr;
    Xcp_RuntimeType *runtime = ch->runtime_ptr;

    XCP_DTO_ENTER_CRITICAL();

    if (runtime->tx_mode == XCP_TX_OFF)
    {
        XCP_DTO_EXIT_CRITICAL();
        return;
    }

    if (storage->dto_queue_count < (uint8) XCP_DTO_QUEUE_DEPTH)
    {
        uint8 head = storage->dto_queue_head;

        (void) memcpy(storage->dto_queue[head], dto_data, dto_length);
        storage->dto_queue_len[head]  = dto_length;
        storage->dto_queue_list[head] = daq_list_no;
        storage->dto_queued[daq_list_no]++;
        storage->dto_queue_head = (head + 1U) % (uint8) XCP_DTO_QUEUE_DEPTH;
        storage->dto_queue_count++;
    }
    else
    {
#if (XCP_ENABLE_DTO_DROP_STATS == STD_ON)
        storage->dropped_dto_counter++;
#endif

        storage->dto_overload_pending = TRUE;
    }

    XCP_DTO_EXIT_CRITICAL();

    /* Every frame takes the same path through the queue, so the order it
       leaves in is the order it was produced in however the pipe treated an
       earlier frame. */
    Xcp_DtoSendNext(ch);
}

/* Hands the queued tail DTO to the lower layer. The slot stays reserved while
   the call runs, so a failed send needs no rewind and keeps its FIFO place.
   Returns TRUE only when the frame is both sent and already confirmed, so the
   caller may drain again. */
static boolean Xcp_DtoSendQueued(Xcp_Channel *ch)
{
    Xcp_StorageType *storage = ch->storage_ptr;
    Xcp_RuntimeType *runtime = ch->runtime_ptr;

    uint8   dto_copy[XCP_MAX_DTO];
    uint8   tail;
    uint8   dto_len;
    boolean sent      = FALSE;
    boolean confirmed = FALSE;

    XCP_DTO_ENTER_CRITICAL();

    if (runtime->tx.dto_in_flight != FALSE)
    {
        XCP_DTO_EXIT_CRITICAL();
        return FALSE;
    }

    /* The latch and the watchdog behind it belong to the frame that owns the
       pipe, so a drain that finds the pipe busy must leave both alone even
       when the owning frame is only one the caller simulated. */
    if ((runtime->tx_mode != XCP_TX_ON) || (runtime->tx.state != XCP_TX_IDLE))
    {
        XCP_DTO_EXIT_CRITICAL();
        return FALSE;
    }

    runtime->tx.dto_latch = FALSE;

    if (storage->dto_queue_count == 0U)
    {
        XCP_DTO_EXIT_CRITICAL();
        return FALSE;
    }

    tail    = storage->dto_queue_tail;
    dto_len = storage->dto_queue_len[tail];
    (void)memcpy(dto_copy, storage->dto_queue[tail], dto_len);

    storage->dto_busy_watchdog_cnt = 0U;
    runtime->tx.dto_latch     = TRUE;
    runtime->tx.state         = XCP_TX_DTO;
    runtime->tx.dto_in_flight = TRUE;

    XCP_DTO_EXIT_CRITICAL();

    sent = (Xcp_Tl_SendDto(dto_copy, dto_len) == E_OK) ? TRUE : FALSE;

    XCP_DTO_ENTER_CRITICAL();

    runtime->tx.dto_in_flight = FALSE;
    /* A confirmation that lands inside the call retires the pipe first and
       must not release or resend the slot this call still owns. */
    confirmed = (runtime->tx.state != XCP_TX_DTO) ? TRUE : FALSE;

    if (sent != FALSE)
    {
        storage->dto_queued[storage->dto_queue_list[tail]]--;
        storage->dto_queue_tail = (uint8) ((tail + 1U) % (uint8) XCP_DTO_QUEUE_DEPTH);
        storage->dto_queue_count--;
    }
    else
    {
        runtime->tx.dto_latch = FALSE;

        if (confirmed == FALSE)
        {
            runtime->tx.state = XCP_TX_IDLE;
        }
    }

    XCP_DTO_EXIT_CRITICAL();

    return ((sent != FALSE) && (confirmed != FALSE)) ? TRUE : FALSE;
}

void Xcp_DtoSendNext(Xcp_Channel *ch)
{
    /* Drain while every frame is retired before its transmit call returns; an
       unconfirmed frame stops the burst until its confirmation arrives. */
    while (Xcp_DtoSendQueued(ch) != FALSE)
    {
    }
}

void Xcp_ReportDtoOverload(Xcp_Channel *ch)
{
    uint16           i;
    Xcp_StorageType *storage = ch->storage_ptr;
    Xcp_RuntimeType *runtime = ch->runtime_ptr;
    boolean          send_pending_cto = FALSE;
    boolean          send_dto = FALSE;

    XCP_DTO_ENTER_CRITICAL();

    if (runtime->tx.dto_latch != FALSE)
    {
        if (storage->dto_busy_watchdog_cnt < XCP_DTO_BUSY_TIMEOUT_CYCLES)
        {
            storage->dto_busy_watchdog_cnt++;
        }
        else
        {

            if (runtime->tx.state == XCP_TX_DTO)
            {
                runtime->tx.state = XCP_TX_IDLE;
            }

            runtime->tx.dto_latch = FALSE;
            storage->dto_busy_watchdog_cnt = 0U;
        }
    }
    else
    {
        storage->dto_busy_watchdog_cnt = 0U;
    }

    if ((runtime->tx_mode == XCP_TX_ON) && (runtime->tx.state == XCP_TX_IDLE))
    {
        if (runtime->tx.cto_count > 0U)
        {
            send_pending_cto = TRUE;
        }
        else if (storage->dto_queue_count > 0U)
        {
            send_dto = TRUE;
        }
    }

    XCP_DTO_EXIT_CRITICAL();

    if (send_pending_cto != FALSE)
    {
        Xcp_SendPendingCto();
    }
    else if (send_dto != FALSE)
    {
        (void)Xcp_DtoSendQueued(ch);
    }

    if (storage->overload_ev_cooldown > 0U)
    {
        storage->overload_ev_cooldown--;
        return;
    }

    if (storage->dto_overload_pending != TRUE)
    {
        return;
    }

    for (i = 0U; i < XCP_MAX_DAQ; i++)
    {
        if (ch->storage_ptr->daq_list[i].state == (uint8) XCP_DAQ_STATE_RUNNING)
        {
            break;
        }
    }

    if (i >= XCP_MAX_DAQ)
    {

        storage->dto_overload_pending = FALSE;
        return;
    }

    if (Xcp_SendDaqOverloadEvent(0xFFFFU) == E_OK)
    {
        storage->dto_overload_pending = FALSE;

        storage->overload_ev_cooldown = (uint16) (1000U / XCP_MAIN_FUNCTION_PERIOD_MS);
    }
    else
    {

        storage->overload_ev_cooldown = 2U;
    }
}

#endif
