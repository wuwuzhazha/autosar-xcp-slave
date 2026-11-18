#include <string.h>
#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Types.h"

/* 每个编译变体都做：Init -> CONNECT -> GET_STATUS，验证内核可运行。
   不经链路壳，直接注入内部入口，与 XcpOn* 名字无关。 */

static void send_cmd(const uint8 *cmd, uint8 len)
{
    PduInfoType info;
    info.sdu_data_ptr  = (uint8 *)cmd;
    info.sdu_length    = len;
    info.meta_data_ptr = NULL_PTR;
    Xcp_HandleRxIndication(&info);
}

int main(void)
{
    static const uint8 connect[]   = {0xFFU, 0x00U};
    static const uint8 get_status[] = {0xFDU};
    uint8 i;

    Xcp_Init(NULL_PTR);

    send_cmd(connect, 2U);
    Xcp_MainFunction();

    send_cmd(get_status, 1U);
    for (i = 0U; i < 4U; i++)
    {
        Xcp_HandleTxConfirmation(E_OK);
        Xcp_MainFunction();
    }

    if (g_xcp_channel.runtime_ptr->connection_state != XCP_CONNECTED)
    {
        return 1;
    }

    return 0;
}
