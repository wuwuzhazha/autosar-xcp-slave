#ifndef XCP_CAL_PAGE_H
#define XCP_CAL_PAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "StandardTypes.h"
#include "Xcp_Config.h"
#include "Xcp_Types.h"

#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)

sint16 Xcp_CmdSetCalPage(const uint8 *data, uint8 length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetCalPage(const uint8 *data, uint8 length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetPagProcessorInfo(const uint8 *data, uint8 length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetSegmentInfo(const uint8 *data, uint8 length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetPageInfo(const uint8 *data, uint8 length, Xcp_ResFrame *res);

sint16 Xcp_CmdSetSegmentMode(const uint8 *data, uint8 length, Xcp_ResFrame *res);

sint16 Xcp_CmdGetSegmentMode(const uint8 *data, uint8 length, Xcp_ResFrame *res);

sint16 Xcp_CmdCopyCalPage(const uint8 *data, uint8 length, Xcp_ResFrame *res);

const Xcp_SegmentConfigType * Xcp_GetSegmentConfig(uint8 segment_number);

void Xcp_InitCalPage(void);

#endif

#ifdef __cplusplus
}
#endif

#endif
