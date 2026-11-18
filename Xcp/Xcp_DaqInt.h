#ifndef XCP_DAQ_INT_H
#define XCP_DAQ_INT_H

/* DAQ 内部共享：cfg/run/stim 三个编译单元共用的声明与宏。
   不对外；只被 Xcp_Daq*.c 包含。 */

#include "Xcp_Daq.h"
#include "Xcp_Types.h"
#include "Xcp_Config.h"
#include "Xcp_Internal.h"

#if (XCP_DAQ_ENABLED == STD_ON)

/* ODT0 是否携带时间戳：TIMESTAMP_FIXED 时恒带，否则看 DAQ 列表 mode */
#if (XCP_TIMESTAMP_TYPE == XCP_NO_TIME_STAMP)
#define XCP_DAQ_HAS_TIMESTAMP(daq) (FALSE)
#elif (XCP_TIMESTAMP_FIXED == 1U)
#define XCP_DAQ_HAS_TIMESTAMP(daq) (TRUE)
#else
#define XCP_DAQ_HAS_TIMESTAMP(daq) ((((daq)->mode) & XCP_DAQ_MODE_TIMESTAMP) != 0U)
#endif

/* 运行态活动 DAQ 索引：run 写（RebuildActiveDaqList），stim 读（ProcessStim 遍历）。 */
extern uint16 Xcp_ActiveDaqIndices[XCP_MAX_DAQ];
extern uint16 Xcp_ActiveDaqCount;

/* 绝对 ODT PID：编码和解码走同一前缀和定义，避免两套算法。 */
static inline uint16 Xcp_AbsPidEncode(uint16 daq_list_no, uint8 odt_no)
{
    uint16 abs_id = 0U;
    uint16 d_idx;

    for (d_idx = 0U; d_idx < daq_list_no; d_idx++)
    {
        abs_id += g_xcp_manager.storage_ptr->daq_list[d_idx].odt_count;
    }
    return (uint16)(abs_id + odt_no);
}

static inline Std_ReturnType Xcp_AbsPidDecode(uint16 abs_odt, uint16 *daq_list_no, uint8 *odt_no)
{
    uint16 prefix = 0U;
    uint16 d_idx;

    for (d_idx = 0U; d_idx < g_xcp_manager.runtime_ptr->daq_count; d_idx++)
    {
        uint16 cnt = (uint16) g_xcp_manager.storage_ptr->daq_list[d_idx].odt_count;

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

#endif /* XCP_DAQ_ENABLED */

#endif /* XCP_DAQ_INT_H */
