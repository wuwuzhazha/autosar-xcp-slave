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
int main(void)
{
    uint8 res[64];
    uint32 l;
    (void)TEST_ADDR(test_ram_var32);
    printf("================ XCP predef DAQ test ================\n");
    Xcp_Init(NULL_PTR);
    CHECK("P1 daq_count == XCP_MIN_DAQ at init",
          g_xcp_channel.runtime_ptr->daq_count == XCP_MIN_DAQ);
    CHECK("P2 list0 loaded: CONFIGURED, event0, dir DAQ, 1 ODT x 2 entries",
          (g_xcp_channel.storage_ptr->daq_list[0].state == XCP_DAQ_STATE_CONFIGURED)
          && (g_xcp_channel.storage_ptr->daq_list[0].event_channel == 0U)
          && (g_xcp_channel.storage_ptr->daq_list[0].odt_count == 1U)
          && (g_xcp_channel.storage_ptr->daq_list[0].odt[0].odt_entry_count == 2U));
    CHECK("P3 list0 entries from config (addr 0x20000000, size4; +4, size2)",
          (g_xcp_channel.storage_ptr->daq_list[0].odt[0].odt_entry[0].address == 0x20000000UL)
          && (g_xcp_channel.storage_ptr->daq_list[0].odt[0].odt_entry[0].size == 4U)
          && (g_xcp_channel.storage_ptr->daq_list[0].odt[0].odt_entry[1].address == 0x20000004UL)
          && (g_xcp_channel.storage_ptr->daq_list[0].odt[0].odt_entry[1].size == 2U));
    CHECK("P4 list1 write_protected flag from config",
          g_xcp_channel.config_ptr->xcp_static_daq_list[1].write_protected == 1U);
    l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
    CHECK("P5 CONNECT -> RES", (l == 8U) && (res[0] == XCP_PID_RES));
    /* DAQ/STIM commands need the resource unlocked — CONNECT re-locks all */
    g_xcp_channel.runtime_ptr->protection_status &=
        (uint8)~(XCP_RESOURCE_DAQ | XCP_RESOURCE_STIM);
    l = xcp_cmd((const uint8 *)"\xD8\x00\x00\x00", 4U, res);
    CHECK("P6 list0 info: PREDEFINED|EVENT_FIXED|DAQ",
          (l == 6U) && (res[1] == 0x07U));
    l = xcp_cmd((const uint8 *)"\xD8\x00\x01\x00", 4U, res);
    CHECK("P7 list1 info: PREDEFINED|DAQ (no EVENT_FIXED)",
          (l == 6U) && (res[1] == 0x05U));
    (void)xcp_cmd((const uint8 *)"\xE2\x00\x00\x00\x00\x00", 6U, res);
    l = xcp_cmd((const uint8 *)"\xE1\xFF\x04\x00\x00\x00\x00\x10", 8U, res);
    CHECK("P8 WRITE_DAQ list0 (unprotected predefined) -> RES",
          (l == 1U) && (res[0] == XCP_PID_RES));
    (void)xcp_cmd((const uint8 *)"\xE2\x00\x01\x00\x00\x00", 6U, res);
    l = xcp_cmd((const uint8 *)"\xE1\xFF\x04\x00\x00\x00\x00\x10", 8U, res);
    CHECK("P9 WRITE_DAQ list1 (write_protected) -> ERR_WRITE_PROTECTED",
          (l == 2U) && (res[1] == XCP_ERR_WRITE_PROTECTED));
    l = xcp_cmd((const uint8 *)"\xE0\x00\x00\x00\x01\x00\x01\x00", 8U, res);
    CHECK("P10 event_fixed list0 event change -> ERR_DAQ_CONFIG",
          (l == 2U) && (res[1] == XCP_ERR_DAQ_CONFIG));
    l = xcp_cmd((const uint8 *)"\xE0\x02\x00\x00\x00\x00\x01\x00", 8U, res);
    CHECK("P11 event_fixed list0 STIM -> ERR_DAQ_CONFIG",
          (l == 2U) && (res[1] == XCP_ERR_DAQ_CONFIG));
    l = xcp_cmd((const uint8 *)"\xE0\x10\x00\x00\x00\x00\x01\x00", 8U, res);
    CHECK("P12 event_fixed list0 same event + timestamp -> RES",
          (l == 1U) && (res[0] == XCP_PID_RES));
    l = xcp_cmd((const uint8 *)"\xE0\x00\x01\x00\x01\x00\x01\x00", 8U, res);
    CHECK("P13 non-fixed list1 event change -> RES",
          (l == 1U) && (res[0] == XCP_PID_RES));
    l = xcp_cmd((const uint8 *)"\xE0\x04\x00\x00\x00\x00\x01\x00", 8U, res);
    CHECK("P14 reserved bit2 -> ERR_MODE_NOT_VALID",
          (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));
    l = xcp_cmd((const uint8 *)"\xE0\x01\x00\x00\x00\x00\x01\x00", 8U, res);
    CHECK("P15 GET-only SELECTED -> ERR_MODE_NOT_VALID",
          (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));
    l = xcp_cmd((const uint8 *)"\xE0\x40\x00\x00\x00\x00\x01\x00", 8U, res);
    CHECK("P16 GET-only RUNNING -> ERR_MODE_NOT_VALID",
          (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));
#if (XCP_RESUME_MODE_ENABLED == STD_ON)
    l = xcp_cmd((const uint8 *)"\xE0\x80\x01\x00\x01\x00\x01\x00", 8U, res);
    CHECK("P17 SET_DAQ_LIST_MODE RESUME bit -> RES",
          (l == 1U) && (res[0] == XCP_PID_RES));
    l = xcp_cmd((const uint8 *)"\xDF\x00\x01\x00", 4U, res);
    CHECK("P18 GET_DAQ_LIST_MODE shows RESUME",
          (l == 8U) && ((res[1] & 0x80U) != 0U));
#endif
    l = xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
    CHECK("P19 ALLOC_ODT predefined -> ERR_WRITE_PROTECTED",
          (l == 2U) && (res[1] == XCP_ERR_WRITE_PROTECTED));
    l = xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
    CHECK("P20 ALLOC_ODT_ENTRY predefined -> ERR_WRITE_PROTECTED",
          (l == 2U) && (res[1] == XCP_ERR_WRITE_PROTECTED));
    l = xcp_cmd((const uint8 *)"\xD5\x00\x01\x00", 4U, res);
    CHECK("P21 ALLOC_DAQ 1 -> RES (3rd list dynamic)",
          (l == 1U) && (res[0] == XCP_PID_RES)
          && (g_xcp_channel.runtime_ptr->daq_count == (XCP_MIN_DAQ + 1U)));
    l = xcp_cmd((const uint8 *)"\xE3\x00\x00\x00", 4U, res);
    CHECK("P22 CLEAR_DAQ_LIST list0 -> RES + reload config",
          (l == 1U) && (res[0] == XCP_PID_RES)
          && (g_xcp_channel.storage_ptr->daq_list[0].state == XCP_DAQ_STATE_CONFIGURED)
          && (g_xcp_channel.storage_ptr->daq_list[0].event_channel == 0U)
          && (g_xcp_channel.storage_ptr->daq_list[0].odt_count == 1U));
    l = xcp_cmd((const uint8 *)"\xE3\x00\x02\x00", 4U, res);
    CHECK("P23 CLEAR_DAQ_LIST dynamic list2 -> FREE",
          (l == 1U) && (g_xcp_channel.storage_ptr->daq_list[2].state == XCP_DAQ_STATE_FREE));
    {
        uint32 lo = TEST_ADDR(test_ram_var32);
        uint8 w[8] = {0xE1, 0xFF, 4, 0, (uint8)lo, (uint8)(lo >> 8), (uint8)(lo >> 16), (uint8)(lo >> 24)};
        (void)xcp_cmd((const uint8 *)"\xE2\x00\x00\x00\x00\x00", 6U, res);
        l = xcp_cmd(w, 8U, res);
        CHECK("P24 WRITE_DAQ list0 entry0 var32 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
        l = xcp_cmd(w, 8U, res);
        CHECK("P25 WRITE_DAQ list0 entry1 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    }
    test_ram_var32 = 0xCAFEBABEUL;
    (void)xcp_cmd((const uint8 *)"\xDE\x01\x00\x00", 4U, res);
    xcp_drain();
    Xcp_Event(0U);
    Xcp_CanIfTxConfirmation(0U, E_OK);
    Xcp_MainFunction();
    {
        uint8 dto[64]; uint32 dl;
        CHECK("P26 predefined list emits DTO with var32",
              (test_tx_pop(dto, &dl) > 0U) && (dl >= 5U)
              && (dto[1] == 0xBEU) && (dto[2] == 0xBAU)
              && (dto[3] == 0xFEU) && (dto[4] == 0xCAU));
    }
    (void)xcp_cmd((const uint8 *)"\xDE\x00\x00\x00", 4U, res);
    l = xcp_cmd((const uint8 *)"\xD6", 1U, res);
    CHECK("P27 FREE_DAQ -> RES, daq_count still XCP_MIN_DAQ",
          (l == 1U) && (res[0] == XCP_PID_RES)
          && (g_xcp_channel.runtime_ptr->daq_count == XCP_MIN_DAQ));
    CHECK("P28 list0 after FREE_DAQ: CONFIGURED ev0 (config reload)",
          (g_xcp_channel.storage_ptr->daq_list[0].state == XCP_DAQ_STATE_CONFIGURED)
          && (g_xcp_channel.storage_ptr->daq_list[0].event_channel == 0U));
    CHECK("P29 dynamic list freed",
          g_xcp_channel.storage_ptr->daq_list[2].state == XCP_DAQ_STATE_FREE);
    CHECK("P30 SchM enter/exit paired, >0",
          (test_schm_enter_count == test_schm_exit_count)
          && (test_schm_enter_count > 0U));
    printf("================ predef: %d passed, %d failed ================\n",
           pass_count, fail_count);
    return (fail_count == 0) ? 0 : 1;
}
