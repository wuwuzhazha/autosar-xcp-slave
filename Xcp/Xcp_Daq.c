#include "Xcp_Daq.h"

#include <string.h>

#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Resume.h"

#if (XCP_ON_CAN_ENABLED == STD_ON)
#include "CanIf.h"
#endif

#if (XCP_DAQ_ENABLED == STD_ON)

/* ODT0 是否携带时间戳：TIMESTAMP_FIXED 时恒带，否则看 DAQ 列表 mode */
#if (XCP_TIMESTAMP_TYPE == XCP_NO_TIME_STAMP)
#define XCP_DAQ_HAS_TIMESTAMP(daq) (FALSE)
#elif (XCP_TIMESTAMP_FIXED == 1U)
#define XCP_DAQ_HAS_TIMESTAMP(daq) (TRUE)
#else
#define XCP_DAQ_HAS_TIMESTAMP(daq) ((((daq)->mode) & XCP_DAQ_MODE_TIMESTAMP) != 0U)
#endif

#if (XCP_ENABLE_DTO_DROP_STATS == STD_ON)
static uint32 dropped_dto_counter = 0;
#endif

static boolean dto_overload_pending = FALSE;
static uint16  overload_ev_cooldown = 0U;

static uint16 dto_busy_watchdog_cnt = 0U;


static uint16 Xcp_ActiveDaqIndices[XCP_MAX_DAQ];
static uint16 Xcp_ActiveDaqCount = 0;

void Xcp_RebuildActiveDaqList(void)
{
    uint16 i;
    Xcp_ActiveDaqCount = 0;

    for (i = 0U; i < g_xcp_manager.runtime_ptr->daq_count; i++)
    {
        if (g_xcp_manager.storage_ptr->daq_list[i].state == (uint8) XCP_DAQ_STATE_RUNNING)
        {
            Xcp_ActiveDaqIndices[Xcp_ActiveDaqCount] = i;
            Xcp_ActiveDaqCount++;
        }
    }
}

#if (XCP_MIN_DAQ > 0U)
static void Xcp_LoadStaticDaqLists(void);
#endif

void Xcp_InitDaqLists(void)
{
    uint16 daq_idx = 0;
    uint8  odt_idx = 0;
    uint16 odt_pool_idx = 0U;
    uint16 entry_pool_idx = 0U;

    g_xcp_manager.runtime_ptr->daq_list = (Xcp_DaqListType *) &g_xcp_manager.storage_ptr->daq_list[0];
#if (XCP_MIN_DAQ > 0U)
    g_xcp_manager.runtime_ptr->daq_count = XCP_MIN_DAQ;
#else
    g_xcp_manager.runtime_ptr->daq_count = 0U;
#endif
    for (daq_idx = 0U; daq_idx < XCP_MAX_DAQ; daq_idx++)
    {
        g_xcp_manager.storage_ptr->daq_list[daq_idx].odt
            = (Xcp_OdtType *) &g_xcp_manager.storage_ptr->odt[odt_pool_idx];
        g_xcp_manager.storage_ptr->daq_list[daq_idx].odt_count = 0U;
        g_xcp_manager.storage_ptr->daq_list[daq_idx].odt_max_count = XCP_MAX_ODT;
        g_xcp_manager.storage_ptr->daq_list[daq_idx].state = XCP_DAQ_STATE_FREE;

        for (odt_idx = 0U; odt_idx < XCP_MAX_ODT; odt_idx++)
        {
            g_xcp_manager.storage_ptr->odt[odt_pool_idx + odt_idx].odt_entry
                = (Xcp_OdtEntryType *) &g_xcp_manager.storage_ptr->odt_entry[entry_pool_idx];
            g_xcp_manager.storage_ptr->odt[odt_pool_idx + odt_idx].odt_entry_count = 0U;
            g_xcp_manager.storage_ptr->odt[odt_pool_idx + odt_idx].odt_entry_max_count = XCP_MAX_ODT_ENTRIES;

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
    const Xcp_StaticDaqListType *cfg = &g_xcp_manager.config_ptr->xcp_static_daq_list[list_no];
    Xcp_DaqListType *daq = &g_xcp_manager.storage_ptr->daq_list[list_no];
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

void Xcp_ResetEventChannelCounters(void)
{
    uint16 i;

    for (i = 0U; i < XCP_MAX_EVENT_CHANNEL; i++)
    {
        g_xcp_manager.runtime_ptr->event_channel_counter[i] = 0U;
    }

#if (XCP_PRESCALER_SUPPORTED == STD_ON)
    for (i = 0U; i < g_xcp_manager.runtime_ptr->daq_count; i++)
    {
        g_xcp_manager.storage_ptr->daq_list[i].prescaler_counter = 0U;
    }
#endif
}

void Xcp_CmdGetDaqProcessorInfo(const uint8 *cmd_data, uint8 cmd_length)
{
    /* 位级 STIM（WRITE_DAQ BIT_OFFSET）与 EV_DAQ_OVERLOAD 恒实现 */
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

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_manager.storage_ptr->cto_buffer[1] = daq_properties;
    g_xcp_manager.storage_ptr->cto_buffer[2] = (uint8) (XCP_MAX_DAQ & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[3] = (uint8) ((XCP_MAX_DAQ >> 8U) & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[4] = (uint8) (XCP_MAX_EVENT_CHANNEL & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[5] = (uint8) ((XCP_MAX_EVENT_CHANNEL >> 8U) & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[6] = (uint8) XCP_MIN_DAQ;
    /* DAQ_KEY_BYTE: 标识字段类型；地址扩展可在 ODT 内不同；默认优化 */
    g_xcp_manager.storage_ptr->cto_buffer[7] =
        (uint8) ((XCP_IDENTIFICATION_FIELD_TYPE & 0x03U) << XCP_DAQ_KEY_ID_FIELD_SHIFT);

    Xcp_SendPositiveResponse(8U);
}

void Xcp_CmdGetDaqResolutionInfo(const uint8 *cmd_data, uint8 cmd_length)
{
    uint8 timestamp_mode;

    (void) cmd_data;
    (void) cmd_length;

    /* TIMESTAMP_MODE: bit0-2 SIZE（XCP_*_BYTE 取值即编码）, bit3 FIXED, bit4-7 UNIT */
    timestamp_mode = (uint8) (XCP_TIMESTAMP_TYPE & 0x07U);
#if (XCP_TIMESTAMP_FIXED == 1U)
    timestamp_mode |= XCP_TS_MODE_FIXED;
#endif
    timestamp_mode |= (uint8) ((XCP_TIMESTAMP_UNIT & 0x0FU) << XCP_TS_MODE_UNIT_SHIFT);

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_manager.storage_ptr->cto_buffer[1] = 1U;
    g_xcp_manager.storage_ptr->cto_buffer[2] = XCP_ODT_ENTRY_SIZE_DAQ;
    g_xcp_manager.storage_ptr->cto_buffer[3] = 1U;
    g_xcp_manager.storage_ptr->cto_buffer[4] = XCP_ODT_ENTRY_SIZE_STIM;
    g_xcp_manager.storage_ptr->cto_buffer[5] = timestamp_mode;
    g_xcp_manager.storage_ptr->cto_buffer[6] = (uint8) (XCP_TIMESTAMP_TICKS & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[7] = (uint8) ((XCP_TIMESTAMP_TICKS >> 8U) & 0xFFU);

    Xcp_SendPositiveResponse(8U);
}

void Xcp_CmdGetDaqEventInfo(const uint8 *cmd_data, uint8 cmd_length)
{
    uint16                      event_channel;
    const Xcp_EventChannelType *event_cfg;
    uint8                       event_properties;

    (void) cmd_length;

    event_channel = Xcp_LeRead16(&cmd_data[2U]);

    if (event_channel >= XCP_MAX_EVENT_CHANNEL)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    event_cfg = &g_xcp_manager.config_ptr->xcp_event_channel[event_channel];

    /* DAQ_EVENT_PROPERTIES: bit2 DAQ / bit3 STIM（类型常量即位值）, bit6-7 一致性 */
    event_properties = (uint8) (event_cfg->event_channel_type & XCP_EVENT_CHANNEL_TYPE_DAQ_STIM);
    event_properties |= (uint8) ((event_cfg->consistency & 0x03U) << XCP_EVENT_PROP_CONSISTENCY_SHIFT);

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_manager.storage_ptr->cto_buffer[1] = event_properties;
    g_xcp_manager.storage_ptr->cto_buffer[2] = (uint8) event_cfg->max_daq_list;
    g_xcp_manager.storage_ptr->cto_buffer[3] = 0x00U;
    g_xcp_manager.storage_ptr->cto_buffer[4] = (uint8) event_cfg->time_cycle;
    g_xcp_manager.storage_ptr->cto_buffer[5] = (uint8) event_cfg->time_unit;
    g_xcp_manager.storage_ptr->cto_buffer[6] = event_cfg->event_channel_priority;

    Xcp_SendPositiveResponse(7U);
}

void Xcp_FreeDaqLists(void)
{
    uint16 i;

    for (i = XCP_MIN_DAQ; i < XCP_MAX_DAQ; i++)
    {
        g_xcp_manager.storage_ptr->daq_list[i].odt_count = 0U;
        g_xcp_manager.storage_ptr->daq_list[i].state = XCP_DAQ_STATE_FREE;
        g_xcp_manager.storage_ptr->daq_list[i].event_channel = 0U;
        g_xcp_manager.storage_ptr->daq_list[i].mode = 0U;
        g_xcp_manager.storage_ptr->daq_list[i].direction = XCP_DAQ_LIST_TYPE_DAQ;
        g_xcp_manager.storage_ptr->daq_list[i].selected = FALSE;
        g_xcp_manager.storage_ptr->daq_list[i].resume = FALSE;
    }

#if (XCP_MIN_DAQ > 0U)
    Xcp_LoadStaticDaqLists();
#endif

    g_xcp_manager.runtime_ptr->daq_count = XCP_MIN_DAQ;

    XCP_DTO_ENTER_CRITICAL();
    g_xcp_manager.storage_ptr->dto_queue_count = 0U;
    g_xcp_manager.storage_ptr->dto_queue_head  = 0U;
    g_xcp_manager.storage_ptr->dto_queue_tail  = 0U;
    XCP_DTO_EXIT_CRITICAL();

    Xcp_ResetEventChannelCounters();
    Xcp_RebuildActiveDaqList();
}

void Xcp_CmdFreeDaq(const uint8 *cmd_data, uint8 cmd_length)
{
    (void) cmd_data;
    (void) cmd_length;
#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_STATIC)
    Xcp_SendErrorResponse(XCP_ERR_DAQ_CONFIG);
    return;
#endif
    Xcp_FreeDaqLists();
    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    Xcp_SendPositiveResponse(1U);
}

void Xcp_CmdAllocDaq(const uint8 *cmd_data, uint8 cmd_length)
{
#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_STATIC)
    (void) cmd_data;
    (void) cmd_length;
    Xcp_SendErrorResponse(XCP_ERR_DAQ_CONFIG);
#else
    uint16 daq_count = (uint16) cmd_data[2];
    uint16 i;

    (void) cmd_length;

    if ((XCP_MIN_DAQ + daq_count) > XCP_MAX_DAQ)
    {
        Xcp_SendErrorResponse(XCP_ERR_MEMORY_OVERFLOW);
        return;
    }

    for (i = 0U; i < g_xcp_manager.runtime_ptr->daq_count; i++)
    {
        if (g_xcp_manager.storage_ptr->daq_list[i].state == XCP_DAQ_STATE_RUNNING)
        {
            Xcp_SendErrorResponse(XCP_ERR_DAQ_ACTIVE);
            return;
        }
    }

    g_xcp_manager.runtime_ptr->daq_count = XCP_MIN_DAQ + daq_count;
    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    Xcp_SendPositiveResponse(1U);
#endif
}

void Xcp_CmdAllocOdt(const uint8 *cmd_data, uint8 cmd_length)
{
    uint16 daq_list_no;
    uint8  odt_count;

    (void) cmd_length;

#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_STATIC)
    Xcp_SendErrorResponse(XCP_ERR_DAQ_CONFIG);
    return;
#endif

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);
    odt_count = cmd_data[4];

    if (daq_list_no >= g_xcp_manager.runtime_ptr->daq_count)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

#if (XCP_MIN_DAQ > 0U)
    if (daq_list_no < XCP_MIN_DAQ)
    {
        Xcp_SendErrorResponse(XCP_ERR_WRITE_PROTECTED);
        return;
    }
#endif

    if (odt_count > XCP_MAX_ODT)
    {
        Xcp_SendErrorResponse(XCP_ERR_MEMORY_OVERFLOW);
        return;
    }

    if (g_xcp_manager.storage_ptr->daq_list[daq_list_no].state == XCP_DAQ_STATE_RUNNING)
    {
        Xcp_SendErrorResponse(XCP_ERR_DAQ_ACTIVE);
        return;
    }

    g_xcp_manager.storage_ptr->daq_list[daq_list_no].odt_count = odt_count;
    g_xcp_manager.storage_ptr->daq_list[daq_list_no].state = XCP_DAQ_STATE_CONFIGURED;

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    Xcp_SendPositiveResponse(1U);
}

void Xcp_CmdAllocOdtEntry(const uint8 *cmd_data, uint8 cmd_length)
{
    uint16 daq_list_no;
    uint8  odt_no;
    uint8  odt_entry_count;

    (void) cmd_length;

#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_STATIC)
    Xcp_SendErrorResponse(XCP_ERR_DAQ_CONFIG);
    return;
#endif

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);
    odt_no = cmd_data[4];
    odt_entry_count = cmd_data[5];

    if (daq_list_no >= g_xcp_manager.runtime_ptr->daq_count)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

#if (XCP_MIN_DAQ > 0U)
    if (daq_list_no < XCP_MIN_DAQ)
    {
        Xcp_SendErrorResponse(XCP_ERR_WRITE_PROTECTED);
        return;
    }
#endif

    if (odt_no >= g_xcp_manager.storage_ptr->daq_list[daq_list_no].odt_count)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

#if (XCP_MAX_ODT_ENTRIES < 255U)   /* uint8 计数天然 <= 255 */
    if (odt_entry_count > XCP_MAX_ODT_ENTRIES)
    {
        Xcp_SendErrorResponse(XCP_ERR_MEMORY_OVERFLOW);
        return;
    }
#endif

    if (g_xcp_manager.storage_ptr->daq_list[daq_list_no].state == XCP_DAQ_STATE_RUNNING)
    {
        Xcp_SendErrorResponse(XCP_ERR_DAQ_ACTIVE);
        return;
    }

    {
        Xcp_OdtType *odt = &g_xcp_manager.storage_ptr->daq_list[daq_list_no].odt[odt_no];
        uint8        e;

        /* 新分配条目清空: size=0 不采样/不刺激，避免沿用上次配置的地址 */
        for (e = 0U; e < odt_entry_count; e++)
        {
            odt->odt_entry[e].address           = 0U;
            odt->odt_entry[e].address_extension = 0U;
            odt->odt_entry[e].size              = 0U;
            odt->odt_entry[e].bit_offset        = XCP_BIT_OFFSET_NONE;
        }
        odt->odt_entry_count = odt_entry_count;
    }
    g_xcp_manager.storage_ptr->daq_list[daq_list_no].state = XCP_DAQ_STATE_CONFIGURED;

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    Xcp_SendPositiveResponse(1U);
}

void Xcp_CmdSetDaqPtr(const uint8 *cmd_data, uint8 cmd_length)
{
    uint16 daq_list_no;
    uint8  odt_no;
    uint8  odt_entry_no;

    (void) cmd_length;

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);
    odt_no = cmd_data[4];
    odt_entry_no = cmd_data[5];

    if (daq_list_no >= g_xcp_manager.runtime_ptr->daq_count)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    if (odt_no >= g_xcp_manager.storage_ptr->daq_list[daq_list_no].odt_count)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    if (odt_entry_no >= g_xcp_manager.storage_ptr->daq_list[daq_list_no].odt[odt_no].odt_entry_count)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    g_xcp_manager.runtime_ptr->daq_list_ptr = daq_list_no;
    g_xcp_manager.runtime_ptr->odt_ptr = odt_no;
    g_xcp_manager.runtime_ptr->odt_entry_ptr = odt_entry_no;

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    Xcp_SendPositiveResponse(1U);
}

void Xcp_CmdWriteDaq(const uint8 *cmd_data, uint8 cmd_length)
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

    if ((g_xcp_manager.runtime_ptr->daq_list_ptr >= g_xcp_manager.runtime_ptr->daq_count)
        || (g_xcp_manager.runtime_ptr->odt_ptr
            >= g_xcp_manager.storage_ptr->daq_list[g_xcp_manager.runtime_ptr->daq_list_ptr].odt_count)
        || (g_xcp_manager.runtime_ptr->odt_entry_ptr
            >= g_xcp_manager.storage_ptr->daq_list[g_xcp_manager.runtime_ptr->daq_list_ptr]
                   .odt[g_xcp_manager.runtime_ptr->odt_ptr]
                   .odt_entry_count))
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

#if (XCP_MIN_DAQ > 0U)
    if ((g_xcp_manager.runtime_ptr->daq_list_ptr < XCP_MIN_DAQ)
        && (g_xcp_manager.config_ptr->xcp_static_daq_list[g_xcp_manager.runtime_ptr->daq_list_ptr]
                .write_protected != FALSE))
    {
        Xcp_SendErrorResponse(XCP_ERR_WRITE_PROTECTED);
        return;
    }
#endif

    if ((bit_offset > XCP_BIT_OFFSET_MAX) && (bit_offset != XCP_BIT_OFFSET_NONE))
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    if (g_xcp_manager.storage_ptr->daq_list[g_xcp_manager.runtime_ptr->daq_list_ptr].state
        == (uint8) XCP_DAQ_STATE_RUNNING)
    {
        Xcp_SendErrorResponse(XCP_ERR_DAQ_ACTIVE);
        return;
    }

    entry = &g_xcp_manager.storage_ptr->daq_list[g_xcp_manager.runtime_ptr->daq_list_ptr]
                 .odt[g_xcp_manager.runtime_ptr->odt_ptr]
                 .odt_entry[g_xcp_manager.runtime_ptr->odt_entry_ptr];
    entry->address = address;
    entry->address_extension = address_extension;
    entry->size = size;
    entry->bit_offset = bit_offset;
    g_xcp_manager.runtime_ptr->odt_entry_ptr++;

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    Xcp_SendPositiveResponse(1U);
}

void Xcp_CmdSetDaqListMode(const uint8 *cmd_data, uint8 cmd_length)
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

    if (daq_list_no >= g_xcp_manager.runtime_ptr->daq_count)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    if ((mode & 0x4DU) != 0U)
    {
        Xcp_SendErrorResponse(XCP_ERR_MODE_NOT_VALID);
        return;
    }

    /* PID_OFF 未实现（DAQ_PROPERTIES.PID_OFF_SUPPORTED=0） */
    if ((mode & XCP_DAQ_MODE_PID_OFF) != 0U)
    {
        Xcp_SendErrorResponse(XCP_ERR_MODE_NOT_VALID);
        return;
    }
#if (XCP_RESUME_MODE_ENABLED == STD_OFF)
    if ((mode & XCP_DAQ_MODE_RESUME) != 0U)
    {
        Xcp_SendErrorResponse(XCP_ERR_MODE_NOT_VALID);
        return;
    }
#endif
#if (XCP_TIMESTAMP_TYPE == XCP_NO_TIME_STAMP)
    if ((mode & XCP_DAQ_MODE_TIMESTAMP) != 0U)
    {
        Xcp_SendErrorResponse(XCP_ERR_MODE_NOT_VALID);
        return;
    }
#endif

    if (event_channel >= XCP_MAX_EVENT_CHANNEL)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    {
        Xcp_DaqListType *daq = &g_xcp_manager.storage_ptr->daq_list[daq_list_no];
        const uint8 evt_type
            = g_xcp_manager.config_ptr->xcp_event_channel[event_channel].event_channel_type;
        const uint8 dir_bit = ((mode & XCP_DAQ_MODE_DIRECTION_STIM) != 0U)
                              ? XCP_EVENT_CHANNEL_TYPE_STIM : XCP_EVENT_CHANNEL_TYPE_DAQ;

        if ((evt_type & dir_bit) == 0U)
        {
            Xcp_SendErrorResponse(XCP_ERR_DAQ_CONFIG);
            return;
        }

        if ((daq->event_fixed != FALSE)
            && ((event_channel != daq->event_channel)
                || ((daq->direction == XCP_DAQ_LIST_TYPE_STIM)
                    != ((mode & XCP_DAQ_MODE_DIRECTION_STIM) != 0U))))
        {
            Xcp_SendErrorResponse(XCP_ERR_DAQ_CONFIG);
            return;
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

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    Xcp_SendPositiveResponse(1U);
}

void Xcp_CmdGetDaqListMode(const uint8 *cmd_data, uint8 cmd_length)
{
    uint16           daq_list_no;
    Xcp_DaqListType *daq;
    uint8            current_mode = 0U;

    (void)cmd_length;

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);

    if (daq_list_no >= g_xcp_manager.runtime_ptr->daq_count)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    daq = &g_xcp_manager.storage_ptr->daq_list[daq_list_no];

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

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_manager.storage_ptr->cto_buffer[1] = current_mode;
    g_xcp_manager.storage_ptr->cto_buffer[2] = 0x00U;
    g_xcp_manager.storage_ptr->cto_buffer[3] = 0x00U;
    g_xcp_manager.storage_ptr->cto_buffer[4] = (uint8)(daq->event_channel & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[5] = (uint8)((daq->event_channel >> 8U) & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[6] = daq->prescaler;
    g_xcp_manager.storage_ptr->cto_buffer[7] = daq->priority;

    Xcp_SendPositiveResponse(8U);
}

void Xcp_CmdStartStopDaqList(const uint8 *cmd_data, uint8 cmd_length)
{
    uint8  mode;
    uint16 daq_list_no;

    (void) cmd_length;

    mode = cmd_data[1];
    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);

    if (daq_list_no >= g_xcp_manager.runtime_ptr->daq_count)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    if (mode == XCP_START_STOP_STOP)
    {
        g_xcp_manager.storage_ptr->daq_list[daq_list_no].state = XCP_DAQ_STATE_STOPPED;
    }
    else if (mode == XCP_START_STOP_START)
    {
        g_xcp_manager.storage_ptr->daq_list[daq_list_no].state = XCP_DAQ_STATE_RUNNING;
#if (XCP_STIM_TIMEOUT_ENABLED == STD_ON)
        if (g_xcp_manager.storage_ptr->daq_list[daq_list_no].direction == XCP_DAQ_LIST_TYPE_STIM)
        {
            Xcp_ResetStimTimeout(daq_list_no);
        }
#endif
    }
    else if (mode == XCP_START_STOP_SELECT)
    {
        /* 仅标记，START_STOP_SYNCH 统一启停 */
        g_xcp_manager.storage_ptr->daq_list[daq_list_no].selected = TRUE;
    }
    else
    {
        Xcp_SendErrorResponse(XCP_ERR_MODE_NOT_VALID);
        return;
    }

    Xcp_RebuildActiveDaqList();

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;

#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
    uint16 first_pid = 0U;
    uint16 idx;

    for (idx = 0U; idx < daq_list_no; idx++)
    {
        first_pid += g_xcp_manager.storage_ptr->daq_list[idx].odt_count;
    }

    g_xcp_manager.storage_ptr->cto_buffer[1] = (uint8) (first_pid & 0xFFU);
#else
    g_xcp_manager.storage_ptr->cto_buffer[1] = 0U;
#endif

    Xcp_SendPositiveResponse(2U);
}

void Xcp_CmdStartStopSynch(const uint8 *cmd_data, uint8 cmd_length)
{
    uint8  mode;
    uint16 i;

    (void) cmd_length;

    mode = cmd_data[1];

    /* 0=stop all, 1=start selected, 2=stop selected */
    if (mode > XCP_START_STOP_SELECT)
    {
        Xcp_SendErrorResponse(XCP_ERR_MODE_NOT_VALID);
        return;
    }

    for (i = 0U; i < g_xcp_manager.runtime_ptr->daq_count; i++)
    {
        Xcp_DaqListType *daq = &g_xcp_manager.storage_ptr->daq_list[i];

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
                /* stop selected：未运行的列表保持原状态 */
            }
            daq->selected = FALSE;
        }
        else
        {
            /* 未选中的列表不受 start/stop selected 影响 */
        }
    }

    Xcp_RebuildActiveDaqList();

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    Xcp_SendPositiveResponse(1U);
}

/* DAQ_LIST_PROPERTIES 的 DAQ/STIM 位：静态列表按配置类型，其余列表两种方向均可 */
static uint8 Xcp_DaqListTypeProperties(uint16 daq_list_no)
{
    uint8 properties = (uint8) (XCP_DAQ_LIST_PROP_DAQ | XCP_DAQ_LIST_PROP_STIM);

#if (XCP_MIN_DAQ > 0U)
    if (daq_list_no < XCP_MIN_DAQ)
    {
        uint8 list_type = g_xcp_manager.config_ptr->xcp_static_daq_list[daq_list_no].direction;

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
            /* XCP_DAQ_LIST_TYPE_DAQ_STIM */
        }
    }
#else
    (void) daq_list_no;
#endif

    return properties;
}

void Xcp_CmdGetDaqListInfo(const uint8 *cmd_data, uint8 cmd_length)
{
    uint16           daq_list_no;
    Xcp_DaqListType *daq;
    uint8            properties;

    (void) cmd_length;

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);

    if (daq_list_no >= g_xcp_manager.runtime_ptr->daq_count)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    daq = &g_xcp_manager.storage_ptr->daq_list[daq_list_no];

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

    /* [1]=DAQ_LIST_PROPERTIES [2]=MAX_ODT [3]=MAX_ODT_ENTRIES [4..5]=FIXED_EVENT */
    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_manager.storage_ptr->cto_buffer[1] = properties;
    g_xcp_manager.storage_ptr->cto_buffer[2] = daq->odt_max_count;
    g_xcp_manager.storage_ptr->cto_buffer[3] = XCP_MAX_ODT_ENTRIES;
    g_xcp_manager.storage_ptr->cto_buffer[4] = (uint8) (daq->event_channel & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[5] = (uint8) ((daq->event_channel >> 8U) & 0xFFU);

    Xcp_SendPositiveResponse(6U);
}

void Xcp_CmdClearDaqList(const uint8 *cmd_data, uint8 cmd_length)
{
    uint16           daq_list_no;
    Xcp_DaqListType *daq;

    (void) cmd_length;

    daq_list_no = Xcp_LeRead16(&cmd_data[2U]);

    if (daq_list_no >= g_xcp_manager.runtime_ptr->daq_count)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    daq = &g_xcp_manager.storage_ptr->daq_list[daq_list_no];

    if (daq->state == (uint8) XCP_DAQ_STATE_RUNNING)
    {
        Xcp_SendErrorResponse(XCP_ERR_DAQ_ACTIVE);
        return;
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

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    Xcp_SendPositiveResponse(1U);
}

void Xcp_CmdReadDaq(const uint8 *cmd_data, uint8 cmd_length)
{
    Xcp_OdtEntryType *entry;
    uint16            daq_list_no = g_xcp_manager.runtime_ptr->daq_list_ptr;
    uint8             odt_no = g_xcp_manager.runtime_ptr->odt_ptr;
    uint8             odt_entry_no = g_xcp_manager.runtime_ptr->odt_entry_ptr;

    (void) cmd_data;
    (void) cmd_length;

    if (daq_list_no >= g_xcp_manager.runtime_ptr->daq_count)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    if (odt_no >= g_xcp_manager.storage_ptr->daq_list[daq_list_no].odt_count)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    if (odt_entry_no >= g_xcp_manager.storage_ptr->daq_list[daq_list_no].odt[odt_no].odt_entry_count)
    {
        Xcp_SendErrorResponse(XCP_ERR_OUT_OF_RANGE);
        return;
    }

    entry = &g_xcp_manager.storage_ptr->daq_list[daq_list_no].odt[odt_no].odt_entry[odt_entry_no];

    /* [1]=BIT_OFFSET [2]=SIZE [3]=ADDRESS_EXTENSION [4..7]=ADDRESS */
    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_manager.storage_ptr->cto_buffer[1] = entry->bit_offset;
    g_xcp_manager.storage_ptr->cto_buffer[2] = entry->size;
    g_xcp_manager.storage_ptr->cto_buffer[3] = entry->address_extension;
    g_xcp_manager.storage_ptr->cto_buffer[4] = (uint8) (entry->address & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[5] = (uint8) ((entry->address >> 8U) & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[6] = (uint8) ((entry->address >> 16U) & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[7] = (uint8) ((entry->address >> 24U) & 0xFFU);

    Xcp_SendPositiveResponse(8U);
}

void Xcp_CmdGetDaqClock(const uint8 *cmd_data, uint8 cmd_length)
{
    uint32 timestamp;

    (void) cmd_data;
    (void) cmd_length;

    timestamp = Xcp_GetTimestamp();

    g_xcp_manager.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_manager.storage_ptr->cto_buffer[1] = 0x00U;
    g_xcp_manager.storage_ptr->cto_buffer[2] = 0x00U;
    g_xcp_manager.storage_ptr->cto_buffer[3] = 0x00U;
    g_xcp_manager.storage_ptr->cto_buffer[4] = (uint8) (timestamp & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[5] = (uint8) ((timestamp >> 8U) & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[6] = (uint8) ((timestamp >> 16U) & 0xFFU);
    g_xcp_manager.storage_ptr->cto_buffer[7] = (uint8) ((timestamp >> 24U) & 0xFFU);

    Xcp_SendPositiveResponse(8U);
}

void Xcp_ProcessDaq(uint16 event_channel)
{
    uint16 i;
    uint16 daqIdx;
    uint8  odtIdx;

    for (i = 0U; i < Xcp_ActiveDaqCount; i++)
    {
        daqIdx = Xcp_ActiveDaqIndices[i];
        Xcp_DaqListType *daq = &g_xcp_manager.storage_ptr->daq_list[daqIdx];

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

    if (daq_list_no >= g_xcp_manager.runtime_ptr->daq_count)
    {
        return;
    }

    if (odt_no >= g_xcp_manager.storage_ptr->daq_list[daq_list_no].odt_count)
    {
        return;
    }

    odt = &g_xcp_manager.storage_ptr->daq_list[daq_list_no].odt[odt_no];

    (void) memset(&local_dto_buffer[0], 0x00, XCP_MAX_DTO);

    buffer_idx = 0U;

#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)

    {
        uint16 abs_odt_id = 0U;
        uint16 d_idx;

        for (d_idx = 0U; d_idx < daq_list_no; d_idx++)
        {
            abs_odt_id += g_xcp_manager.storage_ptr->daq_list[d_idx].odt_count;
        }

        abs_odt_id += odt_no;
        local_dto_buffer[buffer_idx] = (uint8) (abs_odt_id & 0xFFU);
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

    if ((odt_no == 0U) && XCP_DAQ_HAS_TIMESTAMP(&g_xcp_manager.storage_ptr->daq_list[daq_list_no]))
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
            /* 已分配但未 WRITE_DAQ 的条目 */
            continue;
        }

        /* 位对象: 取 address 起第 bit_offset 位（LE），以 1 字节承载 */
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
            if (XCP_CHECK_READ_ACCESS(byte_addr, 1U) == 0U)
            {
                local_dto_buffer[buffer_idx] =
                    (uint8) ((src_ptr[entry->bit_offset >> 3U] >> (entry->bit_offset & 0x07U)) & 0x01U);
            }
        }
        else if (XCP_CHECK_READ_ACCESS(entry->address, entry->size) != 0U)
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
    Xcp_StorageType *storage = g_xcp_manager.storage_ptr;
    Xcp_RuntimeType *runtime = g_xcp_manager.runtime_ptr;

    XCP_DTO_ENTER_CRITICAL();

    if (runtime->tx_mode == XCP_TX_OFF)
    {
        XCP_DTO_EXIT_CRITICAL();
        return;
    }

    if (runtime->tx_last_kind == XCP_TX_KIND_NONE)
    {

        runtime->dto_busy     = TRUE;
        runtime->tx_last_kind = XCP_TX_KIND_DTO;

        if (Xcp_Tl_SendDto(dto_data, dto_length) == E_OK)
        {
            XCP_DTO_EXIT_CRITICAL();
            return;
        }

        runtime->dto_busy     = FALSE;
        runtime->tx_last_kind = XCP_TX_KIND_NONE;
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
    Xcp_StorageType *storage = g_xcp_manager.storage_ptr;
    Xcp_RuntimeType *runtime = g_xcp_manager.runtime_ptr;

    runtime->dto_busy = FALSE;

    if (runtime->tx_mode == XCP_TX_OFF)
    {
        return;
    }

    if (runtime->tx_last_kind != XCP_TX_KIND_NONE)
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

        runtime->dto_busy     = TRUE;
        runtime->tx_last_kind = XCP_TX_KIND_DTO;

        if (Xcp_Tl_SendDto(dto_copy, (uint8) XCP_MAX_DTO) != E_OK)
        {
            runtime->dto_busy     = FALSE;
            runtime->tx_last_kind = XCP_TX_KIND_NONE;
            storage->dto_queue_tail = tail;
            storage->dto_queue_count++;
        }
    }
}

void Xcp_ReportDtoOverload(void)
{
    uint16           i;
    Xcp_StorageType *storage = g_xcp_manager.storage_ptr;
    Xcp_RuntimeType *runtime = g_xcp_manager.runtime_ptr;

    XCP_DTO_ENTER_CRITICAL();

    if (runtime->dto_busy != FALSE)
    {
        if (dto_busy_watchdog_cnt < XCP_DTO_BUSY_TIMEOUT_CYCLES)
        {
            dto_busy_watchdog_cnt++;
        }
        else
        {

            if (runtime->tx_last_kind == XCP_TX_KIND_DTO)
            {
                runtime->tx_last_kind = XCP_TX_KIND_NONE;
            }

            runtime->dto_busy = FALSE;
            dto_busy_watchdog_cnt = 0U;
        }
    }
    else
    {
        dto_busy_watchdog_cnt = 0U;
    }

    if ((runtime->tx_mode == XCP_TX_ON) &&
        (runtime->cto_pending != FALSE) && (runtime->tx_last_kind == XCP_TX_KIND_NONE))
    {
        Xcp_SendPendingCto();
    }

    if ((runtime->tx_mode == XCP_TX_ON) &&
        (runtime->tx_last_kind == XCP_TX_KIND_NONE) && (storage->dto_queue_count > 0U))
    {
        uint8 tail = storage->dto_queue_tail;
        uint8 dto_copy[XCP_MAX_DTO];

        (void)memcpy(dto_copy, storage->dto_queue[tail], (uint8) XCP_MAX_DTO);
        storage->dto_queue_tail = (tail + 1U) % (uint8) XCP_MAX_ODT;
        storage->dto_queue_count--;

        runtime->dto_busy     = TRUE;
        runtime->tx_last_kind = XCP_TX_KIND_DTO;

        if (Xcp_Tl_SendDto(dto_copy, (uint8) XCP_MAX_DTO) != E_OK)
        {
            runtime->dto_busy     = FALSE;
            runtime->tx_last_kind = XCP_TX_KIND_NONE;
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
        if (g_xcp_manager.storage_ptr->daq_list[i].state == (uint8) XCP_DAQ_STATE_RUNNING)
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

void Xcp_ProcessStim(uint16 event_channel)
{
    uint16           i;
    uint16           daqIdx;
    Xcp_DaqListType *daq;

    for (i = 0U; i < Xcp_ActiveDaqCount; i++)
    {
        daqIdx = Xcp_ActiveDaqIndices[i];
        daq = &g_xcp_manager.storage_ptr->daq_list[daqIdx];

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

#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE) || (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_BYTE)
    const uint8 min_stim_len = 2U;
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_WORD)
    const uint8 min_stim_len = 4U;
#else
    const uint8 min_stim_len = 5U;
#endif

    if ((dto_data == NULL) || (dto_length < min_stim_len))
    {
        return E_NOT_OK;
    }

#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)

    {
        uint16 abs_odt = (uint16) dto_data[0];
        uint16 prefix  = 0U;
        uint16 d_idx;

        daq_list_no = 0U;
        odt_no      = 0xFFU;

        for (d_idx = 0U; d_idx < g_xcp_manager.runtime_ptr->daq_count; d_idx++)
        {
            uint16 cnt = (uint16) g_xcp_manager.storage_ptr->daq_list[d_idx].odt_count;

            if ((abs_odt >= prefix) && (abs_odt < (prefix + cnt)))
            {
                daq_list_no = d_idx;
                odt_no      = (uint8) (abs_odt - prefix);
                break;
            }

            prefix = (uint16) (prefix + cnt);
        }

        if (odt_no == 0xFFU)
        {
            return E_NOT_OK;
        }
    }

    data_idx = 1U;
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_BYTE)

    odt_no = dto_data[0];
    daq_list_no = (uint16) dto_data[1];
    data_idx = 2U;
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_WORD)

    odt_no = dto_data[0];
    daq_list_no = (uint16) dto_data[1] | ((uint16) dto_data[2] << 8U);
    data_idx = 3U;
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_WORD_ALIGNED)

    odt_no = dto_data[0];

    daq_list_no = (uint16) dto_data[2] | ((uint16) dto_data[3] << 8U);
    data_idx = 4U;
#endif
    if (daq_list_no >= g_xcp_manager.runtime_ptr->daq_count)
    {
        return E_NOT_OK;
    }

    if (g_xcp_manager.storage_ptr->daq_list[daq_list_no].direction != XCP_DAQ_LIST_TYPE_STIM)
    {
        return E_NOT_OK;
    }

    if (g_xcp_manager.storage_ptr->daq_list[daq_list_no].state != XCP_DAQ_STATE_RUNNING)
    {
        return E_NOT_OK;
    }

    if (odt_no >= g_xcp_manager.storage_ptr->daq_list[daq_list_no].odt_count)
    {
        return E_NOT_OK;
    }

    odt = &g_xcp_manager.storage_ptr->daq_list[daq_list_no].odt[odt_no];

#if (XCP_TIMESTAMP_TYPE != XCP_NO_TIME_STAMP)

    if ((odt_no == 0U) && XCP_DAQ_HAS_TIMESTAMP(&g_xcp_manager.storage_ptr->daq_list[daq_list_no]))
    {
#if (XCP_TIMESTAMP_TYPE == XCP_FOUR_BYTE)
        data_idx += 4U;
#elif (XCP_TIMESTAMP_TYPE == XCP_TWO_BYTE)
        data_idx += 2U;
#elif (XCP_TIMESTAMP_TYPE == XCP_ONE_BYTE)
        data_idx += 1U;
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

        if ((data_idx + elem_len) > dto_length)
        {
            break;
        }

        dst_ptr = (uint8 *) entry->address;

        if (is_bit != FALSE)
        {
            /* 位级 STIM: 数据字节 bit0 写入 address 起第 bit_offset 位（LE） */
            uint8  byte_idx  = (uint8) (entry->bit_offset >> 3U);
            uint8  mask      = (uint8) (1U << (entry->bit_offset & 0x07U));
            uint32 byte_addr = entry->address + byte_idx;

            if (XCP_CHECK_WRITE_ACCESS(byte_addr, 1U) == 0U)
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
        else if (XCP_CHECK_WRITE_ACCESS(entry->address, entry->size) == 0U)
        {
            (void) memcpy(dst_ptr, &dto_data[data_idx], entry->size);
        }
        else
        {
            /* 写保护地址: 跳过数据 */
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
        g_xcp_manager.runtime_ptr->stim_last_received[i] = 0U;
        g_xcp_manager.runtime_ptr->stim_timeout_flag[i] = FALSE;
    }
}

void Xcp_ResetStimTimeout(uint16 daq_list_no)
{
    if (daq_list_no >= XCP_MAX_DAQ)
    {
        return;
    }

    g_xcp_manager.runtime_ptr->stim_last_received[daq_list_no] = g_xcp_manager.runtime_ptr->timestamp;

    g_xcp_manager.runtime_ptr->stim_timeout_flag[daq_list_no] = FALSE;
}

void Xcp_CheckStimTimeout(void)
{
    uint16           i;
    uint32           current_timestamp;
    uint32           elapsed_time;
    uint32           timeout_ticks;
    Xcp_DaqListType *daq;

    if ((g_xcp_manager.runtime_ptr == NULL) || (g_xcp_manager.runtime_ptr->state != XCP_INIT)
        || (g_xcp_manager.runtime_ptr->connection_state != XCP_CONNECTED))
    {
        return;
    }

    current_timestamp = g_xcp_manager.runtime_ptr->timestamp;

    timeout_ticks = XCP_STIM_TIMEOUT_MS / XCP_MAIN_FUNCTION_PERIOD_MS;

    if (timeout_ticks == 0U)
    {
        timeout_ticks = 1U;
    }

    for (i = 0U; i < g_xcp_manager.runtime_ptr->daq_count; i++)
    {
        daq = &g_xcp_manager.storage_ptr->daq_list[i];

        if ((daq->state == XCP_DAQ_STATE_RUNNING) && (daq->direction == XCP_DAQ_LIST_TYPE_STIM))
        {
            if (current_timestamp >= g_xcp_manager.runtime_ptr->stim_last_received[i])
            {
                elapsed_time = current_timestamp - g_xcp_manager.runtime_ptr->stim_last_received[i];
            }
            else
            {
                elapsed_time
                    = (0xFFFFFFFFU - g_xcp_manager.runtime_ptr->stim_last_received[i]) + current_timestamp + 1U;
            }

            if ((elapsed_time >= timeout_ticks) && (g_xcp_manager.runtime_ptr->stim_timeout_flag[i] == FALSE))
            {

                g_xcp_manager.runtime_ptr->stim_timeout_flag[i] = TRUE;
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
