#ifndef XCP_DAQ_INT_H
#define XCP_DAQ_INT_H

#include "Xcp_Daq.h"
#include "Xcp_Types.h"
#include "Xcp_Config.h"
#include "Xcp_Internal.h"

#if (XCP_DAQ_ENABLED == STD_ON)

#if (XCP_TIMESTAMP_TYPE == XCP_NO_TIME_STAMP)
#define XCP_DAQ_HAS_TIMESTAMP(daq) (FALSE)
#elif (XCP_TIMESTAMP_FIXED == 1U)
#define XCP_DAQ_HAS_TIMESTAMP(daq) (TRUE)
#else
#define XCP_DAQ_HAS_TIMESTAMP(daq) ((((daq)->mode) & XCP_DAQ_MODE_TIMESTAMP) != 0U)
#endif

/* ActiveDaqIndices/Count live in Xcp_StorageType::active_daq_idx/count */

static inline uint16 Xcp_AbsPidEncode(Xcp_Channel *ch, uint16 daq_list_no, uint8 odt_no)
{
    uint16 abs_id = 0U;
    uint16 d_idx;

    for (d_idx = 0U; d_idx < daq_list_no; d_idx++)
    {
        abs_id += ch->storage_ptr->daq_list[d_idx].odt_count;
    }
    return (uint16)(abs_id + odt_no);
}

static inline Std_ReturnType Xcp_AbsPidDecode(Xcp_Channel *ch, uint16 abs_odt, uint16 *daq_list_no, uint8 *odt_no)
{
    uint16 prefix = 0U;
    uint16 d_idx;

    for (d_idx = 0U; d_idx < ch->runtime_ptr->daq_count; d_idx++)
    {
        uint16 cnt = (uint16) ch->storage_ptr->daq_list[d_idx].odt_count;

        if ((abs_odt >= prefix) && (abs_odt < (uint16)(prefix + cnt)))
        {
            *daq_list_no = d_idx;
            *odt_no      = (uint8) (abs_odt - prefix);
            return E_OK;
        }
        prefix = (uint16)(prefix + cnt);
    }
    return E_NOT_OK;
}

static inline uint8 Xcp_PidFieldEncode(Xcp_Channel *ch, uint16 daq_list_no, uint8 odt_no, uint8 *buf)
{
    uint8 n = 0U;

#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
    buf[n++] = (uint8) (Xcp_AbsPidEncode(ch, daq_list_no, odt_no) & 0xFFU);
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_BYTE)
    buf[n++] = odt_no;
    buf[n++] = (uint8) daq_list_no;
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_WORD)
    buf[n++] = odt_no;
    buf[n++] = (uint8) (daq_list_no & 0xFFU);
    buf[n++] = (uint8) ((daq_list_no >> 8U) & 0xFFU);
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_WORD_ALIGNED)
    buf[n++] = odt_no;
    buf[n++] = 0x00U;
    buf[n++] = (uint8) (daq_list_no & 0xFFU);
    buf[n++] = (uint8) ((daq_list_no >> 8U) & 0xFFU);
#endif
    return n;
}

static inline Std_ReturnType Xcp_PidFieldDecode(Xcp_Channel *ch, const uint8 *dto_data, uint8 dto_length,
                                              uint16 *daq_list_no, uint8 *odt_no, uint8 *data_idx)
{
#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
    if (dto_length < 2U)
    {
        return E_NOT_OK;
    }
    if (Xcp_AbsPidDecode(ch, (uint16) dto_data[0], daq_list_no, odt_no) != E_OK)
    {
        return E_NOT_OK;
    }
    *data_idx = 1U;
    return E_OK;
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_BYTE)
    if (dto_length < 3U)
    {
        return E_NOT_OK;
    }
    *odt_no      = dto_data[0];
    *daq_list_no = (uint16) dto_data[1];
    *data_idx    = 2U;
    return E_OK;
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_WORD)
    if (dto_length < 4U)
    {
        return E_NOT_OK;
    }
    *odt_no      = dto_data[0];
    *daq_list_no = (uint16) ((uint16) dto_data[1] | ((uint16) dto_data[2] << 8U));
    *data_idx    = 3U;
    return E_OK;
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_WORD_ALIGNED)
    if (dto_length < 5U)
    {
        return E_NOT_OK;
    }
    *odt_no      = dto_data[0];
    *daq_list_no = (uint16) ((uint16) dto_data[2] | ((uint16) dto_data[3] << 8U));
    *data_idx    = 4U;
    return E_OK;
#endif
    (void)ch; (void)dto_length;
    return E_NOT_OK;
}

/* event_channel_number -> config table index. Returns XCP_MAX_EVENT_CHANNEL if not found. */
static inline uint8 Xcp_EventChannelIdx(const Xcp_Channel *ch, uint16 number)
{
    uint8 i;

    for (i = 0U; i < XCP_MAX_EVENT_CHANNEL; i++)
    {
        if (ch->config_ptr->xcp_event_channel[i].event_channel_number == number)
        {
            return i;
        }
    }
    return XCP_MAX_EVENT_CHANNEL;
}

static inline uint8 Xcp_TimestampSize(void)
{
#if (XCP_TIMESTAMP_TYPE == XCP_FOUR_BYTE)
    return 4U;
#elif (XCP_TIMESTAMP_TYPE == XCP_TWO_BYTE)
    return 2U;
#elif (XCP_TIMESTAMP_TYPE == XCP_ONE_BYTE)
    return 1U;
#else
    return 0U;
#endif
}

static inline uint8 Xcp_WriteTimestamp(uint32 ts, uint8 *buf)
{
    uint8 n = 0U;
#if (XCP_TIMESTAMP_TYPE == XCP_FOUR_BYTE)
    buf[n++] = (uint8) (ts & 0xFFU);
    buf[n++] = (uint8) ((ts >> 8U) & 0xFFU);
    buf[n++] = (uint8) ((ts >> 16U) & 0xFFU);
    buf[n++] = (uint8) ((ts >> 24U) & 0xFFU);
#elif (XCP_TIMESTAMP_TYPE == XCP_TWO_BYTE)
    buf[n++] = (uint8) (ts & 0xFFU);
    buf[n++] = (uint8) ((ts >> 8U) & 0xFFU);
#elif (XCP_TIMESTAMP_TYPE == XCP_ONE_BYTE)
    buf[n++] = (uint8) (ts & 0xFFU);
#else
    (void)ts; (void)buf;
#endif
    return n;
}

#endif

#endif
