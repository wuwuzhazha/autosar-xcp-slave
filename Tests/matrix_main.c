#include <string.h>
#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Types.h"

/* Every build variant runs: Init -> CONNECT -> GET_STATUS, to verify the kernel works.
   Inject directly into the internal entry point, bypassing the link shell;
   independent of the XcpOn* names. */

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
