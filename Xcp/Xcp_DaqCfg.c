#include "Xcp_DaqInt.h"

#include <string.h>

#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Resume.h"

#if (XCP_DAQ_ENABLED == STD_ON)

#if (XCP_MIN_DAQ > 0U)
static void Xcp_LoadStaticDaqLists(Xcp_Channel *ch);
#endif

void Xcp_InitDaqLists(Xcp_Channel *ch)
{
    uint16 daq_idx = 0;
    uint8  odt_idx = 0;
    uint16 odt_pool_idx = 0U;
    uint16 entry_pool_idx = 0U;

    ch->runtime_ptr->daq_list = (Xcp_DaqListType *) &ch->storage_ptr->daq_list[0];
#if (XCP_MIN_DAQ > 0U)
    ch->runtime_ptr->daq_count = XCP_MIN_DAQ;
#else
    ch->runtime_ptr->daq_count = 0U;
#endif
    for (daq_idx = 0U; daq_idx < XCP_MAX_DAQ; daq_idx++)
    {
        ch->storage_ptr->daq_list[daq_idx].odt
            = (Xcp_OdtType *) &ch->storage_ptr->odt[odt_pool_idx];
        ch->storage_ptr->daq_list[daq_idx].odt_count = 0U;
        ch->storage_ptr->daq_list[daq_idx].odt_max_count = XCP_MAX_ODT;
        ch->storage_ptr->daq_list[daq_idx].state = XCP_DAQ_STATE_FREE;

        for (odt_idx = 0U; odt_idx < XCP_MAX_ODT; odt_idx++)
        {
            ch->storage_ptr->odt[odt_pool_idx + odt_idx].odt_entry
                = (Xcp_OdtEntryType *) &ch->storage_ptr->odt_entry[entry_pool_idx];
            ch->storage_ptr->odt[odt_pool_idx + odt_idx].odt_entry_count = 0U;
            ch->storage_ptr->odt[odt_pool_idx + odt_idx].odt_entry_max_count = XCP_MAX_ODT_ENTRIES;

            entry_pool_idx += XCP_MAX_ODT_ENTRIES;
        }

        odt_pool_idx += XCP_MAX_ODT;
    }

#if (XCP_MIN_DAQ > 0U)
    Xcp_LoadStaticDaqLists(&g_xcp_channel);
#endif

    Xcp_RebuildActiveDaqList(&g_xcp_channel);
}

#if (XCP_MIN_DAQ > 0U)
static void Xcp_LoadStaticDaqList(Xcp_Channel *ch, uint16 list_no)
{
    const Xcp_StaticDaqListType *cfg = &ch->config_ptr->xcp_static_daq_list[list_no];
    Xcp_DaqListType *daq = &ch->storage_ptr->daq_list[list_no];
    uint8 o;

    daq->state         = XCP_DAQ_STATE_CONFIGURED;
    daq->event_channel = cfg->event_channel_number;
    daq->event_fixed   = cfg->event_fixed;
    daq->direction     = cfg->direction;
    daq->odt_count     = cfg->odt_count;
    daq->odt_max_count = cfg->odt_count;
    daq->mode          = 0U;
    daq->resume        = FALSE;
    daq->selected      = FALSE;
    daq->prescaler     = 0U;
    daq->priority      = 0U;
    daq->prescaler_counter = 0U;

    for (o = 0U; o < cfg->odt_count; o++)
    {
        Xcp_OdtType *odt = &daq->odt[o];
        uint8 e;

        odt->odt_entry_count     = cfg->odt_entry_count;
        odt->odt_entry_max_count = cfg->odt_entry_count;

        if (cfg->entries != NULL_PTR)
        {
            for (e = 0U; e < cfg->odt_entry_count; e++)
            {
                odt->odt_entry[e] = cfg->entries[(o * cfg->odt_entry_count) + e];
            }
        }
    }
}

static void Xcp_LoadStaticDaqLists(Xcp_Channel *ch)
{
    uint16 s;

    for (s = 0U; s < XCP_MIN_DAQ; s++)
    {
        Xcp_LoadStaticDaqList(&g_xcp_channel, s);
    }
}
#endif

sint16 Xcp_CmdGetDaqProcessorInfo(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{

    uint8 daq_properties = XCP_DAQ_PROP_BIT_STIM_SUPPORTED | XCP_DAQ_PROP_OVERLOAD_EVENT;

    (void) cmd_data;
    (void) cmd_length;

#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_DYNAMIC)
    daq_properties |= XCP_DAQ_PROP_CONFIG_TYPE_DYNAMIC;
#endif
#if (XCP_PRESCALER_SUPPORTED == STD_ON)
    daq_properties |= XCP_DAQ_PROP_PRESCALER_SUPPORTED;
#endif
#if (XCP_RESUME_MODE_ENABLED == STD_ON)
    daq_properties |= XCP_DAQ_PROP_RESUME_SUPPORTED;
#endif
#if (XCP_TIMESTAMP_TYPE != XCP_NO_TIME_STAMP)
    daq_properties |= XCP_DAQ_PROP_TIMESTAMP_SUPPORTED;
#endif

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = daq_properties;
    ch->storage_ptr->cto_buffer[2] = (uint8) (XCP_MAX_DAQ & 0xFFU);
    ch->storage_ptr->cto_buffer[3] = (uint8) ((XCP_MAX_DAQ >> 8U) & 0xFFU);
    ch->storage_ptr->cto_buffer[4] = (uint8) (XCP_MAX_EVENT_CHANNEL & 0xFFU);
    ch->storage_ptr->cto_buffer[5] = (uint8) ((XCP_MAX_EVENT_CHANNEL >> 8U) & 0xFFU);
    ch->storage_ptr->cto_buffer[6] = (uint8) XCP_MIN_DAQ;

    ch->storage_ptr->cto_buffer[7] =
        (uint8) ((XCP_IDENTIFICATION_FIELD_TYPE & 0x03U) << XCP_DAQ_KEY_ID_FIELD_SHIFT);

    return (sint16)(8U);
}

sint16 Xcp_CmdGetDaqResolutionInfo(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8 timestamp_mode;

    (void) cmd_data;
    (void) cmd_length;

    timestamp_mode = (uint8) (XCP_TIMESTAMP_TYPE & 0x07U);
#if (XCP_TIMESTAMP_FIXED == 1U)
    timestamp_mode |= XCP_TS_MODE_FIXED;
#endif
    timestamp_mode |= (uint8) ((XCP_TIMESTAMP_UNIT & 0x0FU) << XCP_TS_MODE_UNIT_SHIFT);

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = 1U;
    ch->storage_ptr->cto_buffer[2] = XCP_ODT_ENTRY_SIZE_DAQ;
    ch->storage_ptr->cto_buffer[3] = 1U;
    ch->storage_ptr->cto_buffer[4] = XCP_ODT_ENTRY_SIZE_STIM;
    ch->storage_ptr->cto_buffer[5] = timestamp_mode;
    ch->storage_ptr->cto_buffer[6] = (uint8) (XCP_TIMESTAMP_TICKS & 0xFFU);
    ch->storage_ptr->cto_buffer[7] = (uint8) ((XCP_TIMESTAMP_TICKS >> 8U) & 0xFFU);

    return (sint16)(8U);
}

sint16 Xcp_CmdGetDaqEventInfo(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint16                      event_channel;
    const Xcp_EventChannelType *event_cfg;
    uint8                       event_properties;

    (void) cmd_length;

    event_channel = Xcp_LeRead16(&cmd_data[2U]);

    {
        uint8 idx = Xcp_EventChannelIdx(ch, event_channel);
        if (idx >= XCP_MAX_EVENT_CHANNEL)
        {
            return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
        }
        event_cfg = &ch->config_ptr->xcp_event_channel[idx];
    }

    event_properties = (uint8) (event_cfg->event_channel_type & XCP_EVENT_CHANNEL_TYPE_DAQ_STIM);
    event_properties |= (uint8) ((event_cfg->consistency & 0x03U) << XCP_EVENT_PROP_CONSISTENCY_SHIFT);

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = event_properties;
    ch->storage_ptr->cto_buffer[2] = (uint8) event_cfg->max_daq_list;
    ch->storage_ptr->cto_buffer[3] = 0x00U;
    ch->storage_ptr->cto_buffer[4] = (uint8) event_cfg->time_cycle;
    ch->storage_ptr->cto_buffer[5] = (uint8) event_cfg->time_unit;
    ch->storage_ptr->cto_buffer[6] = event_cfg->event_channel_priority;

    return (sint16)(7U);
}

void Xcp_FreeDaqLists(Xcp_Channel *ch)
{
    uint16 i;

    for (i = XCP_MIN_DAQ; i < XCP_MAX_DAQ; i++)
    {
        ch->storage_ptr->daq_list[i].odt_count = 0U;
        ch->storage_ptr->daq_list[i].state = XCP_DAQ_STATE_FREE;
        ch->storage_ptr->daq_list[i].event_channel = 0U;
        ch->storage_ptr->daq_list[i].mode = 0U;
        ch->storage_ptr->daq_list[i].direction = XCP_DAQ_LIST_TYPE_DAQ;
        ch->storage_ptr->daq_list[i].selected = FALSE;
        ch->storage_ptr->daq_list[i].resume = FALSE;
    }

#if (XCP_MIN_DAQ > 0U)
    Xcp_LoadStaticDaqLists(&g_xcp_channel);
#endif

    ch->runtime_ptr->daq_count = XCP_MIN_DAQ;

    XCP_DTO_ENTER_CRITICAL();
    ch->storage_ptr->dto_queue_count = 0U;
    ch->storage_ptr->dto_queue_head  = 0U;
    ch->storage_ptr->dto_queue_tail  = 0U;
    XCP_DTO_EXIT_CRITICAL();

    Xcp_ResetEventChannelCounters(&g_xcp_channel);
    Xcp_RebuildActiveDaqList(&g_xcp_channel);
}

sint16 Xcp_CmdFreeDaq(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_data;
    (void) cmd_length;

    Xcp_FreeDaqLists(&g_xcp_channel);
    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdAllocDaq(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_STATIC)
    (void) cmd_data;
    (void) cmd_length;
    return Xcp_ResErr(res, XCP_ERR_DAQ_CONFIG);
#else
    uint16 daq_count = Xcp_LeRead16(&cmd_data[2U]);
    uint16 i;

    (void) cmd_length;

    if ((XCP_MIN_DAQ + daq_count) > XCP_MAX_DAQ)
    {
        return Xcp_ResErr(res, XCP_ERR_MEMORY_OVERFLOW);
    }

    for (i = 0U; i < ch->runtime_ptr->daq_count; i++)
    {
        if (ch->storage_ptr->daq_list[i].state == XCP_DAQ_STATE_RUNNING)
        {
            return Xcp_ResErr(res, XCP_ERR_DAQ_ACTIVE);
        }
    }

    ch->runtime_ptr->daq_count = XCP_MIN_DAQ + daq_count;
    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
#endif
}

sint16 Xcp_CmdAllocOdt(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint16 daq_list_no;
    uint8  odt_count;

    (void) cmd_length;

#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_STATIC)
    return Xcp_ResErr(res, XCP_ERR_DAQ_CONFIG);
#endif

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);
    odt_count = cmd_data[4];

    if (daq_list_no >= ch->runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

#if (XCP_MIN_DAQ > 0U)
    if (daq_list_no < XCP_MIN_DAQ)
    {
        return Xcp_ResErr(res, XCP_ERR_WRITE_PROTECTED);
    }
#endif

    if (odt_count > XCP_MAX_ODT)
    {
        return Xcp_ResErr(res, XCP_ERR_MEMORY_OVERFLOW);
    }

    if (ch->storage_ptr->daq_list[daq_list_no].state == XCP_DAQ_STATE_RUNNING)
    {
        return Xcp_ResErr(res, XCP_ERR_DAQ_ACTIVE);
    }

    ch->storage_ptr->daq_list[daq_list_no].odt_count = odt_count;
    ch->storage_ptr->daq_list[daq_list_no].state = XCP_DAQ_STATE_CONFIGURED;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdAllocOdtEntry(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint16 daq_list_no;
    uint8  odt_no;
    uint8  odt_entry_count;

    (void) cmd_length;

#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_STATIC)
    return Xcp_ResErr(res, XCP_ERR_DAQ_CONFIG);
#endif

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);
    odt_no = cmd_data[4];
    odt_entry_count = cmd_data[5];

    if (daq_list_no >= ch->runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

#if (XCP_MIN_DAQ > 0U)
    if (daq_list_no < XCP_MIN_DAQ)
    {
        return Xcp_ResErr(res, XCP_ERR_WRITE_PROTECTED);
    }
#endif

    if (odt_no >= ch->storage_ptr->daq_list[daq_list_no].odt_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

#if (XCP_MAX_ODT_ENTRIES < 255U)
    if (odt_entry_count > XCP_MAX_ODT_ENTRIES)
    {
        return Xcp_ResErr(res, XCP_ERR_MEMORY_OVERFLOW);
    }
#endif

    if (ch->storage_ptr->daq_list[daq_list_no].state == XCP_DAQ_STATE_RUNNING)
    {
        return Xcp_ResErr(res, XCP_ERR_DAQ_ACTIVE);
    }

    {
        Xcp_OdtType *odt = &ch->storage_ptr->daq_list[daq_list_no].odt[odt_no];
        uint8        e;

        for (e = 0U; e < odt_entry_count; e++)
        {
            odt->odt_entry[e].address           = 0U;
            odt->odt_entry[e].address_extension = 0U;
            odt->odt_entry[e].size              = 0U;
            odt->odt_entry[e].bit_offset        = XCP_BIT_OFFSET_NONE;
        }
        odt->odt_entry_count = odt_entry_count;
    }
    ch->storage_ptr->daq_list[daq_list_no].state = XCP_DAQ_STATE_CONFIGURED;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdSetDaqPtr(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint16 daq_list_no;
    uint8  odt_no;
    uint8  odt_entry_no;

    (void) cmd_length;

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);
    odt_no = cmd_data[4];
    odt_entry_no = cmd_data[5];

    if (daq_list_no >= ch->runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (odt_no >= ch->storage_ptr->daq_list[daq_list_no].odt_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (odt_entry_no >= ch->storage_ptr->daq_list[daq_list_no].odt[odt_no].odt_entry_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    ch->runtime_ptr->daq_list_ptr = daq_list_no;
    ch->runtime_ptr->odt_ptr = odt_no;
    ch->runtime_ptr->odt_entry_ptr = odt_entry_no;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdWriteDaq(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8             bit_offset;
    uint8             size;
    uint8             address_extension;
    uint32            address;
    Xcp_OdtEntryType *entry;

    (void) cmd_length;

    bit_offset = cmd_data[1];
    size = cmd_data[2];
    address_extension = cmd_data[3];
    address = Xcp_LeRead32(&cmd_data[4U]);

    if ((ch->runtime_ptr->daq_list_ptr >= ch->runtime_ptr->daq_count)
        || (ch->runtime_ptr->odt_ptr
            >= ch->storage_ptr->daq_list[ch->runtime_ptr->daq_list_ptr].odt_count)
        || (ch->runtime_ptr->odt_entry_ptr
            >= ch->storage_ptr->daq_list[ch->runtime_ptr->daq_list_ptr]
                   .odt[ch->runtime_ptr->odt_ptr]
                   .odt_entry_count))
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

#if (XCP_MIN_DAQ > 0U)
    if ((ch->runtime_ptr->daq_list_ptr < XCP_MIN_DAQ)
        && (ch->config_ptr->xcp_static_daq_list[ch->runtime_ptr->daq_list_ptr]
                .write_protected != FALSE))
    {
        return Xcp_ResErr(res, XCP_ERR_WRITE_PROTECTED);
    }
#endif

    if ((bit_offset > XCP_BIT_OFFSET_MAX) && (bit_offset != XCP_BIT_OFFSET_NONE))
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (ch->storage_ptr->daq_list[ch->runtime_ptr->daq_list_ptr].state
        == (uint8) XCP_DAQ_STATE_RUNNING)
    {
        return Xcp_ResErr(res, XCP_ERR_DAQ_ACTIVE);
    }

    {
        uint8 max_size = (ch->storage_ptr->daq_list[ch->runtime_ptr->daq_list_ptr].direction
                          == XCP_DAQ_LIST_TYPE_STIM)
                         ? (uint8) XCP_ODT_ENTRY_SIZE_STIM
                         : (uint8) XCP_ODT_ENTRY_SIZE_DAQ;
        if (size > max_size)
        {
            return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
        }
    }

    entry = &ch->storage_ptr->daq_list[ch->runtime_ptr->daq_list_ptr]
                 .odt[ch->runtime_ptr->odt_ptr]
                 .odt_entry[ch->runtime_ptr->odt_entry_ptr];
    entry->address = address;
    entry->address_extension = address_extension;
    entry->size = size;
    entry->bit_offset = bit_offset;
    ch->runtime_ptr->odt_entry_ptr++;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdSetDaqListMode(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  mode;
    uint16 daq_list_no;
    uint16 event_channel;
    uint8  prescaler;
    uint8  priority;

    (void) cmd_length;

    mode = cmd_data[1];
    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);
    event_channel = Xcp_LeRead16(&cmd_data[4U]);
    prescaler = cmd_data[6];
    priority = cmd_data[7];

    if (daq_list_no >= ch->runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if ((mode & 0x4DU) != 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

    if ((mode & XCP_DAQ_MODE_PID_OFF) != 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }
#if (XCP_RESUME_MODE_ENABLED == STD_OFF)
    if ((mode & XCP_DAQ_MODE_RESUME) != 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }
#endif
#if (XCP_TIMESTAMP_TYPE == XCP_NO_TIME_STAMP)
    if ((mode & XCP_DAQ_MODE_TIMESTAMP) != 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }
#endif

    {
        Xcp_DaqListType *daq = &ch->storage_ptr->daq_list[daq_list_no];
        uint8 evt_idx = Xcp_EventChannelIdx(ch, event_channel);

        if (evt_idx >= XCP_MAX_EVENT_CHANNEL)
        {
            return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
        }

        const uint8 evt_type
            = ch->config_ptr->xcp_event_channel[evt_idx].event_channel_type;
        const uint8 dir_bit = ((mode & XCP_DAQ_MODE_DIRECTION_STIM) != 0U)
                              ? XCP_EVENT_CHANNEL_TYPE_STIM : XCP_EVENT_CHANNEL_TYPE_DAQ;

        if ((evt_type & dir_bit) == 0U)
        {
            return Xcp_ResErr(res, XCP_ERR_DAQ_CONFIG);
        }

        if ((daq->event_fixed != FALSE)
            && ((event_channel != daq->event_channel)
                || ((daq->direction == XCP_DAQ_LIST_TYPE_STIM)
                    != ((mode & XCP_DAQ_MODE_DIRECTION_STIM) != 0U))))
        {
            return Xcp_ResErr(res, XCP_ERR_DAQ_CONFIG);
        }

        daq->mode = mode;
        daq->event_channel = event_channel;
        daq->prescaler = prescaler;
        daq->priority = priority;
#if (XCP_RESUME_MODE_ENABLED == STD_ON)
        daq->resume = ((mode & XCP_DAQ_MODE_RESUME) != 0U) ? TRUE : FALSE;
#endif

        if ((mode & XCP_DAQ_MODE_DIRECTION_STIM) != 0U)
        {
            daq->direction = XCP_DAQ_LIST_TYPE_STIM;
        }
        else
        {
            daq->direction = XCP_DAQ_LIST_TYPE_DAQ;
        }
    }

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdGetDaqListMode(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint16           daq_list_no;
    Xcp_DaqListType *daq;
    uint8            current_mode = 0U;

    (void)cmd_length;

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);

    if (daq_list_no >= ch->runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    daq = &ch->storage_ptr->daq_list[daq_list_no];

    if (daq->selected != FALSE)
    {
        current_mode |= XCP_DAQ_MODE_SELECTED;
    }
    if (daq->direction == XCP_DAQ_LIST_TYPE_STIM)
    {
        current_mode |= XCP_DAQ_MODE_DIRECTION_STIM;
    }
    if (XCP_DAQ_HAS_TIMESTAMP(daq))
    {
        current_mode |= XCP_DAQ_MODE_TIMESTAMP;
    }
    if (daq->state == (uint8) XCP_DAQ_STATE_RUNNING)
    {
        current_mode |= XCP_DAQ_MODE_RUNNING;
    }
#if (XCP_RESUME_MODE_ENABLED == STD_ON)
    if ((daq->resume != FALSE) || (Xcp_IsDaqListResume(&g_xcp_channel, daq_list_no) != FALSE))
    {
        current_mode |= XCP_DAQ_MODE_RESUME;
    }
#endif

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = current_mode;
    ch->storage_ptr->cto_buffer[2] = 0x00U;
    ch->storage_ptr->cto_buffer[3] = 0x00U;
    ch->storage_ptr->cto_buffer[4] = (uint8)(daq->event_channel & 0xFFU);
    ch->storage_ptr->cto_buffer[5] = (uint8)((daq->event_channel >> 8U) & 0xFFU);
    ch->storage_ptr->cto_buffer[6] = daq->prescaler;
    ch->storage_ptr->cto_buffer[7] = daq->priority;

    return (sint16)(8U);
}

sint16 Xcp_CmdStartStopDaqList(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  mode;
    uint16 daq_list_no;

    (void) cmd_length;

    mode = cmd_data[1];
    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);

    if (daq_list_no >= ch->runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (mode == XCP_START_STOP_STOP)
    {
        ch->storage_ptr->daq_list[daq_list_no].state = XCP_DAQ_STATE_STOPPED;
    }
    else if (mode == XCP_START_STOP_START)
    {
        ch->storage_ptr->daq_list[daq_list_no].state = XCP_DAQ_STATE_RUNNING;
#if (XCP_STIM_TIMEOUT_ENABLED == STD_ON)
        if (ch->storage_ptr->daq_list[daq_list_no].direction == XCP_DAQ_LIST_TYPE_STIM)
        {
            Xcp_ResetStimTimeout(&g_xcp_channel, daq_list_no);
        }
#endif
    }
    else if (mode == XCP_START_STOP_SELECT)
    {

        ch->storage_ptr->daq_list[daq_list_no].selected = TRUE;
    }
    else
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

    Xcp_RebuildActiveDaqList(&g_xcp_channel);

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;

#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
    uint16 first_pid = 0U;
    uint16 idx;

    for (idx = 0U; idx < daq_list_no; idx++)
    {
        first_pid += ch->storage_ptr->daq_list[idx].odt_count;
    }

    ch->storage_ptr->cto_buffer[1] = (uint8) (first_pid & 0xFFU);
#else
    /* RELATIVE_*: first_pid = list number (relative PID of ODT0 in that list is implicitly 0) */
    ch->storage_ptr->cto_buffer[1] = (uint8) (daq_list_no & 0xFFU);
#endif

    return (sint16)(2U);
}

sint16 Xcp_CmdStartStopSynch(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  mode;
    uint16 i;

    (void) cmd_length;

    mode = cmd_data[1];

    if (mode > XCP_START_STOP_SELECT)
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

    for (i = 0U; i < ch->runtime_ptr->daq_count; i++)
    {
        Xcp_DaqListType *daq = &ch->storage_ptr->daq_list[i];

        if (mode == XCP_START_STOP_STOP)
        {
            /* mode 0: stop all RUNNING lists (selected not required) */
            if (daq->state == (uint8) XCP_DAQ_STATE_RUNNING)
            {
                daq->state = XCP_DAQ_STATE_STOPPED;
            }
            daq->selected = FALSE;
        }
        else if (mode == XCP_START_STOP_START)
        {
            /* mode 1: start all selected lists */
            if (daq->selected != FALSE)
            {
                daq->state = XCP_DAQ_STATE_RUNNING;
#if (XCP_STIM_TIMEOUT_ENABLED == STD_ON)
                if (daq->direction == XCP_DAQ_LIST_TYPE_STIM)
                {
                    Xcp_ResetStimTimeout(&g_xcp_channel, i);
                }
#endif
                daq->selected = FALSE;
            }
        }
        else
        {
            /* mode 2: stop RUNNING lists that are selected */
            if ((daq->selected != FALSE)
                && (daq->state == (uint8) XCP_DAQ_STATE_RUNNING))
            {
                daq->state = XCP_DAQ_STATE_STOPPED;
            }
            daq->selected = FALSE;
        }
    }

    Xcp_RebuildActiveDaqList(&g_xcp_channel);

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

static uint8 Xcp_DaqListTypeProperties(Xcp_Channel *ch, uint16 daq_list_no)
{
    uint8 properties = (uint8) (XCP_DAQ_LIST_PROP_DAQ | XCP_DAQ_LIST_PROP_STIM);

#if (XCP_MIN_DAQ > 0U)
    if (daq_list_no < XCP_MIN_DAQ)
    {
        uint8 list_type = ch->config_ptr->xcp_static_daq_list[daq_list_no].direction;

        if (list_type == XCP_DAQ_LIST_TYPE_DAQ)
        {
            properties = XCP_DAQ_LIST_PROP_DAQ;
        }
        else if (list_type == XCP_DAQ_LIST_TYPE_STIM)
        {
            properties = XCP_DAQ_LIST_PROP_STIM;
        }
        else
        {

        }
    }
#else
    (void) daq_list_no;
#endif

    return properties;
}

sint16 Xcp_CmdGetDaqListInfo(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint16           daq_list_no;
    Xcp_DaqListType *daq;
    uint8            properties;

    (void) cmd_length;

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);

    if (daq_list_no >= ch->runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    daq = &ch->storage_ptr->daq_list[daq_list_no];

    properties = Xcp_DaqListTypeProperties(&g_xcp_channel, daq_list_no);
#if (XCP_MIN_DAQ > 0U)
    if (daq_list_no < XCP_MIN_DAQ)
    {
        properties |= XCP_DAQ_LIST_PROP_PREDEFINED;
    }
#endif
    if (daq->event_fixed != FALSE)
    {
        properties |= XCP_DAQ_LIST_PROP_EVENT_FIXED;
    }
#if (XCP_RESUME_MODE_ENABLED == STD_ON)
    if (daq->resume != FALSE)
    {
        properties |= XCP_DAQ_LIST_PROP_RESUME;
    }
#endif

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = properties;
    ch->storage_ptr->cto_buffer[2] = daq->odt_max_count;
    ch->storage_ptr->cto_buffer[3] = XCP_MAX_ODT_ENTRIES;
    ch->storage_ptr->cto_buffer[4] = (uint8) (daq->event_channel & 0xFFU);
    ch->storage_ptr->cto_buffer[5] = (uint8) ((daq->event_channel >> 8U) & 0xFFU);

    return (sint16)(6U);
}

sint16 Xcp_CmdClearDaqList(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint16           daq_list_no;
    Xcp_DaqListType *daq;

    (void) cmd_length;

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);

    if (daq_list_no >= ch->runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    daq = &ch->storage_ptr->daq_list[daq_list_no];

    if (daq->state == (uint8) XCP_DAQ_STATE_RUNNING)
    {
        return Xcp_ResErr(res, XCP_ERR_DAQ_ACTIVE);
    }

#if (XCP_MIN_DAQ > 0U)
    if (daq_list_no < XCP_MIN_DAQ)
    {
        Xcp_LoadStaticDaqList(&g_xcp_channel, daq_list_no);
    }
    else
#endif
    {
        daq->odt_count = 0U;
        daq->state = XCP_DAQ_STATE_FREE;
        daq->event_channel = 0U;
        daq->mode = 0U;
        daq->direction = XCP_DAQ_LIST_TYPE_DAQ;
        daq->selected = FALSE;
        daq->resume = FALSE;
        daq->prescaler = 0U;
        daq->priority = 0U;
        daq->prescaler_counter = 0U;
    }
    Xcp_RebuildActiveDaqList(&g_xcp_channel);

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdReadDaq(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    Xcp_OdtEntryType *entry;
    uint16            daq_list_no = ch->runtime_ptr->daq_list_ptr;
    uint8             odt_no = ch->runtime_ptr->odt_ptr;
    uint8             odt_entry_no = ch->runtime_ptr->odt_entry_ptr;

    (void) cmd_data;
    (void) cmd_length;

    if (daq_list_no >= ch->runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (odt_no >= ch->storage_ptr->daq_list[daq_list_no].odt_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (odt_entry_no >= ch->storage_ptr->daq_list[daq_list_no].odt[odt_no].odt_entry_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    entry = &ch->storage_ptr->daq_list[daq_list_no].odt[odt_no].odt_entry[odt_entry_no];

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = entry->bit_offset;
    ch->storage_ptr->cto_buffer[2] = entry->size;
    ch->storage_ptr->cto_buffer[3] = entry->address_extension;
    ch->storage_ptr->cto_buffer[4] = (uint8) (entry->address & 0xFFU);
    ch->storage_ptr->cto_buffer[5] = (uint8) ((entry->address >> 8U) & 0xFFU);
    ch->storage_ptr->cto_buffer[6] = (uint8) ((entry->address >> 16U) & 0xFFU);
    ch->storage_ptr->cto_buffer[7] = (uint8) ((entry->address >> 24U) & 0xFFU);

    return (sint16)(8U);
}

sint16 Xcp_CmdGetDaqClock(Xcp_Channel *ch, const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint32 timestamp;
    uint8  ts_size;

    (void) cmd_data;
    (void) cmd_length;

    timestamp = Xcp_GetTimestamp();
    ts_size   = Xcp_TimestampSize();

    if (ts_size == 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_CMD_UNKNOWN);
    }

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = 0x00U;
    ch->storage_ptr->cto_buffer[2] = 0x00U;
    ch->storage_ptr->cto_buffer[3] = 0x00U;

    (void)Xcp_WriteTimestamp(timestamp, &ch->storage_ptr->cto_buffer[4U]);

    return (sint16)(4U + ts_size);
}

#endif
