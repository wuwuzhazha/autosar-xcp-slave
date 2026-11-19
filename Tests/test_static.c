#include <stdio.h>
#include <string.h>
#include "StandardTypes.h"
#include "ComStack_Types.h"
#include "Xcp.h"
#include "Xcp_Internal.h"
#include "CanIf.h"
#include "Det.h"
#include "NvM.h"
#include "Os.h"
#include "Xcp_Callback.h"
#include "test_stubs.h"
extern uint32 test_tx_count;
extern uint32 test_schm_enter_count;
extern uint32 test_schm_exit_count;
extern uint32 test_tx_pop(uint8 *out, uint32 *len);
extern const Xcp_ConfigType xcp_config;
extern uint32 test_os_ticks;
static uint32 test_ram_var32;
static int pass_count = 0;
static int fail_count = 0;
#define CHECK(name, cond)                                                  \
    do {                                                                   \
        if (cond) { pass_count++; printf("  PASS  %s\n", name); }          \
        else      { fail_count++; printf("  FAIL  %s  (line %d)\n", name, __LINE__); } \
    } while (0)
static uint32 xcp_cmd(const uint8 *cmd, uint32 len, uint8 *res)
{
    PduInfoType info;
    info.sdu_data_ptr  = (uint8 *)cmd;
    info.sdu_length    = len;
    info.meta_data_ptr = NULL;
    Xcp_CanIfRxIndication(0U, &info);
    Xcp_CanIfTxConfirmation(0U, E_OK);
    Xcp_MainFunction();
    return test_tx_pop(res, &len) > 0U ? len : 0U;
}
static void xcp_drain(void)
{
    uint8 t[64]; uint32 l;
    while (test_tx_pop(t, &l) > 0U) { }
}

/* Re-point every statically-configured ODT entry at test_ram so the
   static list's auto-running DAQ doesn't dereference unmapped addresses.
   Re-applied after FREE_DAQ because Xcp_FreeDaqLists reloads the cfg. */
static void test_patch_static_addrs(void)
{
#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_STATIC) && (XCP_MIN_DAQ > 0U)
    uint16 li;
    for (li = 0U; li < g_xcp_channel.runtime_ptr->daq_count; li++)
    {
        uint8 o;
        for (o = 0U; o < g_xcp_channel.storage_ptr->daq_list[li].odt_count; o++)
        {
            uint8 e;
            for (e = 0U; e < g_xcp_channel.storage_ptr->daq_list[li].odt[o].odt_entry_count; e++)
            {
                g_xcp_channel.storage_ptr->daq_list[li].odt[o].odt_entry[e].address =
                    TEST_ADDR(test_ram_var32);
            }
        }
    }
#endif
}

int main(void)
{
    uint8 res[64];
    uint32 l;
    (void)TEST_ADDR(test_ram_var32);
    printf("================ XCP static DAQ test ================\n");
    Xcp_Init(NULL_PTR);
    test_patch_static_addrs();

    CHECK("S1 daq_count == XCP_MIN_DAQ at init",
          g_xcp_channel.runtime_ptr->daq_count == XCP_MIN_DAQ);
    CHECK("S2 list0 loaded: CONFIGURED, event0, dir DAQ, 1 ODT x 2 entries",
          (g_xcp_channel.storage_ptr->daq_list[0].state == XCP_DAQ_STATE_CONFIGURED)
          && (g_xcp_channel.storage_ptr->daq_list[0].event_channel == 0U)
          && (g_xcp_channel.storage_ptr->daq_list[0].odt_count == 1U)
          && (g_xcp_channel.storage_ptr->daq_list[0].odt[0].odt_entry_count == 2U));
    CHECK("S3 list0 entries from config (patched addr, size4 / size2)",
          (g_xcp_channel.storage_ptr->daq_list[0].odt[0].odt_entry[0].address
              == TEST_ADDR(test_ram_var32))
          && (g_xcp_channel.storage_ptr->daq_list[0].odt[0].odt_entry[0].size == 4U)
          && (g_xcp_channel.storage_ptr->daq_list[0].odt[0].odt_entry[1].size == 2U));
    CHECK("S4 list0 event_fixed=1 from config",
          g_xcp_channel.storage_ptr->daq_list[0].event_fixed == 1U);

    l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
    CHECK("S5 CONNECT -> RES", (l == 8U) && (res[0] == XCP_PID_RES));
    g_xcp_channel.runtime_ptr->protection_status = 0U;  /* bypass seed/key for static-only asserts */

    l = xcp_cmd((const uint8 *)"\xD8\x00\x00\x00", 4U, res);
    CHECK("S6 list0 info: PREDEFINED|EVENT_FIXED|DAQ",
          (l == 6U) && (res[1] == 0x07U));

    /* Dynamic commands are rejected for DAQ_STATIC; FREE_DAQ stays a no-op
       that reloads the static config (slaves may allow it as "return to
       defaults"). */
    l = xcp_cmd((const uint8 *)"\xD6", 1U, res);
    CHECK("S7 FREE_DAQ -> RES, daq_count reverts to XCP_MIN_DAQ",
          (l == 1U) && (res[0] == XCP_PID_RES)
          && (g_xcp_channel.runtime_ptr->daq_count == XCP_MIN_DAQ));
    test_patch_static_addrs();
    l = xcp_cmd((const uint8 *)"\xD5\x00\x01\x00", 4U, res);
    CHECK("S8 ALLOC_DAQ -> ERR_DAQ_CONFIG for static", (l == 2U) && (res[1] == XCP_ERR_DAQ_CONFIG));
    l = xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
    CHECK("S9 ALLOC_ODT list0 -> ERR_DAQ_CONFIG for static",
          (l == 2U) && (res[1] == XCP_ERR_DAQ_CONFIG));
    l = xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
    CHECK("S10 ALLOC_ODT_ENTRY list0 -> ERR_DAQ_CONFIG for static",
          (l == 2U) && (res[1] == XCP_ERR_DAQ_CONFIG));

    /* WRITE_DAQ still allowed into the static list's existing entries */
    (void)xcp_cmd((const uint8 *)"\xE2\x00\x00\x00\x00\x00", 6U, res);
    {
        uint32 lo = TEST_ADDR(test_ram_var32);
        uint8 w[8] = {0xE1, 0xFF, 4, 0, (uint8)lo, (uint8)(lo >> 8), (uint8)(lo >> 16), (uint8)(lo >> 24)};
        l = xcp_cmd(w, 8U, res);
        CHECK("S11 WRITE_DAQ list0 entry0 var32 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
        l = xcp_cmd(w, 8U, res);
        CHECK("S12 WRITE_DAQ list0 entry1 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    }

    /* SET_DAQ_LIST_MODE for event_fixed list: same event OK, change rejected */
    l = xcp_cmd((const uint8 *)"\xE0\x10\x00\x00\x00\x00\x01\x00", 8U, res);
    CHECK("S13 event_fixed same event + timestamp -> RES",
          (l == 1U) && (res[0] == XCP_PID_RES));
    l = xcp_cmd((const uint8 *)"\xE0\x00\x00\x00\x00\x00\x01\x00", 8U, res);
    CHECK("S13b mode back to no-timestamp -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    l = xcp_cmd((const uint8 *)"\xE0\x00\x00\x00\x01\x00\x01\x00", 8U, res);
    CHECK("S14 event_fixed event change -> ERR_DAQ_CONFIG",
          (l == 2U) && (res[1] == XCP_ERR_DAQ_CONFIG));
    l = xcp_cmd((const uint8 *)"\xE0\x02\x00\x00\x00\x00\x01\x00", 8U, res);
    CHECK("S15 event_fixed STIM flip -> ERR_DAQ_CONFIG",
          (l == 2U) && (res[1] == XCP_ERR_DAQ_CONFIG));

    /* start list0 + event fires DTO */
    l = xcp_cmd((const uint8 *)"\xDE\x01\x00\x00", 4U, res);
    CHECK("S16 START_DAQ_LIST list0 -> RES + first_pid",
          (l == 2U) && (res[0] == XCP_PID_RES));
    test_ram_var32 = 0xCAFEBABEUL;
    xcp_drain();
    Xcp_Event(0U);
    Xcp_CanIfTxConfirmation(0U, E_OK);
    Xcp_MainFunction();
    {
        uint8 dto[64]; uint32 dl;
        CHECK("S17 static list emits DTO carrying var32",
              (test_tx_pop(dto, &dl) > 0U) && (dl >= 5U)
              && (dto[1] == 0xBEU) && (dto[2] == 0xBAU)
              && (dto[3] == 0xFEU) && (dto[4] == 0xCAU));
    }
    (void)xcp_cmd((const uint8 *)"\xDE\x00\x00\x00", 4U, res);

    CHECK("S18 SchM enter/exit paired, >0",
          (test_schm_enter_count == test_schm_exit_count)
          && (test_schm_enter_count > 0U));

    printf("================ static: %d passed, %d failed ================\n",
           pass_count, fail_count);
    return (fail_count == 0) ? 0 : 1;
}
