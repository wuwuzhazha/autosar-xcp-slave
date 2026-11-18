#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_CalPage.h"
#include "Xcp_SeedKey.h"
#include "Xcp_Callback.h"

#include <string.h>

#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)

extern Xcp_Channel g_xcp_channel;

Xcp_SegmentType Xcp_SegmentRuntime[XCP_MAX_SEGMENT];

#define XCP_SEG_CFG  (g_xcp_channel.config_ptr->xcp_segment_config)

sint16 Xcp_CmdSetCalPage(Xcp_Channel *ch, const uint8 *data, uint8 length, Xcp_ResFrame *res)
{
    uint8 mode           = 0U;
    uint8 segment_number = 0U;
    uint8 page_number    = 0U;

    (void)length;

    mode           = data[1];
    segment_number = data[2];
    page_number    = data[3];

    if ((mode & (uint8)~(XCP_PAGE_MODE_ECU | XCP_PAGE_MODE_XCP | XCP_PAGE_MODE_ALL)) != 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(&g_xcp_channel, XCP_RESOURCE_CAL_PAG))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    if (segment_number >= XCP_MAX_SEGMENT)
    {
        return Xcp_ResErr(res, XCP_ERR_SEGMENT_NOT_VALID);
    }

    if ((mode & XCP_PAGE_MODE_ALL) != 0U)
    {

        for (uint8 i = 0U; i < XCP_MAX_SEGMENT; i++)
        {
            if (page_number >= XCP_SEG_CFG[i].num_pages)
            {
                return Xcp_ResErr(res, XCP_ERR_PAGE_NOT_VALID);
            }
        }
    }
    else
    {
        if (page_number >= XCP_SEG_CFG[segment_number].num_pages)
        {
            return Xcp_ResErr(res, XCP_ERR_PAGE_NOT_VALID);
        }
    }

    if ((mode & XCP_PAGE_MODE_ECU) != 0U)
    {
        if ((mode & XCP_PAGE_MODE_ALL) != 0U)
        {
            for (uint8 i = 0U; i < XCP_MAX_SEGMENT; i++)
            {
                Xcp_SegmentRuntime[i].active_page_ecu = page_number;
            }
        }
        else
        {
            Xcp_SegmentRuntime[segment_number].active_page_ecu = page_number;
        }
    }

    if ((mode & XCP_PAGE_MODE_XCP) != 0U)
    {
        if ((mode & XCP_PAGE_MODE_ALL) != 0U)
        {
            for (uint8 i = 0U; i < XCP_MAX_SEGMENT; i++)
            {
                Xcp_SegmentRuntime[i].active_page_xcp = page_number;
            }
        }
        else
        {
            Xcp_SegmentRuntime[segment_number].active_page_xcp = page_number;
        }
    }

    ch->runtime_ptr->active_segment  = segment_number;
    ch->runtime_ptr->active_page_ecu = Xcp_SegmentRuntime[segment_number].active_page_ecu;
    ch->runtime_ptr->active_page_xcp = Xcp_SegmentRuntime[segment_number].active_page_xcp;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdGetCalPage(Xcp_Channel *ch, const uint8 *data, uint8 length, Xcp_ResFrame *res)
{
    uint8 mode           = 0U;
    uint8 segment_number = 0U;
    uint8 page_number    = 0U;

    (void)length;

    mode           = data[1];
    segment_number = data[2];

    if (segment_number >= XCP_MAX_SEGMENT)
    {
        return Xcp_ResErr(res, XCP_ERR_SEGMENT_NOT_VALID);
    }

    if (((mode & (XCP_PAGE_MODE_ECU | XCP_PAGE_MODE_XCP)) == 0U)
        || ((mode & (uint8)~(XCP_PAGE_MODE_ECU | XCP_PAGE_MODE_XCP)) != 0U))
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

    if ((mode & (XCP_PAGE_MODE_ECU | XCP_PAGE_MODE_XCP)) == (XCP_PAGE_MODE_ECU | XCP_PAGE_MODE_XCP))
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

    if ((mode & XCP_PAGE_MODE_ECU) != 0U)
    {
        page_number = Xcp_SegmentRuntime[segment_number].active_page_ecu;
    }
    else
    {
        page_number = Xcp_SegmentRuntime[segment_number].active_page_xcp;
    }

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = 0x00U;
    ch->storage_ptr->cto_buffer[2] = 0x00U;
    ch->storage_ptr->cto_buffer[3] = page_number;

    return (sint16)(4U);
}

sint16 Xcp_CmdGetPagProcessorInfo(Xcp_Channel *ch, const uint8 *data, uint8 length, Xcp_ResFrame *res)
{
    (void)data;
    (void)length;

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = XCP_MAX_SEGMENT;

    ch->storage_ptr->cto_buffer[2] = 0x00U;

    return (sint16)(3U);
}

sint16 Xcp_CmdGetSegmentInfo(Xcp_Channel *ch, const uint8 *data, uint8 length, Xcp_ResFrame *res)
{
    uint8  mode;
    uint8  segment_number;
    uint8  segment_info;
    uint32 basic_info;

    (void)length;

    mode           = data[1];
    segment_number = data[2];
    segment_info   = data[3];

    if (segment_number >= XCP_MAX_SEGMENT)
    {
        return Xcp_ResErr(res, XCP_ERR_SEGMENT_NOT_VALID);
    }

    if (mode == 0U)
    {

        if (segment_info == 0U)
        {
            basic_info = XCP_SEG_CFG[segment_number].start_address;
        }
        else if (segment_info == 1U)
        {
            basic_info = XCP_SEG_CFG[segment_number].length;
        }
        else
        {
            return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
        }

        ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
        ch->storage_ptr->cto_buffer[1] = 0x00U;
        ch->storage_ptr->cto_buffer[2] = 0x00U;
        ch->storage_ptr->cto_buffer[3] = 0x00U;
        Xcp_LeWrite32(&ch->storage_ptr->cto_buffer[4U], basic_info);

        return (sint16)(8U);
    }
    else if (mode == 1U)
    {

        ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
        ch->storage_ptr->cto_buffer[1] = XCP_SEG_CFG[segment_number].num_pages;
        ch->storage_ptr->cto_buffer[2] = 0x00U;
        ch->storage_ptr->cto_buffer[3] = 0x00U;
        ch->storage_ptr->cto_buffer[4] = 0x00U;
        ch->storage_ptr->cto_buffer[5] = 0x00U;

        return (sint16)(6U);
    }
    else if (mode == 2U)
    {
        /* mode2 = MAPPING_INFO: address_extension + mapping_addr (页起始物理) */
        uint32 page_len = (XCP_SEG_CFG[segment_number].num_pages != 0U)
                          ? (XCP_SEG_CFG[segment_number].length
                             / XCP_SEG_CFG[segment_number].num_pages) : 0U;
        uint32 map_addr = XCP_SEG_CFG[segment_number].start_address
                          + ((uint32)Xcp_SegmentRuntime[segment_number].active_page_xcp * page_len);

        ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
        ch->storage_ptr->cto_buffer[1] = 0x00U;
        ch->storage_ptr->cto_buffer[2] = 0x00U;
        ch->storage_ptr->cto_buffer[3] = (uint8)((segment_number + 1U) << 4U);
        Xcp_LeWrite32(&ch->storage_ptr->cto_buffer[4U], map_addr);

        return (sint16)(8U);
    }
    else
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }
}

sint16 Xcp_CmdGetPageInfo(Xcp_Channel *ch, const uint8 *data, uint8 length, Xcp_ResFrame *res)
{
    uint8 segment_number;
    uint8 page_number;

    (void)length;

    segment_number = data[2];
    page_number    = data[3];

    if (segment_number >= XCP_MAX_SEGMENT)
    {
        return Xcp_ResErr(res, XCP_ERR_SEGMENT_NOT_VALID);
    }

    if (page_number >= XCP_SEG_CFG[segment_number].num_pages)
    {
        return Xcp_ResErr(res, XCP_ERR_PAGE_NOT_VALID);
    }

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;

    {
        uint8 properties = (uint8) (XCP_PAGE_PROP_ECU_ACCESS_WITHOUT_XCP
                                    | XCP_PAGE_PROP_ECU_ACCESS_WITH_XCP
                                    | XCP_PAGE_PROP_XCP_READ_ACCESS_WITHOUT_ECU
                                    | XCP_PAGE_PROP_XCP_READ_ACCESS_WITH_ECU);

        if (page_number != 0U)
        {
            properties |= (uint8) (XCP_PAGE_PROP_XCP_WRITE_ACCESS_WITHOUT_ECU
                                   | XCP_PAGE_PROP_XCP_WRITE_ACCESS_WITH_ECU);
        }

        ch->storage_ptr->cto_buffer[1] = properties;
    }

    ch->storage_ptr->cto_buffer[2] = segment_number;

    return (sint16)(3U);
}

sint16 Xcp_CmdSetSegmentMode(Xcp_Channel *ch, const uint8 *data, uint8 length, Xcp_ResFrame *res)
{
    uint8 mode;
    uint8 segment_number;

    (void)length;

    mode           = data[1];
    segment_number = data[2];

    if (segment_number >= XCP_MAX_SEGMENT)
    {
        return Xcp_ResErr(res, XCP_ERR_SEGMENT_NOT_VALID);
    }

    if (mode != 0x00U)
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdGetSegmentMode(Xcp_Channel *ch, const uint8 *data, uint8 length, Xcp_ResFrame *res)
{
    uint8 segment_number;

    (void)length;

    segment_number = data[2];

    if (segment_number >= XCP_MAX_SEGMENT)
    {
        return Xcp_ResErr(res, XCP_ERR_SEGMENT_NOT_VALID);
    }

    ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
    ch->storage_ptr->cto_buffer[1] = 0x00U;
    ch->storage_ptr->cto_buffer[2] = 0x00U;

    return (sint16)(3U);
}

sint16 Xcp_CmdCopyCalPage(Xcp_Channel *ch, const uint8 *data, uint8 length, Xcp_ResFrame *res)
{
    uint8 src_segment;
    uint8 src_page;
    uint8 dst_segment;
    uint8 dst_page;

    (void)length;

    src_segment = data[1];
    src_page    = data[2];
    dst_segment = data[3];
    dst_page    = data[4];

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(&g_xcp_channel, XCP_RESOURCE_CAL_PAG))
    {
        return Xcp_ResErr(res, XCP_ERR_ACCESS_LOCKED);
    }
#endif

    if ((src_segment >= XCP_MAX_SEGMENT) || (dst_segment >= XCP_MAX_SEGMENT))
    {
        return Xcp_ResErr(res, XCP_ERR_SEGMENT_NOT_VALID);
    }

    if ((src_page >= XCP_SEG_CFG[src_segment].num_pages) ||
        (dst_page >= XCP_SEG_CFG[dst_segment].num_pages))
    {
        return Xcp_ResErr(res, XCP_ERR_PAGE_NOT_VALID);
    }

    if (dst_page == 0U)
    {
        return Xcp_ResErr(res, XCP_ERR_WRITE_PROTECTED);
    }

    if (Xcp_CopyCalPage_Callout(src_segment, src_page, dst_segment, dst_page) == E_OK)
    {
        ch->storage_ptr->cto_buffer[0] = XCP_PID_RES;
        return (sint16)(1U);
    }
    else
    {
        return Xcp_ResErr(res, XCP_ERR_GENERIC);
    }
}

const Xcp_SegmentConfigType * Xcp_GetSegmentConfig(uint8 segment_number)
{
    if (segment_number >= XCP_MAX_SEGMENT)
    {
        return NULL;
    }

    return &XCP_SEG_CFG[segment_number];
}

void Xcp_InitCalPage(Xcp_Channel *ch)
{
    if (ch->runtime_ptr == NULL)
    {
        return;
    }

    ch->runtime_ptr->active_segment  = 0U;
    ch->runtime_ptr->active_page_ecu = Xcp_SegmentRuntime[0].active_page_ecu;
    ch->runtime_ptr->active_page_xcp = Xcp_SegmentRuntime[0].active_page_xcp;
}

#endif
