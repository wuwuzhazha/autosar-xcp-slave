/* Minimal XCP slave bring-up.
 *
 * Builds against the `can` generated profile and the host stubs under
 * `Tests/stubs/`. Feeds CONNECT + GET_STATUS + GET_DAQ_CLOCK through the
 * internal entry point and prints the response frames the slave produces.
 *
 * Build (from repo root):
 *   gcc -std=c99 -g -Wall -Wextra -Wno-unused-parameter \
 *       -Werror=implicit-function-declaration \
 *       -ITests/stubs -ITests -IXcp -IGen/can \
 *       -include Tests/test_config.h \
 *       Xcp/Xcp.c Xcp/Xcp_Cmd.c Xcp/Xcp_Std.c Xcp/Xcp_DaqCfg.c \
 *       Xcp/Xcp_DaqRun.c Xcp/Xcp_DaqStim.c Xcp/Xcp_SeedKey.c \
 *       Xcp/Xcp_CalPage.c Xcp/Xcp_FlashProg.c Xcp/Xcp_Resume.c \
 *       Xcp/XcpOnCan.c Xcp/XcpOnFr.c Xcp/XcpOnEth.c Xcp/XcpOnCdd.c \
 *       Tests/test_stubs.c Gen/can/Xcp_Cfg.c \
 *       examples/minimal/main.c -o examples/minimal/xcp_minimal
 *   ./examples/minimal/xcp_minimal
 */

#include <stdio.h>
#include <string.h>
#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Types.h"

/* Pull the test-side TX ring so we can print what the slave transmitted. */
extern uint32 test_tx_pop(uint8 *out, uint32 *len);

static void send_cmd(const uint8 *cmd, uint8 len)
{
    PduInfoType info;
    info.sdu_data_ptr  = (uint8 *)cmd;
    info.sdu_length    = len;
    info.meta_data_ptr = NULL_PTR;
    Xcp_HandleRxIndication(&info);
}

static void drain_and_print(const char *label)
{
    uint8  frame[64];
    uint32 len;

    while (test_tx_pop(frame, &len) > 0U)
    {
        uint32 i;
        printf("%-18s", label);
        for (i = 0U; i < len; i++) { printf(" %02X", frame[i]); }
        printf("\n");
    }
}

int main(void)
{
    static const uint8 connect[]      = {0xFFU, 0x00U};
    static const uint8 get_status[]   = {0xFDU};
    static const uint8 get_comm_info[] = {0xFBU};
    static const uint8 get_daq_clock[] = {0xDCU};
    static const uint8 get_pag_info[]  = {0xE9U};
    static const uint8 disconnect[]   = {0xFEU};

    Xcp_Init(NULL_PTR);

    printf("--- XCP slave bring-up (CAN profile) ---\n");

    send_cmd(connect, (uint8) sizeof(connect));
    Xcp_MainFunction();
    Xcp_HandleTxConfirmation(E_OK);
    drain_and_print("CONNECT");

    send_cmd(get_status, (uint8) sizeof(get_status));
    Xcp_MainFunction();
    Xcp_HandleTxConfirmation(E_OK);
    drain_and_print("GET_STATUS");

    send_cmd(get_comm_info, (uint8) sizeof(get_comm_info));
    Xcp_MainFunction();
    Xcp_HandleTxConfirmation(E_OK);
    drain_and_print("GET_COMM_MODE_INFO");

    send_cmd(get_pag_info, (uint8) sizeof(get_pag_info));
    Xcp_MainFunction();
    Xcp_HandleTxConfirmation(E_OK);
    drain_and_print("GET_PAG_PROCESSOR_INFO");

    send_cmd(get_daq_clock, (uint8) sizeof(get_daq_clock));
    Xcp_MainFunction();
    Xcp_HandleTxConfirmation(E_OK);
    drain_and_print("GET_DAQ_CLOCK");

    send_cmd(disconnect, (uint8) sizeof(disconnect));
    Xcp_MainFunction();
    Xcp_HandleTxConfirmation(E_OK);
    drain_and_print("DISCONNECT");

    if (g_xcp_channel.runtime_ptr->connection_state != XCP_DISCONNECTED)
    {
        fprintf(stderr, "FAIL: session did not close\n");
        return 1;
    }

    printf("--- all frames emitted ---\n");
    return 0;
}
