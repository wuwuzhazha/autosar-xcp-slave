#include <string.h>
#include <stdio.h>
#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Types.h"
#include "Xcp_Callback.h"

/* Feature-adaptive functional test: each section is #if-guarded, so trimmed
   variants only test enabled features. The build matrix uses it in place of
   matrix_main — it verifies command round-trips, not just that it runs. */

static int pass_count = 0;
static int fail_count = 0;

#define CHECK(name, cond)                                                  \
    do                                                                     \
    {                                                                      \
        if (cond) { pass_count++; }                                        \
        else      { fail_count++; printf("FAIL %s\n", name); }            \
    } while (0)

uint32 test_tx_pop(uint8 *out, uint32 *len);

static void inject(const uint8 *cmd, uint8 len)
{
    PduInfoType info;
    info.sdu_data_ptr  = (uint8 *)cmd;
    info.sdu_length    = len;
    info.meta_data_ptr = NULL_PTR;
    Xcp_HandleRxIndication(&info);
}

static uint32 reply(uint8 *res)
{
    uint8  f[64];
    uint32 fl;
    uint32 out = 0U;
    uint8  i;

    for (i = 0U; i < 8U; i++)
    {
        Xcp_HandleTxConfirmation(E_OK);
        Xcp_MainFunction();
    }
    while (test_tx_pop(f, &fl) > 0U)
    {
        if ((fl > 0U) && ((f[0] == XCP_PID_RES) || (f[0] == XCP_PID_ERR)))
        {
            (void)memcpy(res, f, fl);
            out = fl;
        }
    }
    return out;
}

static uint32 do_cmd(const uint8 *cmd, uint8 len, uint8 *res)
{
    inject(cmd, len);
    return reply(res);
}

static uint8 ram_var[64];

int main(void)
{
    uint8  res[64];
    uint32 l;
    uint8  cmd[16];

    Xcp_Init(NULL_PTR);

    l = do_cmd((const uint8 *)"\xFF\x00", 2U, res);
    CHECK("connect", (l == 8U) && (res[0] == XCP_PID_RES));
    CHECK("connected state", g_xcp_channel.runtime_ptr->connection_state == XCP_CONNECTED);

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    /* Unlock all resources so subsequent commands are usable */
    {
        static const uint8 rsc[3] = {0x01U, 0x04U, 0x10U};  /* CAL_PAG, DAQ, PGM */
        uint8 k;
        for (k = 0U; k < 3U; k++)
        {
            uint8 unlock[64];
            uint8 seed_len;
            l = do_cmd((const uint8[]){0xF8, 0x00, rsc[k]}, 3U, res);
            if ((l < 2U) || (res[0] != XCP_PID_RES))
            {
                continue;
            }
            seed_len = res[1];
            if (seed_len > 60U)
            {
                seed_len = 60U;
            }
            unlock[0] = 0xF7;
            unlock[1] = seed_len;
            {
                uint8 ui;
                for (ui = 0U; ui < seed_len; ui++)
                {
                    unlock[2 + ui] = (uint8)(~res[2 + ui]);
                }
            }
            (void)do_cmd(unlock, (uint32)(2U + seed_len), res);
        }
    }
#endif

    l = do_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("get_status", (l == 6U) && (res[0] == XCP_PID_RES));

    /* SET_MTA -> ram_var */
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    (void)memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)ram_var}, 4U);
    l = do_cmd(cmd, 8U, res);
    CHECK("set_mta", (l == 1U) && (res[0] == XCP_PID_RES));

#if (XCP_SEED_KEY_ENABLED == STD_OFF)
    /* With seed&key disabled, resources are always unlocked; DOWNLOAD works directly */
#endif
    cmd[0] = 0xF0; cmd[1] = 4; cmd[2] = 0x11; cmd[3] = 0x22; cmd[4] = 0x33; cmd[5] = 0x44;
    l = do_cmd(cmd, 6U, res);
    CHECK("download", (l == 1U) && (res[0] == XCP_PID_RES)
          && (ram_var[0] == 0x11U) && (ram_var[3] == 0x44U));

    /* After DOWNLOAD the MTA advances by 4; UPLOAD reads the advanced position */
    cmd[0] = 0xF5; cmd[1] = 4;
    l = do_cmd(cmd, 2U, res);
    CHECK("upload advances mta", (l == 5U) && (res[0] == XCP_PID_RES));

    /* SET_MTA again to read back the address just written */
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    (void)memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)ram_var}, 4U);
    (void)do_cmd(cmd, 8U, res);
    cmd[0] = 0xF5; cmd[1] = 4;
    l = do_cmd(cmd, 2U, res);
    CHECK("upload reads back", (l == 5U) && (res[0] == XCP_PID_RES) && (res[1] == 0x11U));

#if (XCP_DAQ_ENABLED == STD_ON)
    l = do_cmd((const uint8 *)"\xDA", 1U, res);   /* GET_DAQ_PROCESSOR_INFO */
    CHECK("daq_processor_info", (l == 8U) && (res[0] == XCP_PID_RES));

    cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0;   /* ALLOC_DAQ list0 */
    l = do_cmd(cmd, 4U, res);
#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_STATIC)
    CHECK("alloc_daq static rejected", (l == 2U) && (res[0] == XCP_PID_ERR));
#else
    CHECK("alloc_daq", (l == 1U) && (res[0] == XCP_PID_RES));
#endif
#endif

#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)
    l = do_cmd((const uint8 *)"\xCE", 1U, res);   /* GET_PGM_PROCESSOR_INFO */
    CHECK("pgm_processor_info", (l == 3U) && (res[0] == XCP_PID_RES));
#endif

#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)
    l = do_cmd((const uint8 *)"\xE9", 1U, res);   /* GET_PAG_PROCESSOR_INFO */
    CHECK("pag_processor_info", (l == 3U) && (res[0] == XCP_PID_RES));
#endif

    l = do_cmd((const uint8 *)"\xFE", 1U, res);   /* DISCONNECT */
    CHECK("disconnect", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("disconnected state", g_xcp_channel.runtime_ptr->connection_state == XCP_DISCONNECTED);

    printf("matrix_func: %d passed, %d failed\n", pass_count, fail_count);
    return (fail_count != 0) ? 1 : 0;
}
