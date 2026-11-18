#include "Xcp_DaqInt.h"

#include <string.h>

#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Resume.h"

#if (XCP_DAQ_ENABLED == STD_ON)

#if (XCP_MIN_DAQ > 0U)
static void Xcp_LoadStaticDaqLists(void);
#endif

void Xcp_InitDaqLists(void)
{
    uint16 daq_idx = 0;
    uint8  odt_idx = 0;
    uint16 odt_pool_idx = 0U;
    uint16 entry_pool_idx = 0U;

    g_xcp_channel.runtime_ptr->daq_list = (Xcp_DaqListType *) &g_xcp_channel.storage_ptr->daq_list[0];
#if (XCP_MIN_DAQ > 0U)
    g_xcp_channel.runtime_ptr->daq_count = XCP_MIN_DAQ;
#else
    g_xcp_channel.runtime_ptr->daq_count = 0U;
#endif
    for (daq_idx = 0U; daq_idx < XCP_MAX_DAQ; daq_idx++)
    {
        g_xcp_channel.storage_ptr->daq_list[daq_idx].odt
            = (Xcp_OdtType *) &g_xcp_channel.storage_ptr->odt[odt_pool_idx];
        g_xcp_channel.storage_ptr->daq_list[daq_idx].odt_count = 0U;
        g_xcp_channel.storage_ptr->daq_list[daq_idx].odt_max_count = XCP_MAX_ODT;
        g_xcp_channel.storage_ptr->daq_list[daq_idx].state = XCP_DAQ_STATE_FREE;

        for (odt_idx = 0U; odt_idx < XCP_MAX_ODT; odt_idx++)
        {
            g_xcp_channel.storage_ptr->odt[odt_pool_idx + odt_idx].odt_entry
                = (Xcp_OdtEntryType *) &g_xcp_channel.storage_ptr->odt_entry[entry_pool_idx];
            g_xcp_channel.storage_ptr->odt[odt_pool_idx + odt_idx].odt_entry_count = 0U;
            g_xcp_channel.storage_ptr->odt[odt_pool_idx + odt_idx].odt_entry_max_count = XCP_MAX_ODT_ENTRIES;

            entry_pool_idx += XCP_MAX_ODT_ENTRIES;
        }

        odt_pool_idx += XCP_MAX_ODT;
    }

#if (XCP_MIN_DAQ > 0U)
    Xcp_LoadStaticDaqLists();
#endif
}

#if (XCP_MIN_DAQ > 0U)
static void Xcp_LoadStaticDaqList(uint16 list_no)
{
    const Xcp_StaticDaqListType *cfg = &g_xcp_channel.config_ptr->xcp_static_daq_list[list_no];
    Xcp_DaqListType *daq = &g_xcp_channel.storage_ptr->daq_list[list_no];
    uint8 o;

    daq->state         = XCP_DAQ_STATE_CONFIGURED;
    daq->event_channel = cfg->event_channel_number;
    daq->event_fixed   = cfg->event_fixed;
    daq->direction     = cfg->direction;
    daq->odt_count     = cfg->odt_count;
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

        odt->odt_entry_count = cfg->odt_entry_count;

        if (cfg->entries != NULL_PTR)
        {
            for (e = 0U; e < cfg->odt_entry_count; e++)
            {
                odt->odt_entry[e] = cfg->entries[(o * cfg->odt_entry_count) + e];
            }
        }
    }
}

static void Xcp_LoadStaticDaqLists(void)
{
    uint16 s;

    for (s = 0U; s < XCP_MIN_DAQ; s++)
    {
        Xcp_LoadStaticDaqList(s);
    }
}
#endif

sint16 Xcp_CmdGetDaqProcessorInfo(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
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

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_channel.storage_ptr->cto_buffer[1] = daq_properties;
    g_xcp_channel.storage_ptr->cto_buffer[2] = (uint8) (XCP_MAX_DAQ & 0xFFU);
    g_xcp_channel.storage_ptr->cto_buffer[3] = (uint8) ((XCP_MAX_DAQ >> 8U) & 0xFFU);
    g_xcp_channel.storage_ptr->cto_buffer[4] = (uint8) (XCP_MAX_EVENT_CHANNEL & 0xFFU);
    g_xcp_channel.storage_ptr->cto_buffer[5] = (uint8) ((XCP_MAX_EVENT_CHANNEL >> 8U) & 0xFFU);
    g_xcp_channel.storage_ptr->cto_buffer[6] = (uint8) XCP_MIN_DAQ;

    g_xcp_channel.storage_ptr->cto_buffer[7] =
        (uint8) ((XCP_IDENTIFICATION_FIELD_TYPE & 0x03U) << XCP_DAQ_KEY_ID_FIELD_SHIFT);

    return (sint16)(8U);
}

sint16 Xcp_CmdGetDaqResolutionInfo(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8 timestamp_mode;

    (void) cmd_data;
    (void) cmd_length;

    timestamp_mode = (uint8) (XCP_TIMESTAMP_TYPE & 0x07U);
#if (XCP_TIMESTAMP_FIXED == 1U)
    timestamp_mode |= XCP_TS_MODE_FIXED;
#endif
    timestamp_mode |= (uint8) ((XCP_TIMESTAMP_UNIT & 0x0FU) << XCP_TS_MODE_UNIT_SHIFT);

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_channel.storage_ptr->cto_buffer[1] = 1U;
    g_xcp_channel.storage_ptr->cto_buffer[2] = XCP_ODT_ENTRY_SIZE_DAQ;
    g_xcp_channel.storage_ptr->cto_buffer[3] = 1U;
    g_xcp_channel.storage_ptr->cto_buffer[4] = XCP_ODT_ENTRY_SIZE_STIM;
    g_xcp_channel.storage_ptr->cto_buffer[5] = timestamp_mode;
    g_xcp_channel.storage_ptr->cto_buffer[6] = (uint8) (XCP_TIMESTAMP_TICKS & 0xFFU);
    g_xcp_channel.storage_ptr->cto_buffer[7] = (uint8) ((XCP_TIMESTAMP_TICKS >> 8U) & 0xFFU);

    return (sint16)(8U);
}

sint16 Xcp_CmdGetDaqEventInfo(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint16                      event_channel;
    const Xcp_EventChannelType *event_cfg;
    uint8                       event_properties;

    (void) cmd_length;

    event_channel = Xcp_LeRead16(&cmd_data[2U]);

    if (event_channel >= XCP_MAX_EVENT_CHANNEL)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    event_cfg = &g_xcp_channel.config_ptr->xcp_event_channel[event_channel];

    event_properties = (uint8) (event_cfg->event_channel_type & XCP_EVENT_CHANNEL_TYPE_DAQ_STIM);
    event_properties |= (uint8) ((event_cfg->consistency & 0x03U) << XCP_EVENT_PROP_CONSISTENCY_SHIFT);

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_channel.storage_ptr->cto_buffer[1] = event_properties;
    g_xcp_channel.storage_ptr->cto_buffer[2] = (uint8) event_cfg->max_daq_list;
    g_xcp_channel.storage_ptr->cto_buffer[3] = 0x00U;
    g_xcp_channel.storage_ptr->cto_buffer[4] = (uint8) event_cfg->time_cycle;
    g_xcp_channel.storage_ptr->cto_buffer[5] = (uint8) event_cfg->time_unit;
    g_xcp_channel.storage_ptr->cto_buffer[6] = event_cfg->event_channel_priority;

    return (sint16)(7U);
}

void Xcp_FreeDaqLists(void)
{
    uint16 i;

    for (i = XCP_MIN_DAQ; i < XCP_MAX_DAQ; i++)
    {
        g_xcp_channel.storage_ptr->daq_list[i].odt_count = 0U;
        g_xcp_channel.storage_ptr->daq_list[i].state = XCP_DAQ_STATE_FREE;
        g_xcp_channel.storage_ptr->daq_list[i].event_channel = 0U;
        g_xcp_channel.storage_ptr->daq_list[i].mode = 0U;
        g_xcp_channel.storage_ptr->daq_list[i].direction = XCP_DAQ_LIST_TYPE_DAQ;
        g_xcp_channel.storage_ptr->daq_list[i].selected = FALSE;
        g_xcp_channel.storage_ptr->daq_list[i].resume = FALSE;
    }

#if (XCP_MIN_DAQ > 0U)
    Xcp_LoadStaticDaqLists();
#endif

    g_xcp_channel.runtime_ptr->daq_count = XCP_MIN_DAQ;

    XCP_DTO_ENTER_CRITICAL();
    g_xcp_channel.storage_ptr->dto_queue_count = 0U;
    g_xcp_channel.storage_ptr->dto_queue_head  = 0U;
    g_xcp_channel.storage_ptr->dto_queue_tail  = 0U;
    XCP_DTO_EXIT_CRITICAL();

    Xcp_ResetEventChannelCounters();
    Xcp_RebuildActiveDaqList();
}

sint16 Xcp_CmdFreeDaq(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    (void) cmd_data;
    (void) cmd_length;
#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_STATIC)
    return Xcp_ResErr(res, XCP_ERR_DAQ_CONFIG);
#endif
    Xcp_FreeDaqLists();
    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdAllocDaq(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_STATIC)
    (void) cmd_data;
    (void) cmd_length;
    return Xcp_ResErr(res, XCP_ERR_DAQ_CONFIG);
#else
    uint16 daq_count = (uint16) cmd_data[2];
    uint16 i;

    (void) cmd_length;

    if ((XCP_MIN_DAQ + daq_count) > XCP_MAX_DAQ)
    {
        return Xcp_ResErr(res, XCP_ERR_MEMORY_OVERFLOW);
    }

    for (i = 0U; i < g_xcp_channel.runtime_ptr->daq_count; i++)
    {
        if (g_xcp_channel.storage_ptr->daq_list[i].state == XCP_DAQ_STATE_RUNNING)
        {
            return Xcp_ResErr(res, XCP_ERR_DAQ_ACTIVE);
        }
    }

    g_xcp_channel.runtime_ptr->daq_count = XCP_MIN_DAQ + daq_count;
    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
#endif
}

sint16 Xcp_CmdAllocOdt(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint16 daq_list_no;
    uint8  odt_count;

    (void) cmd_length;

#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_STATIC)
    return Xcp_ResErr(res, XCP_ERR_DAQ_CONFIG);
#endif

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);
    odt_count = cmd_data[4];

    if (daq_list_no >= g_xcp_channel.runtime_ptr->daq_count)
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

    if (g_xcp_channel.storage_ptr->daq_list[daq_list_no].state == XCP_DAQ_STATE_RUNNING)
    {
        return Xcp_ResErr(res, XCP_ERR_DAQ_ACTIVE);
    }

    g_xcp_channel.storage_ptr->daq_list[daq_list_no].odt_count = odt_count;
    g_xcp_channel.storage_ptr->daq_list[daq_list_no].state = XCP_DAQ_STATE_CONFIGURED;

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdAllocOdtEntry(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
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

    if (daq_list_no >= g_xcp_channel.runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

#if (XCP_MIN_DAQ > 0U)
    if (daq_list_no < XCP_MIN_DAQ)
    {
        return Xcp_ResErr(res, XCP_ERR_WRITE_PROTECTED);
    }
#endif

    if (odt_no >= g_xcp_channel.storage_ptr->daq_list[daq_list_no].odt_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

#if (XCP_MAX_ODT_ENTRIES < 255U)
    if (odt_entry_count > XCP_MAX_ODT_ENTRIES)
    {
        return Xcp_ResErr(res, XCP_ERR_MEMORY_OVERFLOW);
    }
#endif

    if (g_xcp_channel.storage_ptr->daq_list[daq_list_no].state == XCP_DAQ_STATE_RUNNING)
    {
        return Xcp_ResErr(res, XCP_ERR_DAQ_ACTIVE);
    }

    {
        Xcp_OdtType *odt = &g_xcp_channel.storage_ptr->daq_list[daq_list_no].odt[odt_no];
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
    g_xcp_channel.storage_ptr->daq_list[daq_list_no].state = XCP_DAQ_STATE_CONFIGURED;

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdSetDaqPtr(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint16 daq_list_no;
    uint8  odt_no;
    uint8  odt_entry_no;

    (void) cmd_length;

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);
    odt_no = cmd_data[4];
    odt_entry_no = cmd_data[5];

    if (daq_list_no >= g_xcp_channel.runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (odt_no >= g_xcp_channel.storage_ptr->daq_list[daq_list_no].odt_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (odt_entry_no >= g_xcp_channel.storage_ptr->daq_list[daq_list_no].odt[odt_no].odt_entry_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    g_xcp_channel.runtime_ptr->daq_list_ptr = daq_list_no;
    g_xcp_channel.runtime_ptr->odt_ptr = odt_no;
    g_xcp_channel.runtime_ptr->odt_entry_ptr = odt_entry_no;

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdWriteDaq(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
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

    if ((g_xcp_channel.runtime_ptr->daq_list_ptr >= g_xcp_channel.runtime_ptr->daq_count)
        || (g_xcp_channel.runtime_ptr->odt_ptr
            >= g_xcp_channel.storage_ptr->daq_list[g_xcp_channel.runtime_ptr->daq_list_ptr].odt_count)
        || (g_xcp_channel.runtime_ptr->odt_entry_ptr
            >= g_xcp_channel.storage_ptr->daq_list[g_xcp_channel.runtime_ptr->daq_list_ptr]
                   .odt[g_xcp_channel.runtime_ptr->odt_ptr]
                   .odt_entry_count))
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

#if (XCP_MIN_DAQ > 0U)
    if ((g_xcp_channel.runtime_ptr->daq_list_ptr < XCP_MIN_DAQ)
        && (g_xcp_channel.config_ptr->xcp_static_daq_list[g_xcp_channel.runtime_ptr->daq_list_ptr]
                .write_protected != FALSE))
    {
        return Xcp_ResErr(res, XCP_ERR_WRITE_PROTECTED);
    }
#endif

    if ((bit_offset > XCP_BIT_OFFSET_MAX) && (bit_offset != XCP_BIT_OFFSET_NONE))
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (g_xcp_channel.storage_ptr->daq_list[g_xcp_channel.runtime_ptr->daq_list_ptr].state
        == (uint8) XCP_DAQ_STATE_RUNNING)
    {
        return Xcp_ResErr(res, XCP_ERR_DAQ_ACTIVE);
    }

    entry = &g_xcp_channel.storage_ptr->daq_list[g_xcp_channel.runtime_ptr->daq_list_ptr]
                 .odt[g_xcp_channel.runtime_ptr->odt_ptr]
                 .odt_entry[g_xcp_channel.runtime_ptr->odt_entry_ptr];
    entry->address = address;
    entry->address_extension = address_extension;
    entry->size = size;
    entry->bit_offset = bit_offset;
    g_xcp_channel.runtime_ptr->odt_entry_ptr++;

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdSetDaqListMode(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
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

    if (daq_list_no >= g_xcp_channel.runtime_ptr->daq_count)
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

    if (event_channel >= XCP_MAX_EVENT_CHANNEL)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    {
        Xcp_DaqListType *daq = &g_xcp_channel.storage_ptr->daq_list[daq_list_no];
        const uint8 evt_type
            = g_xcp_channel.config_ptr->xcp_event_channel[event_channel].event_channel_type;
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

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdGetDaqListMode(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint16           daq_list_no;
    Xcp_DaqListType *daq;
    uint8            current_mode = 0U;

    (void)cmd_length;

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);

    if (daq_list_no >= g_xcp_channel.runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    daq = &g_xcp_channel.storage_ptr->daq_list[daq_list_no];

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
    if ((daq->resume != FALSE) || (Xcp_IsDaqListResume(daq_list_no) != FALSE))
    {
        current_mode |= XCP_DAQ_MODE_RESUME;
    }
#endif

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_channel.storage_ptr->cto_buffer[1] = current_mode;
    g_xcp_channel.storage_ptr->cto_buffer[2] = 0x00U;
    g_xcp_channel.storage_ptr->cto_buffer[3] = 0x00U;
    g_xcp_channel.storage_ptr->cto_buffer[4] = (uint8)(daq->event_channel & 0xFFU);
    g_xcp_channel.storage_ptr->cto_buffer[5] = (uint8)((daq->event_channel >> 8U) & 0xFFU);
    g_xcp_channel.storage_ptr->cto_buffer[6] = daq->prescaler;
    g_xcp_channel.storage_ptr->cto_buffer[7] = daq->priority;

    return (sint16)(8U);
}

sint16 Xcp_CmdStartStopDaqList(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  mode;
    uint16 daq_list_no;

    (void) cmd_length;

    mode = cmd_data[1];
    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);

    if (daq_list_no >= g_xcp_channel.runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (mode == XCP_START_STOP_STOP)
    {
        g_xcp_channel.storage_ptr->daq_list[daq_list_no].state = XCP_DAQ_STATE_STOPPED;
    }
    else if (mode == XCP_START_STOP_START)
    {
        g_xcp_channel.storage_ptr->daq_list[daq_list_no].state = XCP_DAQ_STATE_RUNNING;
#if (XCP_STIM_TIMEOUT_ENABLED == STD_ON)
        if (g_xcp_channel.storage_ptr->daq_list[daq_list_no].direction == XCP_DAQ_LIST_TYPE_STIM)
        {
            Xcp_ResetStimTimeout(daq_list_no);
        }
#endif
    }
    else if (mode == XCP_START_STOP_SELECT)
    {

        g_xcp_channel.storage_ptr->daq_list[daq_list_no].selected = TRUE;
    }
    else
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

    Xcp_RebuildActiveDaqList();

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;

#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
    uint16 first_pid = 0U;
    uint16 idx;

    for (idx = 0U; idx < daq_list_no; idx++)
    {
        first_pid += g_xcp_channel.storage_ptr->daq_list[idx].odt_count;
    }

    g_xcp_channel.storage_ptr->cto_buffer[1] = (uint8) (first_pid & 0xFFU);
#else
    g_xcp_channel.storage_ptr->cto_buffer[1] = 0U;
#endif

    return (sint16)(2U);
}

sint16 Xcp_CmdStartStopSynch(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint8  mode;
    uint16 i;

    (void) cmd_length;

    mode = cmd_data[1];

    if (mode > XCP_START_STOP_SELECT)
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

    for (i = 0U; i < g_xcp_channel.runtime_ptr->daq_count; i++)
    {
        Xcp_DaqListType *daq = &g_xcp_channel.storage_ptr->daq_list[i];

        if (mode == XCP_START_STOP_STOP)
        {
            if (daq->state == (uint8) XCP_DAQ_STATE_RUNNING)
            {
                daq->state = XCP_DAQ_STATE_STOPPED;
            }
            daq->selected = FALSE;
        }
        else if (daq->selected != FALSE)
        {
            if (mode == XCP_START_STOP_START)
            {
                daq->state = XCP_DAQ_STATE_RUNNING;
#if (XCP_STIM_TIMEOUT_ENABLED == STD_ON)
                if (daq->direction == XCP_DAQ_LIST_TYPE_STIM)
                {
                    Xcp_ResetStimTimeout(i);
                }
#endif
            }
            else if (daq->state == (uint8) XCP_DAQ_STATE_RUNNING)
            {
                daq->state = XCP_DAQ_STATE_STOPPED;
            }
            else
            {

            }
            daq->selected = FALSE;
        }
        else
        {

        }
    }

    Xcp_RebuildActiveDaqList();

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

static uint8 Xcp_DaqListTypeProperties(uint16 daq_list_no)
{
    uint8 properties = (uint8) (XCP_DAQ_LIST_PROP_DAQ | XCP_DAQ_LIST_PROP_STIM);

#if (XCP_MIN_DAQ > 0U)
    if (daq_list_no < XCP_MIN_DAQ)
    {
        uint8 list_type = g_xcp_channel.config_ptr->xcp_static_daq_list[daq_list_no].direction;

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

sint16 Xcp_CmdGetDaqListInfo(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint16           daq_list_no;
    Xcp_DaqListType *daq;
    uint8            properties;

    (void) cmd_length;

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);

    if (daq_list_no >= g_xcp_channel.runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    daq = &g_xcp_channel.storage_ptr->daq_list[daq_list_no];

    properties = Xcp_DaqListTypeProperties(daq_list_no);
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

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_channel.storage_ptr->cto_buffer[1] = properties;
    g_xcp_channel.storage_ptr->cto_buffer[2] = daq->odt_max_count;
    g_xcp_channel.storage_ptr->cto_buffer[3] = XCP_MAX_ODT_ENTRIES;
    g_xcp_channel.storage_ptr->cto_buffer[4] = (uint8) (daq->event_channel & 0xFFU);
    g_xcp_channel.storage_ptr->cto_buffer[5] = (uint8) ((daq->event_channel >> 8U) & 0xFFU);

    return (sint16)(6U);
}

sint16 Xcp_CmdClearDaqList(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint16           daq_list_no;
    Xcp_DaqListType *daq;

    (void) cmd_length;

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);

    if (daq_list_no >= g_xcp_channel.runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    daq = &g_xcp_channel.storage_ptr->daq_list[daq_list_no];

    if (daq->state == (uint8) XCP_DAQ_STATE_RUNNING)
    {
        return Xcp_ResErr(res, XCP_ERR_DAQ_ACTIVE);
    }

#if (XCP_MIN_DAQ > 0U)
    if (daq_list_no < XCP_MIN_DAQ)
    {
        Xcp_LoadStaticDaqList(daq_list_no);
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
    Xcp_RebuildActiveDaqList();

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdReadDaq(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    Xcp_OdtEntryType *entry;
    uint16            daq_list_no = g_xcp_channel.runtime_ptr->daq_list_ptr;
    uint8             odt_no = g_xcp_channel.runtime_ptr->odt_ptr;
    uint8             odt_entry_no = g_xcp_channel.runtime_ptr->odt_entry_ptr;

    (void) cmd_data;
    (void) cmd_length;

    if (daq_list_no >= g_xcp_channel.runtime_ptr->daq_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (odt_no >= g_xcp_channel.storage_ptr->daq_list[daq_list_no].odt_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    if (odt_entry_no >= g_xcp_channel.storage_ptr->daq_list[daq_list_no].odt[odt_no].odt_entry_count)
    {
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }

    entry = &g_xcp_channel.storage_ptr->daq_list[daq_list_no].odt[odt_no].odt_entry[odt_entry_no];

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_channel.storage_ptr->cto_buffer[1] = entry->bit_offset;
    g_xcp_channel.storage_ptr->cto_buffer[2] = entry->size;
    g_xcp_channel.storage_ptr->cto_buffer[3] = entry->address_extension;
    g_xcp_channel.storage_ptr->cto_buffer[4] = (uint8) (entry->address & 0xFFU);
    g_xcp_channel.storage_ptr->cto_buffer[5] = (uint8) ((entry->address >> 8U) & 0xFFU);
    g_xcp_channel.storage_ptr->cto_buffer[6] = (uint8) ((entry->address >> 16U) & 0xFFU);
    g_xcp_channel.storage_ptr->cto_buffer[7] = (uint8) ((entry->address >> 24U) & 0xFFU);

    return (sint16)(8U);
}

sint16 Xcp_CmdGetDaqClock(const uint8 *cmd_data, uint8 cmd_length, Xcp_ResFrame *res)
{
    uint32 timestamp;

    (void) cmd_data;
    (void) cmd_length;

    timestamp = Xcp_GetTimestamp();

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_channel.storage_ptr->cto_buffer[1] = 0x00U;
    g_xcp_channel.storage_ptr->cto_buffer[2] = 0x00U;
    g_xcp_channel.storage_ptr->cto_buffer[3] = 0x00U;
    g_xcp_channel.storage_ptr->cto_buffer[4] = (uint8) (timestamp & 0xFFU);
    g_xcp_channel.storage_ptr->cto_buffer[5] = (uint8) ((timestamp >> 8U) & 0xFFU);
    g_xcp_channel.storage_ptr->cto_buffer[6] = (uint8) ((timestamp >> 16U) & 0xFFU);
    g_xcp_channel.storage_ptr->cto_buffer[7] = (uint8) ((timestamp >> 24U) & 0xFFU);

    return (sint16)(8U);
}

#endif
