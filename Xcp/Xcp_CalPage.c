#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_CalPage.h"
#include "Xcp_SeedKey.h"
#include "Xcp_Callback.h"

#include <string.h>

#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)

extern Xcp_Channel g_xcp_channel;

Xcp_SegmentType Xcp_SegmentRuntime[XCP_MAX_SEGMENT];   /* 激活页运行态 */

#define XCP_SEG_CFG  (g_xcp_channel.config_ptr->xcp_segment_config)   /* 段描述表别名 */

sint16 Xcp_CmdSetCalPage(const uint8 *data, uint8 length, Xcp_ResFrame *res)
{
    uint8 mode           = 0U;
    uint8 segment_number = 0U;
    uint8 page_number    = 0U;

    (void)length;

    mode           = data[1];
    segment_number = data[2];
    page_number    = data[3];

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    if (!Xcp_IsResourceUnlocked(XCP_RESOURCE_CAL_PAG))
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

    g_xcp_channel.runtime_ptr->active_segment  = segment_number;
    g_xcp_channel.runtime_ptr->active_page_ecu = Xcp_SegmentRuntime[segment_number].active_page_ecu;
    g_xcp_channel.runtime_ptr->active_page_xcp = Xcp_SegmentRuntime[segment_number].active_page_xcp;

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdGetCalPage(const uint8 *data, uint8 length, Xcp_ResFrame *res)
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

    if ((mode & XCP_PAGE_MODE_ECU) != 0U)
    {
        page_number = Xcp_SegmentRuntime[segment_number].active_page_ecu;
    }
    else if ((mode & XCP_PAGE_MODE_XCP) != 0U)
    {
        page_number = Xcp_SegmentRuntime[segment_number].active_page_xcp;
    }
    else
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_channel.storage_ptr->cto_buffer[1] = 0x00U;
    g_xcp_channel.storage_ptr->cto_buffer[2] = 0x00U;
    g_xcp_channel.storage_ptr->cto_buffer[3] = page_number;

    return (sint16)(4U);
}

sint16 Xcp_CmdGetPagProcessorInfo(const uint8 *data, uint8 length, Xcp_ResFrame *res)
{
    (void)data;
    (void)length;

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_channel.storage_ptr->cto_buffer[1] = XCP_MAX_SEGMENT;

    g_xcp_channel.storage_ptr->cto_buffer[2] = 0x00U;

    return (sint16)(3U);
}

sint16 Xcp_CmdGetSegmentInfo(const uint8 *data, uint8 length, Xcp_ResFrame *res)
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
        /* 基本地址信息: SEGMENT_INFO 0=起始地址, 1=长度 */
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

        g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
        g_xcp_channel.storage_ptr->cto_buffer[1] = 0x00U;
        g_xcp_channel.storage_ptr->cto_buffer[2] = 0x00U;
        g_xcp_channel.storage_ptr->cto_buffer[3] = 0x00U;
        Xcp_LeWrite32(&g_xcp_channel.storage_ptr->cto_buffer[4U], basic_info);

        return (sint16)(8U);
    }
    else if (mode == 1U)
    {
        /* 标准信息: MAX_PAGES / ADDRESS_EXTENSION / MAX_MAPPING / COMPRESSION / ENCRYPTION */
        g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
        g_xcp_channel.storage_ptr->cto_buffer[1] = XCP_SEG_CFG[segment_number].num_pages;
        g_xcp_channel.storage_ptr->cto_buffer[2] = 0x00U;
        g_xcp_channel.storage_ptr->cto_buffer[3] = 0x00U;
        g_xcp_channel.storage_ptr->cto_buffer[4] = 0x00U;
        g_xcp_channel.storage_ptr->cto_buffer[5] = 0x00U;

        return (sint16)(6U);
    }
    else if (mode == 2U)
    {
        /* MAX_MAPPING = 0: 无地址映射，任何 MAPPING_INDEX 都越界 */
        return Xcp_ResErr(res, XCP_ERR_OUT_OF_RANGE);
    }
    else
    {
        return Xcp_ResErr(res, XCP_ERR_MODE_NOT_VALID);
    }
}

sint16 Xcp_CmdGetPageInfo(const uint8 *data, uint8 length, Xcp_ResFrame *res)
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

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;

    /* 页0 为参考页: ECU/XCP 均可读、XCP 不可写；其余为工作页: 全部可访问 */
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

        g_xcp_channel.storage_ptr->cto_buffer[1] = properties;
    }

    /* INIT_SEGMENT: 由本段参考页初始化 */
    g_xcp_channel.storage_ptr->cto_buffer[2] = segment_number;

    return (sint16)(3U);
}

sint16 Xcp_CmdSetSegmentMode(const uint8 *data, uint8 length, Xcp_ResFrame *res)
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

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    return (sint16)(1U);
}

sint16 Xcp_CmdGetSegmentMode(const uint8 *data, uint8 length, Xcp_ResFrame *res)
{
    uint8 segment_number;

    (void)length;

    segment_number = data[2];

    if (segment_number >= XCP_MAX_SEGMENT)
    {
        return Xcp_ResErr(res, XCP_ERR_SEGMENT_NOT_VALID);
    }

    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
    g_xcp_channel.storage_ptr->cto_buffer[1] = 0x00U;
    g_xcp_channel.storage_ptr->cto_buffer[2] = 0x00U;

    return (sint16)(3U);
}

sint16 Xcp_CmdCopyCalPage(const uint8 *data, uint8 length, Xcp_ResFrame *res)
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
    if (!Xcp_IsResourceUnlocked(XCP_RESOURCE_CAL_PAG))
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
        g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_RES;
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

void Xcp_InitCalPage(void)
{
    if (g_xcp_channel.runtime_ptr == NULL)
    {
        return;
    }

    g_xcp_channel.runtime_ptr->active_segment  = 0U;
    g_xcp_channel.runtime_ptr->active_page_ecu = Xcp_SegmentRuntime[0].active_page_ecu;
    g_xcp_channel.runtime_ptr->active_page_xcp = Xcp_SegmentRuntime[0].active_page_xcp;
}

#endif
