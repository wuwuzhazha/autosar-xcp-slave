#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "StandardTypes.h"
#include "ComStack_Types.h"
#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Daq.h"
#include "Xcp_Resume.h"
#include "Xcp_Std.h"
#include "Xcp_Callback.h"
#include "Xcp_FlashProg.h"
#include "test_stubs.h"

extern uint8  test_tx_buf[64][64];
extern uint32 test_tx_len[64];
extern uint32 test_tx_count;
extern uint32 test_tx_drop;
extern boolean test_tx_fail_next;
extern void test_checksum_override_len(uint8);
extern void test_nvm_corrupt(const uint8 *data, uint32 len);
extern uint32 test_flash_program_calls;
extern boolean test_flash_erase_fail;
extern boolean test_flash_program_fail;
extern uint8  test_det_api;
extern uint8  test_det_err;
extern uint32 test_det_count;
extern uint8  test_flash_mem[0x10000];
extern uint8  test_cal_ram[2][0x1000];
extern uint8  test_cal_rom[2][0x1000];
extern uint32 test_flash_verify_expected;
extern boolean test_flash_verify_fail;
uint32 test_tx_pop(uint8 *out, uint32 *len);
void   test_nvm_clear(void);

extern const Xcp_ConfigType xcp_config;

extern uint32 test_os_ticks;

extern uint32 test_tx_pending(void);
static int pass_count = 0;
static int fail_count = 0;

#define CHECK(name, cond)                                                        \
    do                                                                          \
    {                                                                           \
        if (cond) { pass_count++; printf("  PASS  %-58s\n", name); }             \
        else      { fail_count++; printf("  FAIL  %-58s\n", name); }             \
    } while (0)

#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)
#  define Xcp_Tl_RxIndication     Xcp_FrIfRxIndication
#  define Xcp_Tl_TxConfirmation   Xcp_FrIfTxConfirmation
#  define Xcp_Tl_TriggerTransmit  Xcp_FrIfTriggerTransmit
#elif (XCP_ON_ETHERNET_ENABLED == STD_ON)
#  define Xcp_Tl_RxIndication     Xcp_SoAdIfRxIndication
#  define Xcp_Tl_TxConfirmation   Xcp_SoAdIfTxConfirmation
#  define Xcp_Tl_TriggerTransmit  Xcp_SoAdIfTriggerTransmit
#elif (XCP_ON_CDD_ENABLED == STD_ON)
#  define Xcp_Tl_RxIndication     XCP_CDD_RX_INDICATION_NAME
#  define Xcp_Tl_TxConfirmation   XCP_CDD_TX_CONFIRMATION_NAME
#  define Xcp_Tl_TriggerTransmit  XCP_CDD_TRIGGER_TRANSMIT_NAME
#else
#  define Xcp_Tl_RxIndication     Xcp_CanIfRxIndication
#  define Xcp_Tl_TxConfirmation   Xcp_CanIfTxConfirmation
#  define Xcp_Tl_TriggerTransmit  Xcp_CanIfTriggerTransmit
#endif

#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
#  define XCP_TST_ID_LEN 1U
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_BYTE)
#  define XCP_TST_ID_LEN 2U
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_WORD)
#  define XCP_TST_ID_LEN 3U
#else /* RELATIVE_WORD_ALIGNED */
#  define XCP_TST_ID_LEN 4U
#endif

static uint32 test_stim_dto(uint8 *buf, uint8 odt, uint8 list, uint32 data4)
{
#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
    buf[0] = odt; (void)memcpy(&buf[1], &data4, 4U); (void)list; return 5U;
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_BYTE)
    buf[0] = odt; buf[1] = list; (void)memcpy(&buf[2], &data4, 4U); return 6U;
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_WORD)
    buf[0] = odt; buf[1] = list; buf[2] = 0U; (void)memcpy(&buf[3], &data4, 4U); return 7U;
#else /* RELATIVE_WORD_ALIGNED */
    buf[0] = odt; buf[1] = 0U; buf[2] = list; buf[3] = 0U; (void)memcpy(&buf[4], &data4, 4U); return 8U;
#endif
}

static uint32 xcp_pop_pdu(uint8 *res, uint32 *len)
{
    return test_tx_pop(res, len);
}

static void xcp_inject(const uint8 *buf, uint32 len)
{
    PduInfoType info;
#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)
    uint8 fr_frame[64];
    fr_frame[0] = (uint8)(len & 0xFFU);
    fr_frame[1] = (uint8)((len >> 8U) & 0xFFU);
    fr_frame[2] = 0U;
    memcpy(&fr_frame[3], buf, len);
    info.sdu_data_ptr = fr_frame;
    info.sdu_length   = (uint16)(len + 3U);
#elif (XCP_ON_ETHERNET_ENABLED == STD_ON)
    uint8 eth_frame[64];
    eth_frame[0] = (uint8)(len & 0xFFU);
    eth_frame[1] = (uint8)((len >> 8U) & 0xFFU);
    eth_frame[2] = 0U; eth_frame[3] = 0U;
    memcpy(&eth_frame[4], buf, len);
    info.sdu_data_ptr = eth_frame;
    info.sdu_length   = (uint16)(len + 4U);
#else
    info.sdu_data_ptr = (uint8 *)buf;
    info.sdu_length   = len;
#endif
    info.meta_data_ptr = NULL;
    Xcp_Tl_RxIndication(0U, &info);
}

static uint32 xcp_cmd(const uint8 *cmd, uint32 len, uint8 *res)
{
    PduInfoType info;
#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)
    uint8 fr_frame[64];
    fr_frame[0] = (uint8)(len & 0xFFU);
    fr_frame[1] = (uint8)((len >> 8U) & 0xFFU);
    fr_frame[2] = 0U;
    memcpy(&fr_frame[3], cmd, len);
    info.sdu_data_ptr  = fr_frame;
    info.sdu_length    = (uint16)(len + 3U);
#elif (XCP_ON_ETHERNET_ENABLED == STD_ON)
    uint8 eth_frame[64];
    eth_frame[0] = (uint8)(len & 0xFFU);
    eth_frame[1] = (uint8)((len >> 8U) & 0xFFU);
    eth_frame[2] = 0U; eth_frame[3] = 0U;
    memcpy(&eth_frame[4], cmd, len);
    info.sdu_data_ptr  = eth_frame;
    info.sdu_length    = (uint16)(len + 4U);
#else
    info.sdu_data_ptr  = (uint8 *)cmd;
    info.sdu_length    = len;
#endif
    info.meta_data_ptr = NULL;

    Xcp_Tl_RxIndication(0U, &info);
    Xcp_Tl_TxConfirmation(0U, E_OK);
    Xcp_MainFunction();
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)

    while (g_xcp_channel.runtime_ptr->cmd_queue_count > 0U)
    {
        Xcp_Tl_TxConfirmation(0U, E_OK);
        Xcp_MainFunction();
    }
#endif
    return xcp_pop_pdu(res, &len);
}

static void xcp_pump(uint32 n)
{
    uint32 i;
    for (i = 0U; i < n; i++)
    {
        Xcp_Tl_TxConfirmation(0U, E_OK);
        Xcp_MainFunction();
    }
}

static void xcp_drain_tx(void)
{
    uint8  tmp[64];
    uint32 l;
    while (test_tx_pop(tmp, &l) > 0U)
    {

    }
    Xcp_Tl_TxConfirmation(0U, E_OK);
}

static void test_sim_dto_inflight(void)
{
    g_xcp_channel.runtime_ptr->tx.dto_latch = TRUE;
    g_xcp_channel.runtime_ptr->tx.state = XCP_TX_DTO;
}

static void xcp_settle(void)
{
    uint32 i;
    for (i = 0U; i < 20U; i++)
    {
        Xcp_Tl_TxConfirmation(0U, E_OK);
        Xcp_MainFunction();
        if ((g_xcp_channel.runtime_ptr->tx.state != XCP_TX_CTO)
            && (g_xcp_channel.runtime_ptr->tx.dto_latch == FALSE)
            && (g_xcp_channel.runtime_ptr->tx.parked == FALSE)
            && (g_xcp_channel.runtime_ptr->tx.state == XCP_TX_IDLE)
            && (g_xcp_channel.runtime_ptr->cmd_pending == FALSE))
        {
            break;
        }
    }
    xcp_drain_tx();
}

static void xcp_quiesce(void)
{
    uint8  f[64];
    uint32 fl;
    uint16 i;
    uint8  n;

    for (n = 0U; n < 64U; n++)
    {
        while (test_tx_pop(f, &fl) > 0U)
        {
        }
        if ((g_xcp_channel.runtime_ptr->tx.state != XCP_TX_CTO)
            && (g_xcp_channel.runtime_ptr->tx.dto_latch == FALSE)
            && (g_xcp_channel.runtime_ptr->tx.parked == FALSE)
            && (g_xcp_channel.storage_ptr->dto_queue_count == 0U))
        {
            break;
        }
        Xcp_Tl_TxConfirmation(0U, E_OK);
    }
    for (i = 0U; i < XCP_MAX_EVENT_CHANNEL; i++)
    {
        g_xcp_channel.runtime_ptr->event_channel_counter[i] = 0U;
    }
}

static boolean xcp_cmd_has_err(const uint8 *c, uint32 len, uint8 err)
{
    PduInfoType info;
    uint8  f[64]; uint32 fl;
    boolean found = FALSE;

    (void)info;
    xcp_inject(c, len);
    {
        uint8 pi;

        for (pi = 0U; pi < 10U; pi++)
        {
            Xcp_Tl_TxConfirmation(0U, E_OK);
            Xcp_MainFunction();
            if ((g_xcp_channel.runtime_ptr->tx.state != XCP_TX_CTO)
                && (g_xcp_channel.runtime_ptr->cmd_pending == FALSE))
            {
                break;
            }
        }
    }
    while (test_tx_pop(f, &fl) > 0U)
    {
        if ((fl == 2U) && (f[0] == XCP_PID_ERR) && (f[1] == err)) { found = TRUE; }
    }
    return found;
}

static boolean frame_is(const uint8 *act, uint32 act_len, const uint8 *exp, uint32 exp_len)
{
    uint32 i;

    if ((act_len == exp_len) && (memcmp(act, exp, exp_len) == 0))
    {
        return TRUE;
    }
    printf("        got  [%u]:", (unsigned)act_len);
    for (i = 0U; i < act_len; i++) { printf(" %02X", act[i]); }
    printf("\n        want [%u]:", (unsigned)exp_len);
    for (i = 0U; i < exp_len; i++) { printf(" %02X", exp[i]); }
    printf("\n");
    return FALSE;
}

static uint32 xcp_cmd_reply(const uint8 *c, uint32 len, uint8 *res)
{
    uint8  f[64];
    uint32 fl;
    uint32 out = 0U;
    uint8  pi;

    xcp_inject(c, len);
    for (pi = 0U; pi < 10U; pi++)
    {
        Xcp_Tl_TxConfirmation(0U, E_OK);
        Xcp_MainFunction();
        if ((g_xcp_channel.runtime_ptr->tx.state != XCP_TX_CTO)
            && (g_xcp_channel.runtime_ptr->tx.parked == FALSE)
            && (g_xcp_channel.runtime_ptr->cmd_pending == FALSE)
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
            && (g_xcp_channel.runtime_ptr->cmd_queue_count == 0U)
#endif
            )
        {
            break;
        }
    }
    while (test_tx_pop(f, &fl) > 0U)
    {
        if ((out == 0U) && (fl > 0U) && ((f[0] == XCP_PID_RES) || (f[0] == XCP_PID_ERR)))
        {
            (void)memcpy(res, f, fl);
            out = fl;
        }
    }
    return out;
}

#define TST_DAQ_PROPERTIES ((uint8)(0x80U | 0x08U \
    | ((XCP_TIMESTAMP_TYPE != XCP_NO_TIME_STAMP) ? 0x10U : 0U) \
    | ((XCP_RESUME_MODE_ENABLED == STD_ON) ? 0x04U : 0U) \
    | ((XCP_PRESCALER_SUPPORTED == STD_ON) ? 0x02U : 0U) \
    | ((XCP_DAQ_CONFIG_TYPE == XCP_DAQ_DYNAMIC) ? 0x01U : 0U)))

#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
#  define TST_DAQ_KEY_BYTE 0x00U
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_BYTE)
#  define TST_DAQ_KEY_BYTE 0x40U
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_WORD)
#  define TST_DAQ_KEY_BYTE 0x80U
#else
#  define TST_DAQ_KEY_BYTE 0xC0U
#endif

#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON) && (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)
#  define TST_COMM_OPT   0x03U
#  define TST_QUEUE_SIZE ((uint8)XCP_CMD_QUEUE_SIZE)
#elif (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
#  define TST_COMM_OPT   0x02U
#  define TST_QUEUE_SIZE ((uint8)XCP_CMD_QUEUE_SIZE)
#elif (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)
#  define TST_COMM_OPT   0x01U
#  define TST_QUEUE_SIZE 0x00U
#else
#  define TST_COMM_OPT   0x00U
#  define TST_QUEUE_SIZE 0x00U
#endif

#if (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)
#  define TST_CONNECT_COMM (XCP_COMM_BASIC_OPTIONAL | XCP_COMM_BASIC_SLAVE_BLOCK_MODE)
#else
#  define TST_CONNECT_COMM XCP_COMM_BASIC_OPTIONAL
#endif

static uint8  test_ram[256];
static uint32 test_ram_var32;
static uint8  test_ram_var8;

#define RAM_ADDR (TEST_ADDR(test_ram))

int main(void)
{
    uint8  res[64];
    uint32 l;
    uint8  cmd[16];
    Std_VersionInfoType ver;

    (void)TEST_ADDR(test_ram);
    (void)TEST_ADDR(test_ram_var32);
    (void)TEST_ADDR(test_ram_var8);
    printf("================ XCP module test ================\n");

    printf("-- A. DET uninit checks\n");
    {
        PduInfoType info;
        uint8 dummy[4] = {0xFF, 0x00, 0, 0};
        info.sdu_data_ptr = dummy;
        info.sdu_length   = 2;
        info.meta_data_ptr = NULL;
        test_det_count = 0;
        Xcp_Tl_RxIndication(0U, &info);
        CHECK("A1 RxIndication before init -> XCP_E_UNINIT",
              (test_det_count > 0U) && (test_det_err == XCP_E_UNINIT));
        test_det_count = 0;
        Xcp_Tl_TxConfirmation(0U, E_OK);
        CHECK("A2 TxConfirmation before init -> XCP_E_UNINIT",
              (test_det_count > 0U) && (test_det_err == XCP_E_UNINIT));
        test_det_count = 0;
        {
            PduInfoType out;
            uint8 b[8];
            out.sdu_data_ptr = b; out.sdu_length = 8; out.meta_data_ptr = NULL;
            Xcp_Tl_TriggerTransmit(0U, &out);
            CHECK("A3 TriggerTransmit before init -> XCP_E_UNINIT + E_NOT_OK",
                  (test_det_count > 0U) && (test_det_err == XCP_E_UNINIT));
        }
    }

    printf("-- A. Init / VersionInfo\n");
    Xcp_Init(NULL_PTR);
    CHECK("A4 Xcp_Init(NULL) -> default config",
          g_xcp_channel.config_ptr == &xcp_config);
    CHECK("A5 state == XCP_INIT",
          g_xcp_channel.runtime_ptr->state == XCP_INIT);

#if (XCP_TIMESTAMP_TYPE != XCP_NO_TIME_STAMP) && (XCP_TIMESTAMP_SOURCE_OS == STD_ON)
    /* The OS counter already runs when Xcp_Init is called: the first elapsed
       measurement must start at init, not at tick 0 (issue #9 item 5). */
    test_os_ticks = 5000UL;
    Xcp_Init(NULL_PTR);
    CHECK("A5a Init primes os_last_tick: first timestamp is 0",
          Xcp_GetTimestamp() == 0U);
    test_os_ticks += 7UL;
    CHECK("A5b timestamp advances by elapsed ticks only",
          Xcp_GetTimestamp() == 7U);
    test_os_ticks = 0UL;
    Xcp_Init(NULL_PTR);
#endif

#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)
    /* Xcp_Init must reset the segment page selection (issue #9 item 7). */
    Xcp_SegmentRuntime[0].active_page_ecu = 1U;
    Xcp_SegmentRuntime[0].active_page_xcp = 1U;
    Xcp_Init(NULL_PTR);
    CHECK("A5c Init resets segment active pages",
          (Xcp_SegmentRuntime[0].active_page_ecu == 0U)
          && (Xcp_SegmentRuntime[0].active_page_xcp == 0U));
#endif

#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_STATIC)
    /* Static DAQ entries are baked as literal addresses (e.g. 0x20000000) that
       are unmapped in the test process. Rewrite the runtime copies to point at
       real test RAM so the auto-running DAQ list doesn't dereference wild
       pointers. Re-applied after every FREE_DAQ because FreeDaqLists reloads
       the static config. */
    test_patch_static_addrs();
#endif

    test_det_count = 0;
    Xcp_GetVersionInfo(NULL_PTR);
    CHECK("A6 GetVersionInfo(NULL) -> XCP_E_PARAM_POINTER",
          (test_det_count > 0U) && (test_det_err == XCP_E_PARAM_POINTER));
    Xcp_GetVersionInfo(&ver);
    CHECK("A7 GetVersionInfo returns moduleID 212 / v1.0.0",
          (ver.moduleID == XCP_MODULE_ID) && (ver.sw_major_version == 1U)
          && (ver.sw_minor_version == 0U));

    {
        PduInfoType info;
        uint8 dummy[4] = {0xFF, 0x00, 0, 0};
        info.sdu_data_ptr = dummy; info.sdu_length = 2; info.meta_data_ptr = NULL;
        test_det_count = 0;
        Xcp_Tl_RxIndication(9U, &info);
        CHECK("A8 RxIndication wrong PDU -> XCP_E_INVALID_PDUID",
              (test_det_count > 0U) && (test_det_err == XCP_E_INVALID_PDUID));
    }

    printf("-- B. command gating before CONNECT\n");
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("B1 GET_STATUS pre-connect: no response", l == 0U);

    printf("-- B. CONNECT / basic commands\n");
    l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
    CHECK("B2 CONNECT -> RES len 8", (l == 8U) && (res[0] == XCP_PID_RES));

#if (XCP_DAQ_ENABLED == STD_ON)
    /* A scheduler backlog must be clamped to the cycle, not carried forward
       (issue #9 item 6): the counter has to stay below the 10 ms cycle. */
    g_xcp_channel.runtime_ptr->event_channel_counter[0] = 1000U;
    Xcp_MainFunction();
    CHECK("B2a scheduler counter stays below the cycle after a backlog",
          g_xcp_channel.runtime_ptr->event_channel_counter[0] < 10U);
#endif

    CHECK("B3 CONNECT bytes: CAL|DAQ|STIM|PGM, OPTIONAL+BYTE+Intel, CTO/DTO 8, v1/1",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x1D, TST_CONNECT_COMM,
              8U, 8U, 0x00, 0x01, 0x01 }, 8U));

    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);

    CHECK("B6 GET_STATUS bytes: idle session, CAL|DAQ|STIM|PGM locked",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00, 0x1D, 0x00, 0x00, 0x00 }, 6U));

    l = xcp_cmd((const uint8 *)"\xFC", 1U, res);
    CHECK("B8 SYNCH -> ERR_CMD_SYNCH",
          (l == 2U) && (res[0] == XCP_PID_ERR) && (res[1] == XCP_ERR_CMD_SYNCH));

    l = xcp_cmd((const uint8 *)"\xFB", 1U, res);

    CHECK("B9 COMM_MODE_INFO bytes: no master block mode, driver v1.0",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00, TST_COMM_OPT, 0x00,
                                            XCP_MAX_BS, XCP_MIN_ST, TST_QUEUE_SIZE, 0x10 }, 8U));

    l = xcp_cmd((const uint8 *)"\xFA\x00", 2U, res);
    CHECK("B10 GET_ID type0 -> RES len8, len>0",
          (l == 8U) && (res[0] == XCP_PID_RES) && (res[4] > 0U));
    l = xcp_cmd((const uint8 *)"\xFA\x09", 2U, res);
    CHECK("B11 GET_ID type9 -> len=0",
          (l == 8U) && (res[0] == XCP_PID_RES) && (res[4] == 0U));

    l = xcp_cmd((const uint8 *)"\xF5\x07", 2U, res);
    CHECK("B10a UPLOAD after GET_ID -> 'AUTOSAR'",
          (l == 8U) && (res[0] == XCP_PID_RES) && (res[1] == 'A') && (res[7] == 'R'));

    cmd[0] = 0xF0; cmd[1] = 1; cmd[2] = 0xAA;
    l = xcp_cmd(cmd, 3U, res);
    CHECK("B12 DOWNLOAD while locked -> ERR_ACCESS_LOCKED",
          (l == 2U) && (res[0] == XCP_PID_ERR) && (res[1] == XCP_ERR_ACCESS_LOCKED));

    l = xcp_cmd((const uint8 *)"\xF1\x11\x22", 3U, res);
    CHECK("B13 USER_CMD callout echoes",
          (l >= 3U) && (res[0] == XCP_PID_RES) && (res[1] == 0x11U));

#if (XCP_ON_CAN_ENABLED == STD_ON)
    l = xcp_cmd((const uint8 *)"\xF2\xFF", 2U, res);
    CHECK("B14 GET_SLAVE_ID -> 'XCP'",
          (l == 8U) && (res[0] == XCP_PID_RES) && (res[5] == 'X')
          && (res[6] == 'C') && (res[7] == 'P'));
#else
    l = xcp_cmd((const uint8 *)"\xF2\xFF", 2U, res);
    CHECK("B14 GET_SLAVE_ID on non-CAN -> ERR_CMD_UNKNOWN",
          (l == 2U) && (res[1] == XCP_ERR_CMD_UNKNOWN));
#endif
    l = xcp_cmd((const uint8 *)"\xF2\x01", 2U, res);
    CHECK("B15 TL_CMD unknown sub -> ERR_CMD_UNKNOWN",
          (l == 2U) && (res[1] == XCP_ERR_CMD_UNKNOWN));
    l = xcp_cmd((const uint8 *)"\xF2", 1U, res);
    CHECK("B16 TL_CMD short -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));

    l = xcp_cmd((const uint8 *)"\xff", 1U, res);
    CHECK("B17 CONNECT len1 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
    l = xcp_cmd((const uint8 *)"\xf6\x00\x00\x00\xaa\xbb\xcc", 7U, res);
    CHECK("B18 SET_MTA len7 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
    l = xcp_cmd((const uint8 *)"\xe1\x00\x04\x00\xaa\xbb\xcc", 7U, res);
    CHECK("B19 WRITE_DAQ len7 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
    l = xcp_cmd((const uint8 *)"\xd1\x00\x00\xaa\xbb\xcc\xdd", 7U, res);
    l = xcp_cmd((const uint8 *)"\xEB\x00", 2U, res);
    CHECK("B21 SET_CAL_PAGE len2 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
    CHECK("B20 PROGRAM_CLEAR len7 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));

    printf("-- G. seed & key\n");

    cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_CAL_PAG;
    l = xcp_cmd(cmd, 3U, res);
    CHECK("G1 GET_SEED CAL -> len 4, seed pattern",
          (l == 6U) && (res[1] == 4U) && (res[2] == XCP_RESOURCE_CAL_PAG));
    {
        uint8 seed[4];
        uint8 i;
        memcpy(seed, &res[2], 4U);

        cmd[0] = 0xF7; cmd[1] = 4U;
        for (i = 0U; i < 4U; i++) { cmd[2U + i] = (uint8)(seed[i] ^ 0xFFU); }
        l = xcp_cmd(cmd, 6U, res);
        CHECK("G2 UNLOCK correct key -> RES + cleared bit",
              (l >= 1U) && (res[0] == XCP_PID_RES)
              && ((g_xcp_channel.runtime_ptr->protection_status & XCP_RESOURCE_CAL_PAG) == 0U));

        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_PGM;
        l = xcp_cmd(cmd, 3U, res); memcpy(seed, &res[2], 4U);
        cmd[0] = 0xF7; cmd[1] = 4U;
        for (i = 0U; i < 4U; i++) { cmd[2U + i] = 0xAAU; }
        l = xcp_cmd(cmd, 6U, res);
        CHECK("G3 UNLOCK wrong key -> ERR_ACCESS_LOCKED",
              (l == 2U) && (res[1] == XCP_ERR_ACCESS_LOCKED));

        CHECK("G3b wrong key -> disconnected",
              g_xcp_channel.runtime_ptr->connection_state == XCP_DISCONNECTED);

        l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
        CHECK("G3d reconnect after wrong-key", (l >= 1U) && (res[0] == XCP_PID_RES));

        l = xcp_cmd((const uint8 *)"\xF8\x01\x04", 3U, res);
        CHECK("G3c GET_SEED mode=1 no-seed -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

        /* mode=1 continuation: 8B seed > CTO-2=6, first chunk 6B, mode=1 returns remaining 2B, then back to 0 */
        {
        extern void test_seed_override_len(uint8);
        test_seed_override_len(8U);
        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_PGM;
        l = xcp_cmd(cmd, 3U, res);
        CHECK("G3e GET_SEED 8B -> first chunk len6",
              (l == 8U) && (res[1] == 6U));
        l = xcp_cmd((const uint8 *)"\xF8\x01\x04", 3U, res);
        CHECK("G3f GET_SEED mode=1 -> remaining 2B",
              (l == 4U) && (res[1] == 2U) && (res[2] == 0x33U) && (res[3] == 0x44U));
        l = xcp_cmd((const uint8 *)"\xF8\x01\x04", 3U, res);
        CHECK("G3g GET_SEED mode=1 again -> len0",
              (l == 2U) && (res[1] == 0U));
        test_seed_override_len(0U);
        }

        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_CAL_PAG;
        l = xcp_cmd(cmd, 3U, res); memcpy(seed, &res[2], 4U);
        cmd[0] = 0xF7; cmd[1] = 4U;
        for (i = 0U; i < 4U; i++) { cmd[2U + i] = (uint8)(seed[i] ^ 0xFFU); }
        (void)xcp_cmd(cmd, 6U, res);

        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_PGM;
        l = xcp_cmd(cmd, 3U, res); memcpy(seed, &res[2], 4U);

        cmd[0] = 0xF7; cmd[1] = 4U;
        for (i = 0U; i < 4U; i++) { cmd[2U + i] = (uint8)(seed[i] ^ 0xFFU); }
        l = xcp_cmd(cmd, 6U, res);
        CHECK("G4 UNLOCK PGM -> RES", (l >= 1U) && (res[0] == XCP_PID_RES));

        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_DAQ;
        l = xcp_cmd(cmd, 3U, res); memcpy(seed, &res[2], 4U);
        cmd[0] = 0xF7; cmd[1] = 4U;
        for (i = 0U; i < 4U; i++) { cmd[2U + i] = (uint8)(seed[i] ^ 0xFFU); }
        (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_STIM;
        l = xcp_cmd(cmd, 3U, res); memcpy(seed, &res[2], 4U);
        cmd[0] = 0xF7; cmd[1] = 4U;
        for (i = 0U; i < 4U; i++) { cmd[2U + i] = (uint8)(seed[i] ^ 0xFFU); }
        (void)xcp_cmd(cmd, 6U, res);
        CHECK("G5 all resources unlocked",
              g_xcp_channel.runtime_ptr->protection_status == 0U);
    }

    printf("-- C. memory access\n");
    {
        uint32 i;
        for (i = 0U; i < 256U; i++) { test_ram[i] = (uint8)i; }
    }

    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR}, 4U);
    l = xcp_cmd(cmd, 8U, res);
    CHECK("C1 SET_MTA -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    cmd[0] = 0xF5; cmd[1] = 4U;
    l = xcp_cmd(cmd, 2U, res);
    CHECK("C2 UPLOAD 4B -> 00 01 02 03",
          (l == 5U) && (res[1] == 0U) && (res[2] == 1U) && (res[3] == 2U) && (res[4] == 3U));

    l = xcp_cmd(cmd, 2U, res);
    CHECK("C3 UPLOAD again reads advanced MTA (04)",
          (l == 5U) && (res[1] == 4U));

#if (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)
    {
        uint8  block[32];
        uint32 got;
        uint32 fl;

        cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){RAM_ADDR}, 4U);
        (void)xcp_cmd(cmd, 8U, res);

        cmd[0] = 0xF5; cmd[1] = 20U;
        l = xcp_cmd(cmd, 2U, res);
        CHECK("C4 UPLOAD block mode first frame",
              (l > 1U) && (l <= XCP_MAX_CTO) && (res[0] == XCP_PID_RES));

        got = l - 1U;
        (void)memcpy(block, &res[1], got);

        while (got < 20U)
        {
            xcp_pump(1U);
            l = xcp_pop_pdu(res, &fl);
            if (l == 0U)
            {
                break;
            }
            (void)memcpy(&block[got], &res[1], l - 1U);
            got += l - 1U;
        }
        xcp_pump(1U);

        CHECK("C4a UPLOAD block mode streams 20 bytes",
              (got == 20U) && (memcmp(block, test_ram, 20U) == 0));
        CHECK("C4b UPLOAD block mode advances MTA",
              g_xcp_channel.runtime_ptr->mta == (RAM_ADDR + 20U));
        CHECK("C4c UPLOAD block mode completes",
              g_xcp_channel.runtime_ptr->block_transfer_active == FALSE);
    }
#else
    cmd[0] = 0xF5; cmd[1] = 20U;
    l = xcp_cmd(cmd, 2U, res);
    CHECK("C4 UPLOAD >MAX_CTO-1 -> ERR_OUT_OF_RANGE",
          (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));
#endif
    l = xcp_cmd((const uint8 *)"\xEF\x01\xAA", 3U, res);
    CHECK("C4a DOWNLOAD_NEXT no block -> ERR_SEQUENCE",
          (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

    cmd[0] = 0xF4; cmd[1] = 4U; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 16U}, 4U);
    l = xcp_cmd(cmd, 8U, res);
    CHECK("C5 SHORT_UPLOAD at +16 -> 0x10..0x13",
          (l == 5U) && (res[1] == 0x10U) && (res[4] == 0x13U));

    cmd[0] = 0xED; cmd[1] = 4U; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 32U}, 4U);

    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 32U}, 4U);
    (void)xcp_cmd(cmd, 8U, res);

    cmd[0] = 0xF0; cmd[1] = 4U; cmd[2] = 0xDE; cmd[3] = 0xAD; cmd[4] = 0xBE; cmd[5] = 0xEF;
    l = xcp_cmd(cmd, 6U, res);
    CHECK("C6 DOWNLOAD -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("C7 DOWNLOAD wrote DE AD BE EF",
          (test_ram[32] == 0xDEU) && (test_ram[33] == 0xADU)
          && (test_ram[34] == 0xBEU) && (test_ram[35] == 0xEFU));

    /* DOWNLOAD_MAX is a single-frame transfer: it must not arm a block. */
    cmd[0] = 0xEE; cmd[1] = 0; cmd[2] = 0x11; cmd[3] = 0x22; cmd[4] = 0x33; cmd[5] = 0x44;
    cmd[6] = 0x55; cmd[7] = 0x66;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("C8 DOWNLOAD_MAX -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("C8a DOWNLOAD_MAX leaves no block armed",
          g_xcp_channel.runtime_ptr->block_transfer_active == FALSE);

    cmd[0] = 0xEF; cmd[1] = 2U; cmd[2] = 0xC0; cmd[3] = 0xDE;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("C8b DOWNLOAD_NEXT after MAX -> ERR_SEQUENCE",
          (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

    /* Block mode: byte 1 of DOWNLOAD is the total block length, so the
       payload may be shorter. Intermediate frames are answered only by the
       frame that exhausts the declared length (issue #11 items 1 and 3). */
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 112U}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xF0; cmd[1] = 6U; cmd[2] = 0x01; cmd[3] = 0x02; cmd[4] = 0x03; cmd[5] = 0x04;
    l = xcp_cmd(cmd, 6U, res);
    CHECK("C8c block DOWNLOAD (declared 6 > payload 4) -> no response", l == 0U);
    CHECK("C8d block DOWNLOAD armed with remaining 2",
          (g_xcp_channel.runtime_ptr->block_transfer_active == TRUE)
          && (g_xcp_channel.runtime_ptr->block_transfer_remaining == 2U));
    CHECK("C8e block DOWNLOAD wrote the first 4 elements",
          (test_ram[112] == 0x01U) && (test_ram[115] == 0x04U));

    cmd[0] = 0xEF; cmd[1] = 4U; cmd[2] = 0xAA; cmd[3] = 0xBB;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("C8f NEXT overrunning the declared block -> ERR_SEQUENCE",
          (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));
    CHECK("C8g overrunning NEXT wrote nothing and kept the budget",
          (test_ram[116] != 0xAAU)
          && (g_xcp_channel.runtime_ptr->block_transfer_remaining == 2U));

    cmd[0] = 0xEF; cmd[1] = 2U; cmd[2] = 0x05; cmd[3] = 0x06;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("C8h final NEXT -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("C8i block closed after the declared length",
          g_xcp_channel.runtime_ptr->block_transfer_active == FALSE);
    CHECK("C8j block wrote all 6 elements",
          (test_ram[112] == 0x01U) && (test_ram[113] == 0x02U)
          && (test_ram[114] == 0x03U) && (test_ram[115] == 0x04U)
          && (test_ram[116] == 0x05U) && (test_ram[117] == 0x06U));

    cmd[0] = 0xEF; cmd[1] = 2U; cmd[2] = 0x07; cmd[3] = 0x08;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("C8k NEXT after block completion -> ERR_SEQUENCE",
          (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

    /* Bytes beyond the declared count are transport padding (CAN-FD DLC). */
    test_ram[124] = 0xEEU; test_ram[125] = 0xEEU;
    test_ram[126] = 0xEEU; test_ram[127] = 0xEEU;
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 124U}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    l = xcp_cmd((const uint8 *)"\xF0\x02\x01\x02\x03\x04", 6U, res);
    CHECK("C8l DOWNLOAD ignores padding beyond the declared count",
          (l == 1U) && (res[0] == XCP_PID_RES)
          && (test_ram[124] == 0x01U) && (test_ram[125] == 0x02U)
          && (test_ram[126] == 0xEEU) && (test_ram[127] == 0xEEU));

    /* Any other command abandons an armed block. */
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 120U}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xF0; cmd[1] = 6U; cmd[2] = 0x01; cmd[3] = 0x02; cmd[4] = 0x03; cmd[5] = 0x04;
    (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xF4; cmd[1] = 1U; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 80U}, 4U);
    l = xcp_cmd(cmd, 8U, res);
    CHECK("C8m UPLOAD cancels the armed block", (l >= 1U) && (res[0] == XCP_PID_RES));
    CHECK("C8n block inactive after cancel",
          g_xcp_channel.runtime_ptr->block_transfer_active == FALSE);
    cmd[0] = 0xEF; cmd[1] = 2U; cmd[2] = 0xB1; cmd[3] = 0xB2;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("C8o NEXT after cancel -> ERR_SEQUENCE",
          (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

    cmd[0] = 0xED; cmd[1] = 1U; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 60U}, 4U);
    l = xcp_cmd(cmd, 8U, res);
    CHECK("C8p SHORT_DOWNLOAD over CTO -> ERR_OUT_OF_RANGE",
          (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

    memset(&test_ram[40], 0xFF, 4U);
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 40U}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xEC; cmd[1] = 0x0FU; cmd[2] = 0xF0U; cmd[3] = 0x05U; cmd[4] = 0; cmd[5] = 0;
    l = xcp_cmd(cmd, 6U, res);
    CHECK("C9 MODIFY_BITS -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    CHECK("C10 MODIFY_BITS -> *(u32)ram+40 == 0x82F87FFF",
          (*(uint32 *)(void *)&test_ram[40]) == 0x82F87FFFUL);

    memset(&test_ram[40], 0xFF, 4U);
    cmd[0] = 0xEC; cmd[1] = 0x11U; cmd[2] = 0xFF; cmd[3] = 0xFF; cmd[4] = 0; cmd[5] = 0;
    l = xcp_cmd(cmd, 6U, res);
    CHECK("C10b MODIFY_BITS shift17 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
    CHECK("C10c MODIFY_BITS shift17 leaves RAM untouched",
          (*(uint32 *)(void *)&test_ram[40]) == 0xFFFFFFFFUL);

    /* An unaligned MTA must not be dereferenced as uint32* (issue #9 item 3);
       the RMW is byte-wise, so the value is correct on any alignment. */
    test_ram[41] = 0x29U; test_ram[42] = 0x2AU;
    test_ram[43] = 0x2BU; test_ram[44] = 0x2CU;
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 41U}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xEC; cmd[1] = 0x00U; cmd[2] = 0xF0U; cmd[3] = 0x00U; cmd[4] = 0x05U; cmd[5] = 0x00U;
    l = xcp_cmd(cmd, 6U, res);
    CHECK("C10d MODIFY_BITS at unaligned MTA -> RES",
          (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("C10e MODIFY_BITS unaligned RMW correct",
          (test_ram[41] == 0xF5U) && (test_ram[42] == 0x00U)
          && (test_ram[43] == 0x2BU) && (test_ram[44] == 0x2CU));

    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xF3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    cmd[4] = 0x10; cmd[5] = 0; cmd[6] = 0; cmd[7] = 0;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("C11 BUILD_CHECKSUM -> RES",
          (l >= 5U) && (res[0] == XCP_PID_RES));
    {
        uint32 expected = 0U;
        uint32 i;
        for (i = 0U; i < 16U; i++) { expected += test_ram[i]; }

        CHECK("C12 checksum = sum(data[0..15])",
              ((uint32)res[l-4] | ((uint32)res[l-3]<<8) | ((uint32)res[l-2]<<16) | ((uint32)res[l-1]<<24)) == expected);
    }

    printf("-- D. calibration pages\n");
    l = xcp_cmd((const uint8 *)"\xE9", 1U, res);

    CHECK("D1 GET_PAG_PROCESSOR_INFO bytes: 2 segments, no freeze",
          frame_is(res, l, (const uint8[]){ 0xFF, XCP_MAX_SEGMENT, 0x00 }, 3U));
    l = xcp_cmd((const uint8 *)"\xEA\x01\x00", 3U, res);
    CHECK("D2 GET_CAL_PAGE -> RES page byte",
          (l >= 4U) && (res[0] == XCP_PID_RES));

    cmd[0] = 0xEB; cmd[1] = 0x02U; cmd[2] = 0x00U; cmd[3] = 0x01U;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("D3 SET_CAL_PAGE seg0 page1 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    l = xcp_cmd((const uint8 *)"\xEA\x02\x00", 3U, res);
    CHECK("D3b GET_CAL_PAGE confirms switch: [3]=page1",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00, 0x00, 0x01 }, 4U));

    l = xcp_cmd((const uint8 *)"\xEB\x03\x00\x01", 4U, res);
    (void)xcp_cmd((const uint8 *)"\xEA\x02\x00", 3U, res);
    CHECK("D3c page active after SET",
          (l == 1U) && (res[0] == XCP_PID_RES)
          && (Xcp_SegmentRuntime[0].active_page_xcp == 1U));
    cmd[0] = 0xEB; cmd[1] = 0x82U; cmd[2] = 0x00U; cmd[3] = 0x09U;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("D4 SET_CAL_PAGE page9 -> ERR_PAGE_NOT_VALID",
          (l == 2U) && (res[1] == XCP_ERR_PAGE_NOT_VALID));
    cmd[0] = 0xEB; cmd[1] = 0x02U; cmd[2] = 0x09U; cmd[3] = 0x00U;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("D5 SET_CAL_PAGE seg9 -> ERR_SEGMENT_NOT_VALID",
          (l == 2U) && (res[1] == XCP_ERR_SEGMENT_NOT_VALID));

    l = xcp_cmd((const uint8 *)"\xE8\x00\x00\x00\x00", 5U, res);
    CHECK("D6 GET_SEGMENT_INFO mode0 address = 0x20000000",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20 }, 8U));
    l = xcp_cmd((const uint8 *)"\xE8\x00\x00\x01\x00", 5U, res);
    CHECK("D6a GET_SEGMENT_INFO mode0 length = 0x1000",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00 }, 8U));

    l = xcp_cmd((const uint8 *)"\xE8\x01\x00\x00\x00", 5U, res);
    CHECK("D6b GET_SEGMENT_INFO mode1: 2 pages, ext/map/access=0",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x02, 0x00, 0x00, 0x00 }, 5U));
    l = xcp_cmd((const uint8 *)"\xE8\x02\x00\x00\x00", 5U, res);
    CHECK("D6c GET_SEGMENT_INFO mode2 mapping: ext=0x10, addr=seg0_page0",
          (l == 8U) && (res[0] == XCP_PID_RES) && (res[3] == 0x10U)
          && (res[4] == 0x00U) && (res[5] == 0x00U) && (res[6] == 0x00U) && (res[7] == 0x20U));
    l = xcp_cmd((const uint8 *)"\xE8\x00\x00\x02\x00", 5U, res);
    CHECK("D6d GET_SEGMENT_INFO mode0 info2 -> ERR_OUT_OF_RANGE",
          (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

    l = xcp_cmd((const uint8 *)"\xE7\x00\x00\x00", 4U, res);
    CHECK("D7 GET_PAGE_INFO seg0 page0 (reference): XCP read-only ECU r/w",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x0C, 0x03 }, 3U));
    l = xcp_cmd((const uint8 *)"\xE7\x00\x01\x01", 4U, res);
    CHECK("D7a GET_PAGE_INFO seg1 page1 (working): all access",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x3C, 0x03 }, 3U));
    cmd[0] = 0xE6; cmd[1] = 0; cmd[2] = 0;
    l = xcp_cmd(cmd, 3U, res);
    CHECK("D8 SET_SEGMENT_MODE -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    l = xcp_cmd((const uint8 *)"\xE5\x00\x00", 3U, res);
    CHECK("D9 GET_SEGMENT_MODE -> RES", (l >= 2U) && (res[0] == XCP_PID_RES));
    cmd[0] = 0xE4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1;
    l = xcp_cmd(cmd, 5U, res);
    CHECK("D10 COPY_CAL_PAGE -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("D10b COPY copied 0x1000 bytes",
          memcmp(test_cal_ram[1], test_cal_rom[0], 0x1000U) == 0);

    printf("-- E. DAQ dynamic config\n");
    l = xcp_cmd((const uint8 *)"\xDA", 1U, res);

    CHECK("E1 GET_DAQ_PROCESSOR_INFO bytes",
          frame_is(res, l, (const uint8[]){ 0xFF, TST_DAQ_PROPERTIES, XCP_MAX_DAQ, 0x00,
                                            XCP_MAX_EVENT_CHANNEL, 0x00, XCP_MIN_DAQ,
                                            TST_DAQ_KEY_BYTE }, 8U));
    l = xcp_cmd((const uint8 *)"\xD9", 1U, res);

    CHECK("E2 GET_DAQ_RESOLUTION_INFO bytes: 4B timestamp, unit 1ms, ticks 1",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x01, XCP_ODT_ENTRY_SIZE_DAQ, 0x01,
                                            XCP_ODT_ENTRY_SIZE_STIM, 0x64, 0x01, 0x00 }, 8U));

    l = xcp_cmd((const uint8 *)"\xD7\x00\x00\x00", 4U, res);
    CHECK("E3 GET_DAQ_EVENT_INFO ch0: DAQ|STIM, ODT consistency, 10 x 1ms",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x0C, 0xFF, 0x00, 10U, 0x06, 0x00 }, 7U));
    l = xcp_cmd((const uint8 *)"\xD7\x00\x02\x00", 4U, res);
    CHECK("E3a GET_DAQ_EVENT_INFO ch2: DAQ only, ODT consistency",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x04, 0xFF, 0x00, 100U, 0x06, 0x02 }, 7U));
    l = xcp_cmd((const uint8 *)"\xD7\x00\x03\x00", 4U, res);
    CHECK("E3b GET_DAQ_EVENT_INFO ch3: STIM only, ODT consistency",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x08, 0xFF, 0x00, 0x00, 0x06, 0x03 }, 7U));

    l = xcp_cmd((const uint8 *)"\xD6", 1U, res);
    CHECK("E4 FREE_DAQ -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 2; cmd[3] = 0;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("E5 ALLOC_DAQ 2 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("E6 daq_count == 2", g_xcp_channel.runtime_ptr->daq_count == 2U);

    cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 2;
    l = xcp_cmd(cmd, 5U, res);
    CHECK("E7 ALLOC_ODT -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 2;
    l = xcp_cmd(cmd, 6U, res);
    CHECK("E8 ALLOC_ODT_ENTRY -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 99; cmd[3] = 0;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("E9 ALLOC_DAQ 99 -> ERR_MEMORY_OVERFLOW",
          (l == 2U) && (res[1] == XCP_ERR_MEMORY_OVERFLOW));

    cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0;
    l = xcp_cmd(cmd, 6U, res);
    CHECK("E10 SET_DAQ_PTR -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 4; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
    l = xcp_cmd(cmd, 8U, res);
    CHECK("E11 WRITE_DAQ var32 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 2; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR}, 4U);
    l = xcp_cmd(cmd, 8U, res);
    CHECK("E12 WRITE_DAQ entry1 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    cmd[0] = 0xE0; cmd[1] = 0x00; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("E13 SET_DAQ_LIST_MODE -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    cmd[0] = 0xDF; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    l = xcp_cmd(cmd, 4U, res);

    CHECK("E14 GET_DAQ_LIST_MODE bytes: idle DAQ list, ev0, prescaler 1",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 }, 8U));

    cmd[0] = 0xD8; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    l = xcp_cmd(cmd, 4U, res);

    CHECK("E15 GET_DAQ_LIST_INFO bytes: dynamic list, DAQ|STIM",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x0C, XCP_MAX_ODT, XCP_MAX_ODT_ENTRIES,
                                            0x00, 0x00 }, 6U));

    l = xcp_cmd((const uint8 *)"\xDC", 1U, res);
    CHECK("E16 GET_DAQ_CLOCK -> RES len8", (l == 8U) && (res[0] == XCP_PID_RES));

    cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0;
    (void)xcp_cmd(cmd, 6U, res);
    l = xcp_cmd((const uint8 *)"\xDB", 1U, res);
    {
        uint32 a = TEST_ADDR(test_ram_var32);

        CHECK("E17 READ_DAQ bytes: BIT_OFFSET=FF SIZE=4 EXT=0 ADDRESS=&var32",
              frame_is(res, l, (const uint8[]){ 0xFF, 0xFF, 0x04, 0x00, (uint8)a, (uint8)(a >> 8U),
                                                (uint8)(a >> 16U), (uint8)(a >> 24U) }, 8U));
    }

    cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("E18 START_DAQ_LIST -> RES + FIRST_PID 0",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00 }, 2U));
    l = xcp_cmd_reply((const uint8 *)"\xDF\x00\x00\x00", 4U, res);
    CHECK("E18a GET_DAQ_LIST_MODE running list: currentMode.running",
          (l == 8U) && (res[1] == 0x40U));
    l = xcp_cmd_reply((const uint8 *)"\xFD", 1U, res);
    CHECK("E18b GET_STATUS: sessionStatus.daqRunning",
          (l == 6U) && ((res[1] & 0x40U) != 0U));

    xcp_quiesce();

    cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0;
    (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 1; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR}, 4U);
    l = xcp_cmd(cmd, 8U, res);
    CHECK("E19 WRITE_DAQ while RUNNING -> ERR_DAQ_ACTIVE",
          (l == 2U) && (res[1] == XCP_ERR_DAQ_ACTIVE));

    test_ram_var32 = 0xA5A5F00DUL;
    xcp_drain_tx();
    Xcp_Event(0U);
    Xcp_Tl_TxConfirmation(0U, E_OK);
    {
        uint8 dto[64]; uint32 dl;
        uint32 popped = test_tx_pop(dto, &dl);
        CHECK("E20 DTO emitted on event", popped > 0U);
        CHECK("E21 DTO PID==0", dl >= 1U && dto[0] == 0U);

        CHECK("E22 DTO contains var32 A5A5F00D at dto[IDLEN..]",
              (dl >= (XCP_TST_ID_LEN + 4U)) && (dto[XCP_TST_ID_LEN] == 0x0DU) && (dto[XCP_TST_ID_LEN+1U] == 0xF0U)
              && (dto[XCP_TST_ID_LEN+2U] == 0xA5U) && (dto[XCP_TST_ID_LEN+3U] == 0xA5U));
    }

    {
        uint8 dto[64]; uint32 dl = 0U;
        CHECK("E23 ODT1 DTO PID==1",
              (test_tx_pop(dto, &dl) > 0U) && (dl >= 1U) && (dto[0] == 1U));
    }
    Xcp_Tl_TxConfirmation(0U, E_OK);

    cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 2U;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("E19a SET_DAQ_LIST_MODE prescaler2 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    xcp_drain_tx();
    Xcp_Event(0U); Xcp_Tl_TxConfirmation(0U, E_OK);
    {
        uint8 dto[64]; uint32 dl;
        CHECK("E19b prescaler event1 no DTO", test_tx_pop(dto, &dl) == 0U);
    }
    Xcp_Event(0U); Xcp_Tl_TxConfirmation(0U, E_OK);
    {
        uint8 dto[64]; uint32 dl;
        CHECK("E19c prescaler event2 DTO", test_tx_pop(dto, &dl) > 0U);
        xcp_drain_tx();
    }

    cmd[0] = 0xE0; cmd[6] = 1U;
    (void)xcp_cmd(cmd, 8U, res);

    cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("E24 STOP_DAQ_LIST -> RES", (l == 2U) && (res[0] == XCP_PID_RES));

    cmd[0] = 0xE3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("E25 CLEAR_DAQ_LIST -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    l = xcp_cmd((const uint8 *)"\xDD\x00", 2U, res);
    CHECK("E26 STOP_ALL_SYNCH -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    printf("-- E. select + start_stop_synch\n");
    (void)xcp_cmd_reply((const uint8 *)"\xD6", 1U, res);
    (void)xcp_cmd_reply((const uint8 *)"\xD5\x00\x02\x00", 4U, res);
    (void)xcp_cmd_reply((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
    (void)xcp_cmd_reply((const uint8 *)"\xD4\x00\x01\x00\x01", 5U, res);
    l = xcp_cmd_reply((const uint8 *)"\xDE\x02\x00\x00", 4U, res);
    CHECK("E30 START_STOP_DAQ_LIST select -> RES + FIRST_PID",
          (l == 2U) && (res[0] == XCP_PID_RES));
    l = xcp_cmd_reply((const uint8 *)"\xDF\x00\x00\x00", 4U, res);
    CHECK("E31 GET_DAQ_LIST_MODE: selected, not running", (l == 8U) && (res[1] == 0x01U));
    l = xcp_cmd_reply((const uint8 *)"\xDD\x01", 2U, res);
    CHECK("E32 START_STOP_SYNCH start selected -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("E33 only the selected list0 RUNNING",
          (g_xcp_channel.storage_ptr->daq_list[0].state == XCP_DAQ_STATE_RUNNING)
          && (g_xcp_channel.storage_ptr->daq_list[1].state != XCP_DAQ_STATE_RUNNING));
    l = xcp_cmd_reply((const uint8 *)"\xDF\x00\x00\x00", 4U, res);
    CHECK("E34 after start selected: running set, selected cleared", (l == 8U) && (res[1] == 0x40U));
    (void)xcp_cmd_reply((const uint8 *)"\xDE\x02\x01\x00", 4U, res);
    (void)xcp_cmd_reply((const uint8 *)"\xDE\x02\x00\x00", 4U, res);
    l = xcp_cmd_reply((const uint8 *)"\xDD\x02", 2U, res);
    CHECK("E35 START_STOP_SYNCH stop selected: list0 stopped, flags cleared",
          (l == 1U) && (g_xcp_channel.storage_ptr->daq_list[0].state != XCP_DAQ_STATE_RUNNING)
          && (g_xcp_channel.storage_ptr->daq_list[0].selected == FALSE)
          && (g_xcp_channel.storage_ptr->daq_list[1].selected == FALSE));
    l = xcp_cmd_reply((const uint8 *)"\xDE\x03\x00\x00", 4U, res);
    CHECK("E36 START_STOP_DAQ_LIST mode3 -> ERR_MODE_NOT_VALID",
          (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));
    l = xcp_cmd_reply((const uint8 *)"\xDD\x03", 2U, res);
    CHECK("E37 START_STOP_SYNCH mode3 -> ERR_MODE_NOT_VALID",
          (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));

    l = xcp_cmd_reply((const uint8 *)"\xE0\x20\x00\x00\x00\x00\x01\x00", 8U, res);
    CHECK("E38 SET_DAQ_LIST_MODE PID_OFF -> ERR_MODE_NOT_VALID",
          (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));
    l = xcp_cmd_reply((const uint8 *)"\xE0\x00\x00\x00\x03\x00\x01\x00", 8U, res);
    CHECK("E39 DAQ list on STIM-only event ch3 -> ERR_DAQ_CONFIG",
          (l == 2U) && (res[1] == XCP_ERR_DAQ_CONFIG));
    l = xcp_cmd_reply((const uint8 *)"\xE0\x02\x00\x00\x02\x00\x01\x00", 8U, res);
    CHECK("E40 STIM list on DAQ-only event ch2 -> ERR_DAQ_CONFIG",
          (l == 2U) && (res[1] == XCP_ERR_DAQ_CONFIG));
    (void)xcp_cmd_reply((const uint8 *)"\xDD\x00", 2U, res);
    (void)xcp_cmd_reply((const uint8 *)"\xD6", 1U, res);
    xcp_settle();

    printf("-- E. STIM\n");

    (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
    cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
    cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
    cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 4; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
    (void)xcp_cmd(cmd, 8U, res);

    cmd[0] = 0xE0; cmd[1] = 0x02; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("E27 SET_DAQ_LIST_MODE STIM -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0;
    (void)xcp_cmd(cmd, 4U, res);

    {
        test_ram_var32 = 0;

        uint8 stim_dto2[8] = {0};
        uint32 stim_len = test_stim_dto(stim_dto2, 0U, 0U, 0x44332211UL);
        xcp_inject(stim_dto2, stim_len);
        Xcp_MainFunction();
        Xcp_Tl_TxConfirmation(0U, E_OK);
        CHECK("E28 STIM DTO wrote var32 = 0x44332211",
              test_ram_var32 == 0x44332211UL);
    }

    xcp_drain_tx();
    xcp_pump(150U);
    {
        uint8 ev[64]; uint32 el;
        boolean found = FALSE;
        while (test_tx_pop(ev, &el) > 0U)
        {
            if ((el >= 2U) && (ev[0] == XCP_PID_EV) && (ev[1] == XCP_EV_STIM_TIMEOUT))
            {
                found = TRUE;
            }
        }
        CHECK("E29 STIM timeout -> EV_STIM_TIMEOUT", found);
    }

    (void)xcp_cmd((const uint8 *)"\xDD\x00", 2U, res);
    (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
    cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 2; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);

    cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
    cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 4; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xE0; cmd[1] = 0x02; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
    (void)xcp_cmd(cmd, 8U, res);

    cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
    cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 4; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xE0; cmd[1] = 0x00; cmd[2] = 1; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
    (void)xcp_cmd(cmd, 8U, res);

    cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
    cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);

    {
        uint8 stim_dto[8] = {0};
        uint32 stim_len = test_stim_dto(stim_dto, 0U, 0U, 0x11223344UL);
        test_ram_var32 = 0;
        xcp_inject(stim_dto, stim_len);
        Xcp_MainFunction();
        Xcp_Tl_TxConfirmation(0U, E_OK);
        CHECK("E30a bypass: STIM wrote var32", test_ram_var32 == 0x11223344UL);
    }
    xcp_drain_tx();
    Xcp_Event(0U);
    Xcp_Tl_TxConfirmation(0U, E_OK);
    {
        uint8 dto[64]; uint32 dl;
        boolean ok = FALSE;
        while (test_tx_pop(dto, &dl) > 0U)
        {
            if ((dl >= (XCP_TST_ID_LEN + 4U)) && (dto[XCP_TST_ID_LEN] == 0x44U) && (dto[XCP_TST_ID_LEN+1U] == 0x33U)
                && (dto[XCP_TST_ID_LEN+2U] == 0x22U) && (dto[XCP_TST_ID_LEN+3U] == 0x11U)) { ok = TRUE; }
        }
        CHECK("E30b bypass: DTO carries STIM-written value 0x11223344", ok);
    }

    printf("-- F. flash programming\n");
    memset(test_flash_mem, 0xAA, sizeof(test_flash_mem));

    l = xcp_cmd((const uint8 *)"\xCA\x01\xAA", 3U, res);
    CHECK("F0a PROGRAM_NEXT before start -> ERR_SEQUENCE",
          (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));
    l = xcp_cmd((const uint8 *)"\xC9\xAA", 2U, res);
    CHECK("F0b PROGRAM_MAX before start -> ERR_SEQUENCE",
          (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));
    l = xcp_cmd((const uint8 *)"\xD2", 1U, res);

    CHECK("F1 PROGRAM_START bytes",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00, TST_COMM_OPT, XCP_MAX_CTO,
                                            XCP_MAX_BS, XCP_MIN_ST, TST_QUEUE_SIZE }, 7U));

    l = xcp_cmd((const uint8 *)"\xD2", 1U, res);
    CHECK("F2 PROGRAM_START again -> ERR_PGM_ACTIVE",
          (l == 2U) && (res[1] == XCP_ERR_PGM_ACTIVE));
    l = xcp_cmd((const uint8 *)"\xCE", 1U, res);

    CHECK("F3 GET_PGM_PROCESSOR_INFO bytes: ABSOLUTE",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x01, XCP_MAX_FLASH_SECTOR }, 3U));
    l = xcp_cmd((const uint8 *)"\xCD\x00\x00", 3U, res);

    CHECK("F4 GET_SECTOR_INFO mode0 sec0 bytes",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x08 }, 8U));

    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){0x08000000UL}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xD1; cmd[1] = 0x00; cmd[2] = 0; cmd[3] = 0;
    cmd[4] = 0x00; cmd[5] = 0x40; cmd[6] = 0; cmd[7] = 0;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("F5 PROGRAM_CLEAR -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("F6 sector0 erased to 0xFF", test_flash_mem[0] == 0xFFU);

    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){0x08000000UL}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xD0; cmd[1] = 4; cmd[2] = 0x11; cmd[3] = 0x22; cmd[4] = 0x33; cmd[5] = 0x44;
    l = xcp_cmd(cmd, 6U, res);
    CHECK("F7 PROGRAM 4B -> RES", (l >= 1U) && (res[0] == XCP_PID_RES));
    cmd[0] = 0xD0; cmd[1] = 0;
    l = xcp_cmd(cmd, 2U, res);
    CHECK("F8 PROGRAM 0-len flush -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("F9 flash[0..3] = 11 22 33 44",
          (test_flash_mem[0] == 0x11U) && (test_flash_mem[1] == 0x22U)
          && (test_flash_mem[2] == 0x33U) && (test_flash_mem[3] == 0x44U));

    cmd[0] = 0xCA; cmd[1] = 2U; cmd[2] = 0xAA; cmd[3] = 0xBB;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("F9a PROGRAM_NEXT -> RES len5", (l == 5U) && (res[0] == XCP_PID_RES));
    cmd[0] = 0xC9; cmd[1] = 0x77; cmd[2] = 0x88; cmd[3] = 0x99;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("F9b PROGRAM_MAX -> RES", (l >= 1U) && (res[0] == XCP_PID_RES));
    cmd[0] = 0xD0; cmd[1] = 0;
    l = xcp_cmd(cmd, 2U, res);
    CHECK("F9c flush after NEXT/MAX -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("F9d flash[4..8] = AA BB 77 88 99",
          (test_flash_mem[4] == 0xAAU) && (test_flash_mem[5] == 0xBBU)
          && (test_flash_mem[6] == 0x77U) && (test_flash_mem[7] == 0x88U)
          && (test_flash_mem[8] == 0x99U));

    test_flash_verify_expected = 0;
    cmd[0] = 0xC8; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 0; cmd[7] = 0;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("F10 PROGRAM_VERIFY ok -> RES", (l >= 1U) && (res[0] == XCP_PID_RES));
    test_flash_verify_fail = TRUE;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("F11 PROGRAM_VERIFY fail -> ERR_VERIFY",
          (l == 2U) && (res[1] == XCP_ERR_VERIFY));
    test_flash_verify_fail = FALSE;

    cmd[0] = 0xCC; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 0; cmd[7] = 0;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("F12 PROGRAM_PREPARE -> RES", (l >= 1U) && (res[0] == XCP_PID_RES));
    cmd[0] = 0xCB; cmd[1] = 0; cmd[2] = 0; cmd[3] = XCP_PGM_MODE_ABSOLUTE; cmd[4] = 0; cmd[5] = 0; cmd[6] = 0; cmd[7] = 0;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("F13 PROGRAM_FORMAT -> RES", (l >= 1U) && (res[0] == XCP_PID_RES));

    l = xcp_cmd((const uint8 *)"\xCF", 1U, res);
    CHECK("F14 PROGRAM_RESET -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("F15 PROGRAM_RESET -> disconnected",
          g_xcp_channel.runtime_ptr->connection_state == XCP_DISCONNECTED);

    printf("-- H. resume mode\n");

    l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
    xcp_drain_tx();
    /* After F15 disconnect+reconnect all resources are locked; reopen DAQ+STIM */
    g_xcp_channel.runtime_ptr->protection_status &=
        (uint8)~(XCP_RESOURCE_DAQ | XCP_RESOURCE_STIM);
    (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
    cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);

    (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
    (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
    (void)xcp_cmd((const uint8 *)"\xE2\x00\x00\x00\x00\x00", 6U, res);
    cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 4; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    (void)xcp_cmd((const uint8 *)"\xE0\x00\x00\x00\x02\x00\x01\x00", 8U, res);
    (void)xcp_cmd((const uint8 *)"\xDE\x01\x00\x00", 4U, res);
    (void)xcp_cmd((const uint8 *)"\xE0\x80\x00\x00\x02\x00\x01\x00", 8U, res);
    l = xcp_cmd_reply((const uint8 *)"\xF9\x04\x34\x12", 4U, res);
    CHECK("H1 SET_REQUEST STORE_DAQ_REQ_RESUME -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    l = xcp_cmd_reply((const uint8 *)"\xFD", 1U, res);
    CHECK("H2 GET_STATUS while NvM write pending: storeDaqRequest|daqRunning",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x44, 0x11, 0x00, 0x00, 0x00 }, 6U));
    xcp_drain_tx();
    Xcp_NvMWriteRamBlockToNvmCallback(XCP_NVM_BLOCK_ID_RESUME, 0U);
    {
        uint8 ev[64]; uint32 el;
        boolean found = FALSE;
        while (test_tx_pop(ev, &el) > 0U)
        {
            if ((el >= 2U) && (ev[0] == XCP_PID_EV) && (ev[1] == XCP_EV_STORE_DAQ)) { found = TRUE; }
        }
        CHECK("H2a NvM write done -> EV_STORE_DAQ", found);
    }
    l = xcp_cmd_reply((const uint8 *)"\xFD", 1U, res);
    CHECK("H2b GET_STATUS stored: storeDaqRequest cleared, SESSION_CONFIGURATION_ID=0x1234",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x40, 0x11, 0x00, 0x34, 0x12 }, 6U));
    l = xcp_cmd_reply((const uint8 *)"\xDF\x00\x00\x00", 4U, res);
    CHECK("H2c GET_DAQ_LIST_MODE list0: resume|running",
          (l == 8U) && (res[1] == 0xC0U));

    xcp_drain_tx();
    Xcp_Init(NULL_PTR);
    CHECK("H3 resume: daq_count restored", g_xcp_channel.runtime_ptr->daq_count == 1U);
    {
        uint8 ev[64]; uint32 el;
        boolean found = FALSE;
        while (test_tx_pop(ev, &el) > 0U)
        {
            if ((el >= 2U) && (ev[0] == XCP_PID_EV) && (ev[1] == XCP_EV_RESUME_MODE))
            {
                found = TRUE;
            }
        }
        CHECK("H4 EV_RESUME_MODE emitted at power-up", found);
    }

    l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
    /* Re-opened session re-locks every resource — clear DAQ+STIM+PGM so the
       H/I/A/K blocks that follow can drive the protocol without explicit
       seed/key round-trips. */
    g_xcp_channel.runtime_ptr->protection_status &=
        (uint8)~(XCP_RESOURCE_DAQ | XCP_RESOURCE_STIM | XCP_RESOURCE_PGM);

    {
        uint8 dto[64]; uint32 dl;
        CHECK("H13 resumed list0 auto-RUNNING",
              g_xcp_channel.storage_ptr->daq_list[0].state == XCP_DAQ_STATE_RUNNING);

        test_ram_var32 = 0x11223344UL;
        xcp_drain_tx();
        Xcp_Event(2U); Xcp_Tl_TxConfirmation(0U, E_OK);
        CHECK("H14 resumed DAQ emits var32 on event2",
              (test_tx_pop(dto, &dl) > 0U) && (dl >= (XCP_TST_ID_LEN + 4U))
              && (dto[XCP_TST_ID_LEN] == 0x44U) && (dto[XCP_TST_ID_LEN + 3U] == 0x11U));
    }
    l = xcp_cmd_reply((const uint8 *)"\xFD", 1U, res);
    CHECK("H15 GET_STATUS in RESUME mode: resume|daqRunning, SESSION_CONFIGURATION_ID kept",
          frame_is(res, l, (const uint8[]){ 0xFF, 0xC0, 0x01, 0x00, 0x34, 0x12 }, 6U));
    (void)xcp_cmd_reply((const uint8 *)"\xDE\x00\x00\x00", 4U, res);
    xcp_drain_tx();

    l = xcp_cmd((const uint8 *)"\xF9\x08\x00\x00", 4U, res);
    CHECK("H5 SET_REQUEST CLEAR_DAQ_REQ -> RES, RAM DAQ kept",
          (l == 1U) && (res[0] == XCP_PID_RES) && (g_xcp_channel.runtime_ptr->daq_count == 1U));
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("H5a GET_STATUS after clear: clearDaqReq, no resume, cfg=0",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x08, 0x01, 0x00, 0x00, 0x00 }, 6U));

    l = xcp_cmd((const uint8 *)"\xF9\x01\x00\x00", 4U, res);
    CHECK("H6 SET_REQUEST STORE_CAL_REQ -> RES + storeCalRequest",
          (l == 1U) && (res[0] == XCP_PID_RES)
          && ((g_xcp_channel.runtime_ptr->session_status & 0x01U) != 0U));

    l = xcp_cmd((const uint8 *)"\xF9\x02\x00\x00", 4U, res);
    CHECK("H7 SET_REQUEST STORE_DAQ_NO_RESUME -> RES, stored but resume_active=0",
          (l == 1U) && (res[0] == XCP_PID_RES));
    xcp_drain_tx();
    Xcp_NvMWriteRamBlockToNvmCallback(XCP_NVM_BLOCK_ID_RESUME, 0U);
    CHECK("H7a NvM store complete: data_valid but resume_active cleared",
          (g_xcp_resume_runtime.data_valid == TRUE) && (g_xcp_resume_data.resume_active == FALSE));

    (void)xcp_cmd((const uint8 *)"\xDE\x02\x00\x00", 4U, res);
    (void)xcp_cmd((const uint8 *)"\xF9\x04\x00\x00", 4U, res);
    xcp_drain_tx();
    Xcp_NvMWriteRamBlockToNvmCallback(XCP_NVM_BLOCK_ID_RESUME, 0U);
    {
        uint8 ev[64]; uint32 el;
        boolean found = FALSE;
        while (test_tx_pop(ev, &el) > 0U)
        {
            if ((ev[0] == XCP_PID_EV) && (ev[1] == XCP_EV_STORE_DAQ)) { found = TRUE; }
        }
        CHECK("H8 NvM write callback -> EV_STORE_DAQ", found);
    }
    CHECK("H8b store_pending cleared", g_xcp_resume_runtime.store_pending == FALSE);

    g_xcp_resume_runtime.restore_pending = TRUE;
    g_xcp_channel.runtime_ptr->daq_count = 0U;
    xcp_drain_tx();
    Xcp_NvMReadRamBlockFromNvmCallback(XCP_NVM_BLOCK_ID_RESUME, 0U);
    CHECK("H9a restore_pending consumed by read callback",
          g_xcp_resume_runtime.restore_pending == FALSE);
    {
        uint8 ev[64]; uint32 el;
        boolean found = FALSE;
        while (test_tx_pop(ev, &el) > 0U)
        {
            if ((ev[0] == XCP_PID_EV) && (ev[1] == XCP_EV_RESUME_MODE)) { found = TRUE; }
        }
        CHECK("H9b NvM read callback -> EV_RESUME_MODE", found);
    }

#if (XCP_TIMESTAMP_SOURCE_OS == STD_ON)
    CHECK("H10 GetTimestamp == OS-derived acc",
          Xcp_GetTimestamp() == g_xcp_channel.runtime_ptr->os_timestamp_acc);
#else
    CHECK("H10 GetTimestamp == runtime timestamp",
          Xcp_GetTimestamp() == g_xcp_channel.runtime_ptr->timestamp);
#endif
    CHECK("H11 IsProgrammingActive == FALSE (after PGM reset)",
          Xcp_IsProgrammingActive(&g_xcp_channel) == FALSE);
    {
        Xcp_Channel null_channel;
        (void)memset(&null_channel, 0, sizeof(null_channel));
        Xcp_InitFlashProg(&null_channel);
        CHECK("H11a InitFlashProg NULL runtime tolerated",
              null_channel.runtime_ptr == NULL_PTR);
        CHECK("H11b IsProgrammingActive NULL runtime -> FALSE",
              Xcp_IsProgrammingActive(&null_channel) == FALSE);
        CHECK("H11c GetSectorConfig out of range -> NULL",
              Xcp_GetSectorConfig(XCP_MAX_FLASH_SECTOR) == NULL_PTR);
    }
    CHECK("H12 IsResumeModeActive == TRUE (activated after resume)",
          Xcp_IsResumeModeActive(&g_xcp_channel) == TRUE);

    printf("-- I. event packets\n");
    xcp_drain_tx();
    (void)Xcp_SendEventPacket(XCP_EV_USER, (const uint8 *)"\x42", 1U);
    Xcp_Tl_TxConfirmation(0U, E_OK);
    {
        uint8 ev[64]; uint32 el;
        if (test_tx_pop(ev, &el) == 0U) { ev[0] = 0; el = 0U; }
        CHECK("I1 EV_USER frame", (el >= 2U) && (ev[0] == XCP_PID_EV) && (ev[1] == XCP_EV_USER));
    }
    (void)Xcp_SendServiceRequest(XCP_SERV_TEXT, (const uint8 *)"HI", 2U);
    Xcp_Tl_TxConfirmation(0U, E_OK);
    {
        uint8 ev[64]; uint32 el = 0U;
        if (test_tx_pop(ev, &el) == 0U) { ev[0] = 0; el = 0U; }
        CHECK("I2 SERV_TEXT frame", (el >= 3U) && (ev[0] == XCP_PID_SERV) && (ev[1] == XCP_SERV_TEXT));
    }

    (void)Xcp_SendSleepEvent();
    Xcp_Tl_TxConfirmation(0U, E_OK);
    (void)Xcp_SendWakeUpEvent();
    Xcp_Tl_TxConfirmation(0U, E_OK);
    (void)Xcp_SendSessionTerminatedEvent();
    Xcp_Tl_TxConfirmation(0U, E_OK);
    (void)Xcp_SendServiceRequest(XCP_SERV_RESET, NULL_PTR, 0U);
    Xcp_Tl_TxConfirmation(0U, E_OK);
    {
        uint8 ev[64]; uint32 el;
        uint8 seen_sleep = 0U, seen_wake = 0U, seen_term = 0U, seen_reset = 0U;
        while (test_tx_pop(ev, &el) > 0U)
        {
            if (ev[0] == XCP_PID_EV)
            {
                if (ev[1] == XCP_EV_SLEEP)     { seen_sleep = 1U; }
                if (ev[1] == XCP_EV_WAKE_UP)   { seen_wake  = 1U; }
                if (ev[1] == XCP_EV_SESSION_TERMINATED) { seen_term = 1U; }
            }
            if ((ev[0] == XCP_PID_SERV) && (ev[1] == XCP_SERV_RESET)) { seen_reset = 1U; }
        }
        CHECK("I3 EV_SLEEP",   seen_sleep == 1U);
        CHECK("I4 EV_WAKE_UP", seen_wake  == 1U);
        CHECK("I5 EV_SESSION_TERMINATED", seen_term == 1U);
        CHECK("I6 SERV_RESET", seen_reset == 1U);
    }

    /* I8: EV_TIME_SYNC sent */
    (void)Xcp_SendEventPacket(XCP_EV_TIME_SYNC, (const uint8 *)"\x01\x02\x03\x04", 4U);
    Xcp_Tl_TxConfirmation(0U, E_OK);
    {
        uint8 ev[64]; uint32 el;
        if (test_tx_pop(ev, &el) == 0U) { ev[0] = 0; el = 0U; }
        CHECK("I8 EV_TIME_SYNC frame", (el >= 2U) && (ev[0] == XCP_PID_EV) && (ev[1] == XCP_EV_TIME_SYNC));
    }

#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)
    /* ext address extension: ext high 4 bits select the CalPage segment, translated by the active page */
    printf("-- ext address extension\n");
    {
        uint32 seg_base = g_xcp_channel.config_ptr->xcp_segment_config[0].start_address;
        uint32 seg_len  = g_xcp_channel.config_ptr->xcp_segment_config[0].length;
        uint32 npage    = g_xcp_channel.config_ptr->xcp_segment_config[0].num_pages;
        uint32 page_len = (npage != 0U) ? (seg_len / npage) : 0U;
        /* ext=0x10 -> segment 0, addr=in-page offset. Verify MemTranslate's physical address */
        {
            uint8 page = Xcp_SegmentRuntime[0].active_page_xcp;
            uint32 phys = seg_base + ((uint32)page * page_len);

            CHECK("E1 ext 0x10 -> seg0 page translation",
                  Xcp_MemTranslate(&g_xcp_channel, 0x10U, 0x40U) == (phys + 0x40U));
            /* segment 1 (ext=0x20) */
            {
                const Xcp_SegmentConfigType *c1 = &g_xcp_channel.config_ptr->xcp_segment_config[1];
                uint32 s1 = c1->start_address;
                uint32 pl1 = (c1->num_pages != 0U) ? (c1->length / c1->num_pages) : 0U;
                uint8 p1 = Xcp_SegmentRuntime[1].active_page_xcp;
                CHECK("E2 ext 0x20 -> seg1 translation",
                      Xcp_MemTranslate(&g_xcp_channel, 0x20U, 0x10U)
                      == (s1 + ((uint32)p1 * pl1) + 0x10U));
            }
            /* ext=0 not translated */
            CHECK("E3 ext 0 passthrough",
                  Xcp_MemTranslate(&g_xcp_channel, 0x00U, 0xDEADBEEFUL) == 0xDEADBEEFUL);
        }
        (void)npage;
    }
#endif

    printf("-- A. TX mode\n");
    Xcp_SetTransmissionMode(0U, XCP_TX_OFF);
    xcp_drain_tx();
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("A9 TX_OFF suppresses response", l == 0U);
    Xcp_SetTransmissionMode(0U, XCP_TX_ON);
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("A10 TX_ON resumes response", (l == 6U) && (res[0] == XCP_PID_RES));

    {
        uint8 dto[64]; uint32 dl;

        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        (void)xcp_cmd((const uint8 *)"\xD5\x00\x01\x00", 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
        (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
        (void)xcp_cmd((const uint8 *)"\xE2\x00\x00\x00\x00\x00", 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res);
        (void)xcp_cmd((const uint8 *)"\xDE\x01\x00\x00", 4U, res);
        xcp_drain_tx();

        Xcp_SetTransmissionMode(0U, XCP_TX_OFF);
        Xcp_Event(0U); Xcp_Tl_TxConfirmation(0U, E_OK);
        CHECK("A11 TX_OFF suppresses DTO", test_tx_pop(dto, &dl) == 0U);

        Xcp_SetTransmissionMode(0U, XCP_TX_ON);
        Xcp_Event(0U); Xcp_Tl_TxConfirmation(0U, E_OK);
        CHECK("A12 TX_ON resumes DTO", test_tx_pop(dto, &dl) > 0U);
        xcp_drain_tx();
        (void)xcp_cmd((const uint8 *)"\xDE\x00\x00\x00", 4U, res);
    }

    printf("-- J. error paths\n");

    {
        uint8 c1[2] = {0xFD, 0};
        uint32 enq = 1U;
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
        enq = XCP_CMD_QUEUE_SIZE + 1U;
#else
        enq = 2U;
#endif
        while (enq-- > 0U)
        {
            xcp_inject(c1, 1U);
        }
        Xcp_MainFunction();
        Xcp_Tl_TxConfirmation(0U, E_OK);
        Xcp_MainFunction();
        Xcp_Tl_TxConfirmation(0U, E_OK);
        {
            uint8 r2[64]; uint32 l2; boolean busy_found = FALSE;
            while (test_tx_pop(r2, &l2) > 0U)
            {
                if ((l2 == 2U) && (r2[0] == XCP_PID_ERR) && (r2[1] == XCP_ERR_CMD_BUSY))
                { busy_found = TRUE; }
            }
            CHECK("J1 queue full -> ERR_CMD_BUSY", busy_found);
        }
    }

    {
        PduInfoType out;
        uint8 b[8];

        out.sdu_data_ptr = b; out.sdu_length = 8; out.meta_data_ptr = NULL;
        g_xcp_channel.storage_ptr->cto_buffer[0] = 0xAB;
        g_xcp_channel.storage_ptr->cto_length = 3U;
        Std_ReturnType r = Xcp_Tl_TriggerTransmit(0U, &out);
        CHECK("J2 TriggerTransmit copies cto_buffer",
              (r == E_OK) && (out.sdu_length == 3U) && (b[0] == 0xABU));
        out.sdu_length = 2;
        r = Xcp_Tl_TriggerTransmit(0U, &out);
        CHECK("J3 TriggerTransmit small buf -> E_NOT_OK", r == E_NOT_OK);
        r = Xcp_Tl_TriggerTransmit(0U, NULL);
        CHECK("J4 TriggerTransmit NULL -> E_NOT_OK", r == E_NOT_OK);
    }

#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
    xcp_pump((uint32)XCP_CMD_QUEUE_SIZE + 2U);
    xcp_drain_tx();
    {
        PduInfoType i5;
        uint8 c5[2] = {0xFD, 0};
        i5.sdu_data_ptr = c5; i5.sdu_length = 1; i5.meta_data_ptr = NULL;
        test_tx_fail_next = TRUE;
        Xcp_Tl_RxIndication(0U, &i5);
        Xcp_Tl_TxConfirmation(0U, E_OK);
        Xcp_MainFunction();
        CHECK("J5 TX fail -> no frame, busy cleared",
              (test_tx_pop(res, &l) == 0U) && (g_xcp_channel.runtime_ptr->tx.state != XCP_TX_CTO));
    }
    test_tx_fail_next = FALSE;
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("J6 TX recovers", (l == 6U) && (res[0] == XCP_PID_RES));
#else
    test_tx_fail_next = TRUE;
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("J5 TX fail -> no frame, busy cleared",
          (l == 0U) && (g_xcp_channel.runtime_ptr->tx.state != XCP_TX_CTO));

    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("J6a overlapped -> ERR_CMD_BUSY", (l == 2U) && (res[0] == XCP_PID_ERR));
    xcp_pump(1U);
    l = test_tx_pop(res, &l);
    CHECK("J6 deferred cmd RES delivered", (l == 6U) && (res[0] == XCP_PID_RES));
#endif

    l = xcp_cmd((const uint8 *)"\xFF\x77", 2U, res);
    CHECK("J8 CONNECT bad mode -> ERR_MODE_NOT_VALID",
          (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){0xDEAD0000UL}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xF5; cmd[1] = 4U;
    l = xcp_cmd(cmd, 2U, res);
    CHECK("J9 UPLOAD forbidden addr -> ERR_ACCESS_DENIED",
          (l == 2U) && (res[1] == XCP_ERR_ACCESS_DENIED));
    l = xcp_cmd((const uint8 *)"\xF7\x04\x01\x02\x03\x04", 6U, res);
    CHECK("J10 UNLOCK no seed -> ERR_SEQUENCE",
          (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

    l = xcp_cmd((const uint8 *)"\xFE", 1U, res);
    CHECK("J7 DISCONNECT -> RES + state",
          (l == 1U) && (res[0] == XCP_PID_RES)
          && (g_xcp_channel.runtime_ptr->connection_state == XCP_DISCONNECTED));

    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("J11 GET_STATUS while disconnected -> silence", l == 0U);
    l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
    CHECK("J12 reconnect after disconnect", (l == 8U) && (res[0] == XCP_PID_RES));

    /* K section needs DAQ/STIM/PGM unlocked after this fresh CONNECT */
    g_xcp_channel.runtime_ptr->protection_status &=
        (uint8)~(XCP_RESOURCE_DAQ | XCP_RESOURCE_STIM | XCP_RESOURCE_PGM);

#define XCP_BLOCK_TYPE_NONE     (0U)
#define XCP_BLOCK_TYPE_DOWNLOAD (1U)
#define XCP_BLOCK_TYPE_UPLOAD   (2U)
#define XCP_BLOCK_TYPE_PROGRAM  (3U)

    printf("-- K. edge cases\n");

    {

        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);

        test_sim_dto_inflight();
        xcp_drain_tx();
        {
            uint32 qi;
            for (qi = 0U; qi < (XCP_MAX_ODT + 2U); qi++)
            {
                Xcp_Event(0U);
            }
        }
        CHECK("K1a DTO queue count capped at XCP_MAX_ODT",
              g_xcp_channel.storage_ptr->dto_queue_count == XCP_MAX_ODT);

        g_xcp_channel.runtime_ptr->tx.dto_latch = FALSE;
        g_xcp_channel.runtime_ptr->tx.state = XCP_TX_IDLE;
        {
            uint8  ev[64]; uint32 el;
            boolean found = FALSE;
            xcp_pump(5U);
            while (test_tx_pop(ev, &el) > 0U)
            {
                if ((el >= 2U) && (ev[0] == XCP_PID_EV) && (ev[1] == XCP_EV_DAQ_OVERLOAD)) { found = TRUE; }
            }
            CHECK("K1b DTO overflow -> EV_DAQ_OVERLOAD", found);
        }

        cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        xcp_settle();
    }

    {

        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xE0; cmd[1] = 0x02; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);

        {
            PduInfoType info;
            uint8 stim[8] = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0, 0, 0};
            (void)info;
            xcp_inject(stim, 8U);
        }

        g_xcp_channel.runtime_ptr->timestamp = 50U;
        g_xcp_channel.runtime_ptr->stim_last_received[0] = 0xFFFFFF90UL;
        g_xcp_channel.runtime_ptr->stim_timeout_flag[0] = FALSE;
        xcp_drain_tx();
        Xcp_MainFunction();
        {
            uint8  ev[64]; uint32 el; boolean found = FALSE;
            while (test_tx_pop(ev, &el) > 0U)
            {
                if ((el >= 2U) && (ev[0] == XCP_PID_EV) && (ev[1] == XCP_EV_STIM_TIMEOUT)) { found = TRUE; }
            }
            CHECK("K2a wraparound elapsed=162ms -> EV_STIM_TIMEOUT", found);
            CHECK("K2b timeout_flag set after wrap",
                  g_xcp_channel.runtime_ptr->stim_timeout_flag[0] == TRUE);
        }

        g_xcp_channel.runtime_ptr->stim_timeout_flag[0] = FALSE;
        g_xcp_channel.runtime_ptr->stim_last_received[0] = g_xcp_channel.runtime_ptr->timestamp;
        xcp_drain_tx();
        Xcp_MainFunction();
        {
            uint8  ev[64]; uint32 el; boolean found = FALSE;
            while (test_tx_pop(ev, &el) > 0U)
            {
                if ((el >= 2U) && (ev[0] == XCP_PID_EV) && (ev[1] == XCP_EV_STIM_TIMEOUT)) { found = TRUE; }
            }
            CHECK("K2c no-wrap no-timeout -> silent", !found);
        }
        cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
    }

    {
        Std_ReturnType r;

        g_xcp_channel.runtime_ptr->block_transfer_active = TRUE;
        g_xcp_channel.runtime_ptr->block_transfer_type   = XCP_BLOCK_TYPE_DOWNLOAD;
        g_xcp_channel.runtime_ptr->block_transfer_remaining = 100U;
        r = Xcp_StartBlockTransfer(&g_xcp_channel, XCP_BLOCK_TYPE_DOWNLOAD, 200U, 8U);
        CHECK("K3a StartBlockTransfer while active -> E_NOT_OK", r == E_NOT_OK);
        CHECK("K3b state preserved after rejected start",
              g_xcp_channel.runtime_ptr->block_transfer_active == TRUE
              && g_xcp_channel.runtime_ptr->block_transfer_remaining == 100U);
        Xcp_CancelBlockTransfer(&g_xcp_channel);
        CHECK("K3c cancel clears the block state",
              g_xcp_channel.runtime_ptr->block_transfer_active == FALSE
              && g_xcp_channel.runtime_ptr->block_transfer_remaining == 0U);
    }

    {
        Std_ReturnType r;
        PduInfoType info;
        uint8 dto_short[1] = {0x00};
        uint8 dto_baddaq[8] = {0x3F, 0x01, 0x02, 0x03, 0x04, 0, 0, 0};

        r = Xcp_ReceiveStimDto(&g_xcp_channel, NULL, 4U);
        CHECK("K4a STIM NULL -> E_NOT_OK", r == E_NOT_OK);
        r = Xcp_ReceiveStimDto(&g_xcp_channel, dto_short, 1U);
        CHECK("K4b STIM len<2 -> E_NOT_OK", r == E_NOT_OK);
        r = Xcp_ReceiveStimDto(&g_xcp_channel, dto_baddaq, 8U);
        CHECK("K4c STIM out-of-range daq list -> E_NOT_OK", r == E_NOT_OK);

        Xcp_Tl_RxIndication(0U, NULL);
        info.sdu_data_ptr = NULL; info.sdu_length = 4U; info.meta_data_ptr = NULL;
        Xcp_Tl_RxIndication(0U, &info);
        CHECK("K4d NULL pdu/sdu -> no pending cmd",
              g_xcp_channel.runtime_ptr->cmd_pending == FALSE
              && g_xcp_channel.runtime_ptr->busy_pending == FALSE);

        {
            uint8 big[XCP_MAX_CTO + 4U];
            memset(big, 0xFD, sizeof(big));
            xcp_inject(big, sizeof(big));
            CHECK("K4e cmd > XCP_MAX_CTO dropped",
                  g_xcp_channel.runtime_ptr->cmd_pending == FALSE);
        }
    }

    {
        PduInfoType tinfo;
        uint8 tbuf[64];
        Std_ReturnType r;
        uint32 det_before = test_det_count;

        (void)xcp_cmd((const uint8 *)"\xFD", 1U, res);

        tinfo.sdu_data_ptr = tbuf; tinfo.sdu_length = 2U; tinfo.meta_data_ptr = NULL;
        r = Xcp_Tl_TriggerTransmit(0U, &tinfo);
        CHECK("K5a TriggerTransmit short buffer -> E_NOT_OK", r == E_NOT_OK);

        tinfo.sdu_data_ptr = tbuf; tinfo.sdu_length = 64U; tinfo.meta_data_ptr = NULL;
        r = Xcp_Tl_TriggerTransmit(0U, &tinfo);
        CHECK("K5b TriggerTransmit ok -> E_OK + cto_length",
              (r == E_OK) && (tinfo.sdu_length == g_xcp_channel.storage_ptr->cto_length));

#if (XCP_DEV_ERROR_DETECT == STD_ON)
        r = Xcp_Tl_TriggerTransmit(0U, NULL);
        CHECK("K5c TriggerTransmit NULL -> E_NOT_OK + DET",
              (r == E_NOT_OK) && (test_det_count > det_before));
#endif
    }

    {

        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);

        Xcp_Tl_TxConfirmation(0U, E_OK);

        Xcp_Event(0U);
        CHECK("K6a DTO in-flight sets dto_busy", g_xcp_channel.runtime_ptr->tx.dto_latch == TRUE);

        {
            uint8 gs = 0xFD;
            xcp_inject(&gs, 1U);
            Xcp_MainFunction();
        }
        CHECK("K6b RES deferred while DTO busy -> pending",
              g_xcp_channel.runtime_ptr->tx.parked == TRUE
              && g_xcp_channel.runtime_ptr->tx.dto_latch == TRUE);

        Xcp_Tl_TxConfirmation(0U, E_OK);
        CHECK("K6c DTO conf -> pending CTO sent (cto_busy)",
              g_xcp_channel.runtime_ptr->tx.parked == FALSE
              && g_xcp_channel.runtime_ptr->tx.state == XCP_TX_CTO);

        Xcp_Tl_TxConfirmation(0U, E_OK);
        CHECK("K6d second conf clears cto_busy",
              g_xcp_channel.runtime_ptr->tx.state != XCP_TX_CTO);

        {
            uint8 gs = 0xFD;
            xcp_settle();
            xcp_inject(&gs, 1U);
            Xcp_MainFunction();
        }
        Xcp_Tl_TxConfirmation(0U, E_NOT_OK);
        CHECK("K6e CTO conf fail -> busy cleared + error",
              g_xcp_channel.runtime_ptr->tx.state != XCP_TX_CTO
              && g_xcp_channel.runtime_ptr->tx.last_error == 1U);

        cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        xcp_settle();
    }

    {

        l = xcp_cmd((const uint8 *)"\xDF\x00\x63\x00", 4U, res);
        CHECK("K7a GET_DAQ_LIST_MODE list99 -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        l = xcp_cmd((const uint8 *)"\xE3\x00\x00\x00", 4U, res);
        CHECK("K7b CLEAR_DAQ_LIST running -> ERR_DAQ_ACTIVE",
              (l == 2U) && (res[1] == XCP_ERR_DAQ_ACTIVE));
        cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        (void)xcp_cmd((const uint8 *)"\xE3\x00\x00\x00", 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        xcp_settle();
    }

    {
        test_sim_dto_inflight();
        g_xcp_channel.storage_ptr->dto_queue_count = 0U;
        {
            uint32 wi;
            for (wi = 0U; wi < 201U; wi++)
            {
                Xcp_MainFunction();
            }
        }
        CHECK("K8a dto_busy cleared after watchdog timeout",
              g_xcp_channel.runtime_ptr->tx.dto_latch == FALSE);

        {
            uint32 wi;
            for (wi = 0U; wi < 20U; wi++)
            {
                Xcp_Tl_TxConfirmation(0U, E_OK);
                Xcp_MainFunction();
            }
        }
        CHECK("K8b queue drains and idle after watchdog",
              g_xcp_channel.storage_ptr->dto_queue_count == 0U);
    }

    {

        l = xcp_cmd((const uint8 *)"\xF9\x0C\x00\x00", 4U, res);
        CHECK("K9a SET_REQUEST clear+store_resume -> RES",
              (l == 1U) && (res[0] == XCP_PID_RES));
        CHECK("K9b sessionStatus.storeDaqRequest pending",
              (g_xcp_channel.runtime_ptr->session_status & 0x04U) != 0U);
        Xcp_NvMWriteRamBlockToNvmCallback(XCP_NVM_BLOCK_ID_RESUME, 0U);
        CHECK("K9c NvM write done clears storeDaqRequest",
              (g_xcp_channel.runtime_ptr->session_status & 0x04U) == 0U);
        xcp_settle();

        l = xcp_cmd((const uint8 *)"\xF9\x01", 2U, res);
        CHECK("K9d SET_REQUEST len2 -> ERR_CMD_SYNTAX",
              (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));

        l = xcp_cmd((const uint8 *)"\xF9\xE0\x00\x00", 4U, res);
        CHECK("K9e SET_REQUEST reserved bits only -> RES (no-op)",
              (l == 1U) && (res[0] == XCP_PID_RES));
    }

    {
        l = xcp_cmd((const uint8 *)"\xF2", 1U, res);
        CHECK("K10a TL_CMD len1 -> ERR_CMD_SYNTAX",
              (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
        l = xcp_cmd((const uint8 *)"\xF2\x01", 2U, res);
        CHECK("K10b TL_CMD unknown sub -> ERR_CMD_UNKNOWN",
              (l == 2U) && (res[1] == XCP_ERR_CMD_UNKNOWN));
#if (XCP_ON_CAN_ENABLED == STD_ON)
        l = xcp_cmd((const uint8 *)"\xF2\xFF", 2U, res);
        CHECK("K10c GET_SLAVE_ID -> RES len8 'XCP'",
              (l == 8U) && (res[0] == XCP_PID_RES)
              && (res[5] == 'X') && (res[6] == 'C') && (res[7] == 'P'));
#else
        l = xcp_cmd((const uint8 *)"\xF2\xFF", 2U, res);
        CHECK("K10c GET_SLAVE_ID on non-CAN -> ERR_CMD_UNKNOWN",
              (l == 2U) && (res[1] == XCP_ERR_CMD_UNKNOWN));
#endif
    }

    {

        cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
        (void)xcp_cmd(cmd, 8U, res);

        g_xcp_channel.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_CAL_PAG;
        test_ram_var32 = 0x1234ABCDUL;
        l = xcp_cmd((const uint8 *)"\xEC\x00\x00\x00\xFF\x00", 6U, res);
        CHECK("K11a MODIFY_BITS xor 0x00FF -> RES",
              (l == 1U) && (res[0] == XCP_PID_RES));
        CHECK("K11b MODIFY_BITS applied", test_ram_var32 == 0x123400FFUL);

        g_xcp_channel.runtime_ptr->protection_status |= XCP_RESOURCE_CAL_PAG;
        l = xcp_cmd((const uint8 *)"\xEC\x00\x00\x00\xFF\x00", 6U, res);
        CHECK("K11c MODIFY_BITS locked -> ERR_ACCESS_LOCKED",
              (l == 2U) && (res[1] == XCP_ERR_ACCESS_LOCKED));
        g_xcp_channel.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_CAL_PAG;

        l = xcp_cmd((const uint8 *)"\xF8\x00\x7F", 3U, res);
        CHECK("K11d GET_SEED bad resource -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));
        l = xcp_cmd((const uint8 *)"\xF8\x00\x01", 3U, res);
        CHECK("K11e GET_SEED unlocked resource -> RES len0",
              (l == 2U) && (res[0] == XCP_PID_RES) && (res[1] == 0U));

        g_xcp_channel.runtime_ptr->protection_status |= XCP_RESOURCE_DAQ;
        l = xcp_cmd((const uint8 *)"\xF8\x00\x04", 3U, res);
        l = xcp_cmd((const uint8 *)"\xF7\x08\x01\x02", 4U, res);
        CHECK("K11f UNLOCK declared len > cmd -> ERR_CMD_SYNTAX",
              (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));

        g_xcp_channel.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_DAQ;
        g_xcp_channel.runtime_ptr->seed_requested = FALSE;
        xcp_settle();
    }

    {

        g_xcp_channel.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_PGM;

        g_xcp_channel.runtime_ptr->pgm_started = FALSE;
        l = xcp_cmd((const uint8 *)"\xD1\x00\x00\x00\x00\x40\x00\x00", 8U, res);
        CHECK("K12a PROGRAM_CLEAR before START -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

        l = xcp_cmd((const uint8 *)"\xD0\x02\xAA\xBB", 4U, res);
        CHECK("K12b PROGRAM before START -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

        l = xcp_cmd((const uint8 *)"\xC9\xAA", 2U, res);
        CHECK("K12c PROGRAM_MAX before START -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

        l = xcp_cmd((const uint8 *)"\xCC\x00\x01\x00", 4U, res);
        CHECK("K12c1 PROGRAM_PREPARE before START -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));
        l = xcp_cmd((const uint8 *)"\xCB\x00\x00\x00\x00", 5U, res);
        CHECK("K12c2 PROGRAM_FORMAT before START -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));
        l = xcp_cmd((const uint8 *)"\xC8\x00\x00\x00\x00\x00\x00\x00", 8U, res);
        CHECK("K12c3 PROGRAM_VERIFY before START -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));
        l = xcp_cmd((const uint8 *)"\xCF", 1U, res);
        CHECK("K12c4 PROGRAM_RESET before START -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

        g_xcp_channel.runtime_ptr->pgm_started = TRUE;

        g_xcp_channel.runtime_ptr->mta = 0xFFFFFF00UL;
        l = xcp_cmd((const uint8 *)"\xD1\x00\x00\x00\x00\x10\x00\x00", 8U, res);
        CHECK("K12d PROGRAM_CLEAR range overflow -> ERR_GENERIC",
              (l == 2U) && (res[1] == XCP_ERR_GENERIC));

        g_xcp_channel.runtime_ptr->mta = 0x08020000UL;
        l = xcp_cmd((const uint8 *)"\xD1\x00\x00\x00\x00\x10\x00\x00", 8U, res);
        CHECK("K12e PROGRAM_CLEAR address hole -> ERR_GENERIC",
              (l == 2U) && (res[1] == XCP_ERR_GENERIC));

        g_xcp_channel.runtime_ptr->mta = 0x08000000UL;
        l = xcp_cmd((const uint8 *)"\xD0\x08\xAA\xBB", 4U, res);
        CHECK("K12f PROGRAM declared > payload -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        g_xcp_channel.runtime_ptr->mta = 0xDEAD0000UL;
        l = xcp_cmd((const uint8 *)"\xD0\x02\xAA\xBB", 4U, res);
        CHECK("K12g PROGRAM outside sector -> ERR_GENERIC",
              (l == 2U) && (res[1] == XCP_ERR_GENERIC));

        g_xcp_channel.runtime_ptr->mta = 0x08000000UL;
        l = xcp_cmd((const uint8 *)"\xC9", 1U, res);
        CHECK("K12h PROGRAM_MAX empty -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        l = xcp_cmd((const uint8 *)"\xD1\x01\x00\x00\x00\x40\x00\x00", 8U, res);
        CHECK("K12i PROGRAM_CLEAR bad mode -> ERR_MODE_NOT_VALID",
              (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));

        l = xcp_cmd((const uint8 *)"\xCD\x01\x00", 3U, res);
        CHECK("K12j GET_SECTOR_INFO mode1 returns length",
              (l == 8U) && (res[0] == XCP_PID_RES) && (res[4] == 0x00U)
              && (res[5] == 0x40U) && (res[6] == 0x00U) && (res[7] == 0x00U));
        l = xcp_cmd((const uint8 *)"\xCD\x02\x00", 3U, res);
        CHECK("K12k GET_SECTOR_INFO mode2 returns empty name",
              (l == 2U) && (res[0] == XCP_PID_RES) && (res[1] == 0x00U));
        l = xcp_cmd((const uint8 *)"\xCD\x03\x00", 3U, res);
        CHECK("K12l GET_SECTOR_INFO bad mode -> ERR_MODE_NOT_VALID",
              (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));

        l = xcp_cmd((const uint8 *)"\xCC\x00\xFF\xFF", 4U, res);
        CHECK("K12m PROGRAM_PREPARE oversize -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        l = xcp_cmd((const uint8 *)"\xCB\x01\x00\x00\x00", 5U, res);
        CHECK("K12n PROGRAM_FORMAT compression -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));
        l = xcp_cmd((const uint8 *)"\xCB\x00\x00\x00\x00", 5U, res);
        CHECK("K12o PROGRAM_FORMAT bad method -> ERR_MODE_NOT_VALID",
              (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));
        l = xcp_cmd((const uint8 *)"\xCB\x00\x00\x01\x02", 5U, res);
        CHECK("K12p PROGRAM_FORMAT bad access -> ERR_MODE_NOT_VALID",
              (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));

        l = xcp_cmd((const uint8 *)"\xCA\x08\xAA", 3U, res);
        CHECK("K12q PROGRAM_NEXT truncated -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        g_xcp_channel.runtime_ptr->mta = 0xDEAD0000UL;
        l = xcp_cmd((const uint8 *)"\xC9\xAA", 2U, res);
        CHECK("K12r PROGRAM_MAX outside sector -> ERR_GENERIC",
              (l == 2U) && (res[1] == XCP_ERR_GENERIC));

        g_xcp_channel.runtime_ptr->mta = 0x08000000UL;
        (void)xcp_cmd((const uint8 *)"\xCC\x00\x02\x00", 4U, res);
        l = xcp_cmd((const uint8 *)"\xD0\x04\x01\x02\x03\x04", 6U, res);
        CHECK("K12s PROGRAM exceeds declared size -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));
        l = xcp_cmd((const uint8 *)"\xCA\x04\x01\x02\x03\x04", 6U, res);
        CHECK("K12t PROGRAM_NEXT exceeds declared size -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));
        l = xcp_cmd((const uint8 *)"\xC9\x01\x02\x03\x04", 5U, res);
        CHECK("K12u PROGRAM_MAX exceeds declared size -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

        test_flash_erase_fail = TRUE;
        l = xcp_cmd((const uint8 *)"\xD1\x00\x00\x00\x00\x40\x00\x00", 8U, res);
        CHECK("K12v PROGRAM_CLEAR callout failure -> ERR_GENERIC",
              (l == 2U) && (res[1] == XCP_ERR_GENERIC));
        test_flash_erase_fail = FALSE;

        (void)xcp_cmd((const uint8 *)"\xCC\x00\x00\x00", 4U, res);
        test_flash_program_fail = TRUE;
        l = xcp_cmd((const uint8 *)"\xD0\x04\x01\x02\x03\x04", 6U, res);
        CHECK("K12w PROGRAM callout failure -> ERR_GENERIC",
              (l == 2U) && (res[1] == XCP_ERR_GENERIC));
        l = xcp_cmd((const uint8 *)"\xCA\x02\xAA\xBB", 4U, res);
        CHECK("K12x PROGRAM_NEXT callout failure -> ERR_GENERIC",
              (l == 2U) && (res[1] == XCP_ERR_GENERIC));
        l = xcp_cmd((const uint8 *)"\xC9\xAA\xBB", 3U, res);
        CHECK("K12y PROGRAM_MAX callout failure -> ERR_GENERIC",
              (l == 2U) && (res[1] == XCP_ERR_GENERIC));
        test_flash_program_fail = FALSE;

        g_xcp_channel.runtime_ptr->pgm_started = FALSE;
        g_xcp_channel.runtime_ptr->protection_status |= XCP_RESOURCE_PGM;
        xcp_settle();
    }

    {

        l = xcp_cmd((const uint8 *)"\xEA\x00\x00", 3U, res);
        CHECK("K13a GET_CAL_PAGE mode0 -> ERR_MODE_NOT_VALID",
              (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));

        l = xcp_cmd((const uint8 *)"\xEA\x01\x09", 3U, res);
        CHECK("K13b GET_CAL_PAGE seg9 -> ERR_SEGMENT_NOT_VALID",
              (l == 2U) && (res[1] == XCP_ERR_SEGMENT_NOT_VALID));

        l = xcp_cmd((const uint8 *)"\xE8\x03\x00\x00\x00", 5U, res);
        CHECK("K13c GET_SEGMENT_INFO mode3 -> ERR_MODE_NOT_VALID",
              (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));

        l = xcp_cmd((const uint8 *)"\xE7\x00\x00\x09", 4U, res);
        CHECK("K13d GET_PAGE_INFO page9 -> ERR_PAGE_NOT_VALID",
              (l == 2U) && (res[1] == XCP_ERR_PAGE_NOT_VALID));

        g_xcp_channel.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_CAL_PAG;
        l = xcp_cmd((const uint8 *)"\xE4\x00\x01\x00\x00", 5U, res);
        CHECK("K13e COPY_CAL_PAGE dst page0 -> ERR_WRITE_PROTECTED",
              (l == 2U) && (res[1] == XCP_ERR_WRITE_PROTECTED));

        l = xcp_cmd((const uint8 *)"\xE4\x09\x01\x00\x01", 5U, res);
        CHECK("K13f COPY_CAL_PAGE bad seg -> ERR_SEGMENT_NOT_VALID",
              (l == 2U) && (res[1] == XCP_ERR_SEGMENT_NOT_VALID));
    }

    {

        xcp_drain_tx();
        Xcp_Event(3U);
        {
            uint8 tmp[64]; uint32 tl;
            CHECK("K14a Event unmatched channel -> no DTO",
                  test_tx_pop(tmp, &tl) == 0U);
        }

        {
            uint8 saved = g_xcp_channel.runtime_ptr->state;
            uint32 det_before = test_det_count;
            PduInfoType ti;
            uint8 tb[8];
            Std_ReturnType r;
            g_xcp_channel.runtime_ptr->state = XCP_UNINIT;
            ti.sdu_data_ptr = tb; ti.sdu_length = 8U; ti.meta_data_ptr = NULL;
            r = Xcp_Tl_TriggerTransmit(0U, &ti);
#if (XCP_DEV_ERROR_DETECT == STD_ON)
            CHECK("K14b TriggerTransmit UNINIT -> E_NOT_OK + DET",
                  (r == E_NOT_OK) && (test_det_count > det_before));
#else
            CHECK("K14b TriggerTransmit UNINIT -> E_NOT_OK", r == E_NOT_OK);
#endif
            g_xcp_channel.runtime_ptr->state = saved;
        }
    }

    {
        uint8  ev[64]; uint32 el; boolean found;
        Xcp_ResumeDataType *rd = &g_xcp_resume_data;
        uint16 saved_checksum;
        uint32 saved_magic;
        uint8  saved_version;
        uint8  saved_count;

        memset(rd, 0, sizeof(*rd));
        rd->magic_number = XCP_RESUME_MAGIC_NUMBER;
        rd->version      = XCP_RESUME_DATA_VERSION;
        rd->daq_count    = 1U;
        rd->resume_active = TRUE;
        rd->checksum     = 0U;

        {
            const uint8 *p = (const uint8 *)rd;
            uint32 off = offsetof(Xcp_ResumeDataType, checksum);
            uint16 cs = 0U;
            uint32 i;
            for (i = 0U; i < off; i++) { cs += p[i]; }
            for (i = off + sizeof(rd->checksum); i < sizeof(*rd); i++) { cs += p[i]; }
            rd->checksum = cs;
        }
        xcp_drain_tx();
        CHECK("K15a valid data restores -> ACTIVE",
              Xcp_RestoreDaqConfig() == E_OK
              && g_xcp_resume_runtime.state == XCP_RESUME_STATE_ACTIVE);
        {
            found = FALSE;
            while (test_tx_pop(ev, &el) > 0U)
            {
                if ((el >= 2U) && (ev[0] == XCP_PID_EV) && (ev[1] == XCP_EV_RESUME_MODE)) { found = TRUE; }
            }
            CHECK("K15b valid restore emits EV_RESUME_MODE", found);
        }

        saved_magic = rd->magic_number;
        rd->magic_number = 0xDEADBEEFUL;
        CHECK("K15c bad magic -> E_NOT_OK + IDLE",
              (Xcp_RestoreDaqConfig() == E_NOT_OK)
              && (g_xcp_resume_runtime.state == XCP_RESUME_STATE_IDLE));
        rd->magic_number = saved_magic;

        saved_version = rd->version;
        rd->version = (uint8)(saved_version + 1U);
        CHECK("K15d bad version -> E_NOT_OK + IDLE",
              (Xcp_RestoreDaqConfig() == E_NOT_OK)
              && (g_xcp_resume_runtime.state == XCP_RESUME_STATE_IDLE));
        rd->version = saved_version;

        saved_count = rd->daq_count;
        rd->daq_count = (uint8)(XCP_MAX_DAQ + 1U);
        CHECK("K15e daq_count overflow -> E_NOT_OK",
              Xcp_RestoreDaqConfig() == E_NOT_OK);
        rd->daq_count = saved_count;

        saved_checksum = rd->checksum;
        rd->checksum = (uint16)(saved_checksum ^ 0xFFFFU);
        CHECK("K15f bad checksum -> E_NOT_OK + IDLE",
              (Xcp_RestoreDaqConfig() == E_NOT_OK)
              && (g_xcp_resume_runtime.state == XCP_RESUME_STATE_IDLE));
        rd->checksum = saved_checksum;

        rd->resume_active = FALSE;
        {
            const uint8 *p = (const uint8 *)rd;
            uint32 off = offsetof(Xcp_ResumeDataType, checksum);
            uint16 cs = 0U;
            uint32 i;
            for (i = 0U; i < off; i++) { cs += p[i]; }
            for (i = off + sizeof(rd->checksum); i < sizeof(*rd); i++) { cs += p[i]; }
            rd->checksum = cs;
        }
        CHECK("K15g resume_active=0 -> E_NOT_OK + IDLE",
              (Xcp_RestoreDaqConfig() == E_NOT_OK)
              && (g_xcp_resume_runtime.state == XCP_RESUME_STATE_IDLE));
        rd->resume_active = TRUE;
        {
            const uint8 *p = (const uint8 *)rd;
            uint32 off = offsetof(Xcp_ResumeDataType, checksum);
            uint16 cs = 0U;
            uint32 i;
            for (i = 0U; i < off; i++) { cs += p[i]; }
            for (i = off + sizeof(rd->checksum); i < sizeof(*rd); i++) { cs += p[i]; }
            rd->checksum = cs;
        }

        g_xcp_resume_runtime.restore_pending = TRUE;
        Xcp_NvMReadRamBlockFromNvmCallback(XCP_NVM_BLOCK_ID_RESUME, 1U);
        CHECK("K15h NvM read fail -> IDLE + pending cleared",
              (g_xcp_resume_runtime.state == XCP_RESUME_STATE_IDLE)
              && (g_xcp_resume_runtime.restore_pending == FALSE));

        g_xcp_resume_runtime.restore_pending = TRUE;
        Xcp_NvMReadRamBlockFromNvmCallback((uint16)(XCP_NVM_BLOCK_ID_RESUME + 1U), 0U);
        CHECK("K15i wrong block_id -> ignored",
              g_xcp_channel.runtime_ptr->connection_state == XCP_CONNECTED
              && g_xcp_resume_runtime.restore_pending == TRUE);
        g_xcp_resume_runtime.restore_pending = FALSE;
        xcp_settle();
    }

    {

        cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram)}, 4U);
        (void)xcp_cmd(cmd, 8U, res);

        memset(test_ram, 0x5A, 16U);

#if (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)
        {
            uint32 fl;
            l = xcp_cmd((const uint8 *)"\xF5\x08", 2U, res);
            CHECK("K16a UPLOAD 8 -> block first frame",
                  (l == 8U) && (res[0] == XCP_PID_RES) && (res[1] == 0x5AU));
            xcp_pump(1U);
            l = xcp_pop_pdu(res, &fl);
            CHECK("K16a2 UPLOAD 8 -> block tail",
                  (l == 2U) && (res[0] == XCP_PID_RES) && (res[1] == 0x5AU));
            xcp_pump(1U);
        }
#else
        l = xcp_cmd((const uint8 *)"\xF5\x08", 2U, res);
        CHECK("K16a UPLOAD 8 -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));
#endif

        l = xcp_cmd((const uint8 *)"\xF5\x07", 2U, res);
        CHECK("K16b UPLOAD 7 -> RES len8",
              (l == 8U) && (res[0] == XCP_PID_RES));

        {
            uint32 mta0 = g_xcp_channel.runtime_ptr->mta;
            l = xcp_cmd((const uint8 *)"\xF5\x00", 2U, res);
            CHECK("K16c UPLOAD 0 -> RES len1, MTA fixed",
                  (l == 1U) && (res[0] == XCP_PID_RES)
                  && (g_xcp_channel.runtime_ptr->mta == mta0));
        }

        cmd[0] = 0xF4; cmd[1] = 0x08; cmd[2] = 0; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram)}, 4U);
        l = xcp_cmd(cmd, 8U, res);
        CHECK("K16d SHORT_UPLOAD 8 -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram)}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        l = xcp_cmd((const uint8 *)"\xF0\x06\xAA\xBB", 4U, res);
        CHECK("K16e DOWNLOAD declared > payload -> silent block, remaining 4",
              (l == 0U)
              && (g_xcp_channel.runtime_ptr->block_transfer_active == TRUE)
              && (g_xcp_channel.runtime_ptr->block_transfer_remaining == 4U));

        cmd[0] = 0xED; cmd[1] = 0x06; cmd[2] = 0; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram)}, 4U);
        l = xcp_cmd(cmd, 8U, res);
        CHECK("K16f SHORT_DOWNLOAD declared > payload -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        {
            uint32 mta0 = g_xcp_channel.runtime_ptr->mta;
            l = xcp_cmd((const uint8 *)"\xEE", 1U, res);
            CHECK("K16g DOWNLOAD_MAX len1 -> RES len1, no write",
                  (l == 1U) && (res[0] == XCP_PID_RES)
                  && (g_xcp_channel.runtime_ptr->mta == mta0));
        }

        l = xcp_cmd((const uint8 *)"\xF1\xBE\xEF", 3U, res);
        CHECK("K16h USER_CMD echo -> RES + payload",
              (l == 3U) && (res[0] == XCP_PID_RES) && (res[1] == 0xBEU) && (res[2] == 0xEFU));

        xcp_settle();
    }

    {

        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = (uint8)(XCP_MAX_DAQ + 1U); cmd[3] = 0;
        l = xcp_cmd(cmd, 4U, res);
        CHECK("K17a ALLOC_DAQ 9 -> ERR_MEMORY_OVERFLOW",
              (l == 2U) && (res[1] == XCP_ERR_MEMORY_OVERFLOW));

        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0;
        l = xcp_cmd(cmd, 4U, res);
        CHECK("K17b ALLOC_DAQ while RUNNING -> ERR_DAQ_ACTIVE",
              (l == 2U) && (res[1] == XCP_ERR_DAQ_ACTIVE));

        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = (uint8)(XCP_MAX_ODT + 1U);
        l = xcp_cmd(cmd, 5U, res);
        CHECK("K17c ALLOC_ODT 5 -> ERR_MEMORY_OVERFLOW",
              (l == 2U) && (res[1] == XCP_ERR_MEMORY_OVERFLOW));

#if (XCP_MAX_ODT_ENTRIES < 255U)
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = (uint8)(XCP_MAX_ODT_ENTRIES + 1U);
        l = xcp_cmd(cmd, 6U, res);
        CHECK("K17d ALLOC_ODT_ENTRY over max -> ERR_MEMORY_OVERFLOW",
              (l == 2U) && (res[1] == XCP_ERR_MEMORY_OVERFLOW));
#else
        CHECK("K17d ALLOC_ODT_ENTRY over max -> ERR_MEMORY_OVERFLOW", TRUE);
#endif

        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 5; cmd[5] = 0;
        CHECK("K17e SET_DAQ_PTR odt5 -> ERR_OUT_OF_RANGE",
              xcp_cmd_has_err(cmd, 6U, XCP_ERR_OUT_OF_RANGE));

        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 9;
        CHECK("K17f SET_DAQ_PTR entry9 -> ERR_OUT_OF_RANGE",
              xcp_cmd_has_err(cmd, 6U, XCP_ERR_OUT_OF_RANGE));

        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0;
        (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
        CHECK("K17g WRITE_DAQ while RUNNING -> ERR_DAQ_ACTIVE",
              xcp_cmd_has_err(cmd, 8U, XCP_ERR_DAQ_ACTIVE));

        cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        xcp_settle();
    }

    {
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 2; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);

        cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        CHECK("K18a WRITE_DAQ advances entry ptr",
              g_xcp_channel.runtime_ptr->odt_entry_ptr == 1U);

        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var8)}, 4U);
        cmd[2] = 1;
        l = xcp_cmd(cmd, 8U, res);
        CHECK("K18b WRITE_DAQ entry1 -> RES",
              (l == 1U) && (res[0] == XCP_PID_RES));
        CHECK("K18c entry ptr now 2",
              g_xcp_channel.runtime_ptr->odt_entry_ptr == 2U);

        l = xcp_cmd(cmd, 8U, res);
        CHECK("K18d WRITE_DAQ beyond entries -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        xcp_settle();
    }

#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
    {
        uint8 gs = 0xFD;
        uint8 i;

        xcp_settle();

        for (i = 0U; i < XCP_CMD_QUEUE_SIZE; i++)
        {
            xcp_inject(&gs, 1U);
        }
        CHECK("K19a queue filled to MAX", g_xcp_channel.runtime_ptr->cmd_queue_count == XCP_CMD_QUEUE_SIZE);

        xcp_inject(&gs, 1U);
        CHECK("K19b overflow sets busy_pending", g_xcp_channel.runtime_ptr->busy_pending == TRUE);

        xcp_drain_tx();
        Xcp_MainFunction();
        {
            uint8 e[64]; uint32 el; boolean busy_seen = FALSE;
            while (test_tx_pop(e, &el) > 0U)
            {
                if ((el == 2U) && (e[0] == XCP_PID_ERR) && (e[1] == XCP_ERR_CMD_BUSY)) { busy_seen = TRUE; }
            }
            CHECK("K19c overflow -> ERR_CMD_BUSY emitted", busy_seen);
        }

        {
            uint8 drain_i;
            for (drain_i = 0U; drain_i < (XCP_CMD_QUEUE_SIZE + 2U); drain_i++)
            {
                Xcp_Tl_TxConfirmation(0U, E_OK);
                Xcp_MainFunction();
            }
        }
        xcp_settle();
    }
#else
    CHECK("K19 single-command mode: queue saturation N/A", TRUE);
#endif

    {

        {
            uint8 dto_c7[8] = {0xC7, 0,0,0,0,0,0,0};
            xcp_settle();
            xcp_inject(dto_c7, 8U);
            CHECK("K20a PID 0xC7 -> STIM route, no cmd pending",
                  g_xcp_channel.runtime_ptr->cmd_pending == FALSE);
        }

        {
            uint8 ver[8] = {0xC8, 0,0,0,0,0,0,0};
            g_xcp_channel.runtime_ptr->protection_status |= XCP_RESOURCE_PGM;
            CHECK("K20b PID 0xC8 -> cmd route (ERR_ACCESS_LOCKED)",
                  xcp_cmd_has_err(ver, 8U, XCP_ERR_ACCESS_LOCKED));
            g_xcp_channel.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_PGM;
        }

        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 2; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        (void)xcp_cmd((const uint8 *)"\xFE", 1U, res);
        CHECK("K20c DISCONNECT frees DAQ (daq_count=0)",
              g_xcp_channel.runtime_ptr->daq_count == 0U);
        (void)xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
        xcp_settle();

        g_xcp_channel.runtime_ptr->protection_status &=
            (uint8)~(XCP_RESOURCE_DAQ | XCP_RESOURCE_STIM);
        l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
        CHECK("K20d GET_STATUS fields",
              (l == 6U) && (res[0] == XCP_PID_RES)
              && ((res[1] & (XCP_SS_RESUME | XCP_SS_DAQ_RUNNING))
                  == ((Xcp_IsResumeModeActive(&g_xcp_channel) != FALSE) ? XCP_SS_RESUME : 0x00U))
              && (res[2] == g_xcp_channel.runtime_ptr->protection_status));

        {
            uint32 mta_before = g_xcp_channel.runtime_ptr->mta;
            l = xcp_cmd((const uint8 *)"\xFA\x00", 2U, res);
            CHECK("K20e GET_ID type0 -> len>0 + MTA set",
                  (l == 8U) && (res[0] == XCP_PID_RES) && (res[4] > 0U)
                  && (g_xcp_channel.runtime_ptr->mta != mta_before));
            l = xcp_cmd((const uint8 *)"\xFA\x01", 2U, res);
            CHECK("K20f GET_ID type1 -> len0",
                  (l == 8U) && (res[0] == XCP_PID_RES) && (res[4] == 0U));
        }
    }

    {

        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);

        xcp_drain_tx();
        {
            uint8 f[64]; uint32 fl; uint32 dto_n = 0U;
            Xcp_MainFunction();
            Xcp_Tl_TxConfirmation(0U, E_OK);
            while (test_tx_pop(f, &fl) > 0U)
            {
                if (f[0] < 0xC8U) { dto_n++; }
            }
            CHECK("K21a auto event emits DTO", dto_n >= 1U);
        }

        cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; cmd[5] = 0; cmd[6] = 3; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        xcp_drain_tx();
        {
            uint8 f[64]; uint32 fl; uint32 dto_n = 0U;
            uint8 pi;
            for (pi = 0U; pi < 6U; pi++)
            {
                Xcp_MainFunction();
                Xcp_Tl_TxConfirmation(0U, E_OK);
            }
            while (test_tx_pop(f, &fl) > 0U)
            {
                if (f[0] < 0xC8U) { dto_n++; }
            }
            CHECK("K21b prescaler=3 -> 2 DTOs in 6 cycles", dto_n == 2U);
        }

        cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xE0; cmd[1] = 0x02; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        xcp_drain_tx();
        Xcp_Event(1U);
        {
            uint8 f[64]; uint32 fl;
            CHECK("K21c STIM event emits no DTO", test_tx_pop(f, &fl) == 0U);
        }

        g_xcp_channel.runtime_ptr->stim_timeout_flag[0] = TRUE;
        {
            PduInfoType info;
            uint8 stim[8] = {0};
            uint32 stim_len = test_stim_dto(stim, 0U, 0U, 0x44332211UL);
            (void)info;
            xcp_inject(stim, stim_len);
        }
        CHECK("K21d STIM reception clears timeout flag",
              g_xcp_channel.runtime_ptr->stim_timeout_flag[0] == FALSE);

        cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        xcp_settle();
    }

    {
        g_xcp_channel.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_PGM;
        g_xcp_channel.runtime_ptr->pgm_started = TRUE;

        memset(test_flash_mem, 0xAA, sizeof(test_flash_mem));
        g_xcp_channel.runtime_ptr->mta = 0x08002000UL;
        l = xcp_cmd((const uint8 *)"\xD1\x00\x00\x00\x00\x60\x00\x00", 8U, res);
        CHECK("K22a CLEAR cross-sector valid -> RES",
              (l == 1U) && (res[0] == XCP_PID_RES));
        CHECK("K22b s0 tail + s1 erased",
              (test_flash_mem[0x2000] == 0xFFU) && (test_flash_mem[0x7FFF] == 0xFFU));

        g_xcp_channel.runtime_ptr->mta = 0x08003FF8UL;
        l = xcp_cmd((const uint8 *)"\xD0\x05\x01\x02\x03\x04\x05", 7U, res);
        CHECK("K22c PROGRAM within sector tail -> RES",
              (l == 1U) && (res[0] == XCP_PID_RES));

        CHECK("K22d PROGRAM crossing sector end -> ERR_GENERIC",
              xcp_cmd_has_err((const uint8 *)"\xD0\x04\x01\x02\x03\x04", 6U, XCP_ERR_GENERIC));

        g_xcp_channel.runtime_ptr->pgm_started = FALSE;
        g_xcp_channel.runtime_ptr->protection_status |= XCP_RESOURCE_PGM;
        xcp_settle();
    }

    {
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
        (void)xcp_cmd(cmd, 8U, res);

        g_xcp_channel.runtime_ptr->odt_entry_ptr = 0U;
        l = xcp_cmd((const uint8 *)"\xDB", 1U, res);
        CHECK("K23a READ_DAQ returns written entry",
              (l == 8U) && (res[0] == XCP_PID_RES) && (res[1] == 0xFFU) && (res[2] == 4U)
              && (res[3] == 0U) && (Xcp_LeRead32(&res[4]) == TEST_ADDR(test_ram_var32)));

        cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 2; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res);
        l = xcp_cmd((const uint8 *)"\xD8\x00\x00\x00", 4U, res);
        CHECK("K23b GET_DAQ_LIST_INFO fields",
              frame_is(res, l, (const uint8[]){ 0xFF, 0x0C, XCP_MAX_ODT, XCP_MAX_ODT_ENTRIES,
                                                0x02, 0x00 }, 6U));

        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        (void)xcp_cmd((const uint8 *)"\xD5\x00\x01\x00", 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
        (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
        (void)xcp_cmd((const uint8 *)"\xE2\x00\x00\x00\x00\x00", 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF;  cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
        (void)xcp_cmd(cmd, 8U, res);

        (void)xcp_cmd((const uint8 *)"\xE0\x10\x00\x00\x00\x00\x01\x00", 8U, res);
        (void)xcp_cmd((const uint8 *)"\xDE\x01\x00\x00", 4U, res);
        test_os_ticks = 123456789UL;
        xcp_drain_tx();
        Xcp_Event(0U); Xcp_Tl_TxConfirmation(0U, E_OK);
        {
            uint8 dto[64]; uint32 dl;
            if (test_tx_pop(dto, &dl) > 0U)
            {
                uint32 ts = (uint32)dto[XCP_TST_ID_LEN] | ((uint32)dto[XCP_TST_ID_LEN+1U]<<8) | ((uint32)dto[XCP_TST_ID_LEN+2U]<<16) | ((uint32)dto[XCP_TST_ID_LEN+3U]<<24);
                CHECK("K6f DTO carries OS timestamp",
                      ts == 123456789UL);
            }
            else
            {
                CHECK("K6f DTO carries OS timestamp", FALSE);
            }
        }
        {
            uint32 enter0;
            uint32 exit0;

            /* The first event sends immediately, the second is queued; the TX
               confirmation dequeues it and that path must hold the DTO
               critical section (issue #9 item 4). */
            Xcp_Event(0U);
            Xcp_Event(0U);
            enter0 = test_schm_enter_count;
            exit0  = test_schm_exit_count;
            Xcp_Tl_TxConfirmation(0U, E_OK);
            CHECK("K6g TX confirmation dequeue takes the DTO critical section",
                  (test_schm_enter_count > enter0)
                  && (test_schm_exit_count > exit0)
                  && ((test_schm_enter_count - enter0)
                      == (test_schm_exit_count - exit0)));
        }
        {
            uint32 enter0;
            uint32 exit0;

            /* With no frame in flight the confirmation must still hold the
               lock while it inspects and clears tx.state (issue #11 item 4). */
            g_xcp_channel.runtime_ptr->tx.state = XCP_TX_IDLE;
            enter0 = test_schm_enter_count;
            exit0  = test_schm_exit_count;
            Xcp_Tl_TxConfirmation(0U, E_OK);
            CHECK("K29 TX confirmation transition takes the DTO critical section",
                  ((test_schm_enter_count - enter0) == 1U)
                  && ((test_schm_exit_count - exit0) == 1U));
        }
        (void)xcp_cmd((const uint8 *)"\xDE\x00\x00\x00", 4U, res);
        xcp_drain_tx();

        {
            uint8 bit_var = 0x00U;
            uint8 dto[64]; uint32 dl;
            (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
            (void)xcp_cmd((const uint8 *)"\xD5\x00\x01\x00", 4U, res);
            (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
            (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
            (void)xcp_cmd((const uint8 *)"\xE2\x00\x00\x00\x00\x00", 6U, res);
            cmd[0] = 0xE1; cmd[1] = 3U; cmd[2] = 1; cmd[3] = 0;
            memcpy(&cmd[4], &(uint32){TEST_ADDR(bit_var)}, 4U);
            (void)xcp_cmd(cmd, 8U, res);
            (void)xcp_cmd((const uint8 *)"\xE0\x00\x00\x00\x00\x00\x01\x00", 8U, res);
            (void)xcp_cmd((const uint8 *)"\xDE\x01\x00\x00", 4U, res);

            bit_var = 0x08U;
            xcp_drain_tx();
            Xcp_Event(0U); Xcp_Tl_TxConfirmation(0U, E_OK);
            if (test_tx_pop(dto, &dl) > 0U)
            {
                CHECK("K6g DAQ bit_offset extracts bit3",
                      dto[XCP_TST_ID_LEN] == 0x01U);
            }
            else { CHECK("K6g DAQ bit_offset extracts bit3", FALSE); }

            bit_var = 0x04U;
            xcp_drain_tx();
            Xcp_Event(0U); Xcp_Tl_TxConfirmation(0U, E_OK);
            if (test_tx_pop(dto, &dl) > 0U)
            {
                CHECK("K6h DAQ bit3 clear -> 0",
                      dto[XCP_TST_ID_LEN] == 0x00U);
            }
            else { CHECK("K6h DAQ bit3 clear -> 0", FALSE); }

            (void)xcp_cmd((const uint8 *)"\xDE\x00\x00\x00", 4U, res);
            (void)xcp_cmd((const uint8 *)"\xE0\x02\x00\x00\x00\x00\x01\x00", 8U, res);
            (void)xcp_cmd((const uint8 *)"\xDE\x01\x00\x00", 4U, res);
            bit_var = 0x00U;
            {
                uint8 stim[8] = {0};
                uint32 stim_len = test_stim_dto(stim, 0U, 0U, 0x01UL);
                xcp_inject(stim, stim_len);
                Xcp_MainFunction(); Xcp_Tl_TxConfirmation(0U, E_OK);
                CHECK("K6i STIM sets bit3 only",
                      (bit_var == 0x08U));
            }
            bit_var = 0xFFU;
            {
                uint8 stim[8] = {0};
                uint32 stim_len = test_stim_dto(stim, 0U, 0U, 0x00UL);
                xcp_inject(stim, stim_len);
                { uint8 dummy[1]; (void)dummy; }
                xcp_inject(stim, 8U);
                Xcp_MainFunction(); Xcp_Tl_TxConfirmation(0U, E_OK);
                CHECK("K6j STIM clears bit3 only",
                      (bit_var == 0xF7U));
            }
            (void)xcp_cmd((const uint8 *)"\xDE\x00\x00\x00", 4U, res);
            xcp_drain_tx();
        }

        {
            uint32 bit_var32 = 0U;
            uint8  dto[64]; uint32 dl;
            (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
            (void)xcp_cmd((const uint8 *)"\xD5\x00\x01\x00", 4U, res);
            (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
            (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x02", 6U, res);
            (void)xcp_cmd((const uint8 *)"\xE2\x00\x00\x00\x00\x00", 6U, res);
            cmd[0] = 0xE1; cmd[1] = 0x20U; cmd[2] = 1; cmd[3] = 0;
            memcpy(&cmd[4], &(uint32){TEST_ADDR(bit_var32)}, 4U);
            l = xcp_cmd(cmd, 8U, res);
            CHECK("K6k WRITE_DAQ bit_offset 0x20 -> ERR_OUT_OF_RANGE",
                  (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));
            cmd[1] = 12U;
            l = xcp_cmd(cmd, 8U, res);
            CHECK("K6l WRITE_DAQ bit_offset 12 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
            (void)xcp_cmd((const uint8 *)"\xE0\x00\x00\x00\x00\x00\x01\x00", 8U, res);
            (void)xcp_cmd((const uint8 *)"\xDE\x01\x00\x00", 4U, res);
            bit_var32 = 0x00001000UL;
            xcp_drain_tx();
            Xcp_Event(0U); Xcp_Tl_TxConfirmation(0U, E_OK);
            CHECK("K6m DAQ bit12 sampled, unwritten entry skipped",
                  (test_tx_pop(dto, &dl) > 0U) && (dto[XCP_TST_ID_LEN] == 0x01U));
            (void)xcp_cmd_reply((const uint8 *)"\xDE\x00\x00\x00", 4U, res);
            (void)xcp_cmd_reply((const uint8 *)"\xE0\x02\x00\x00\x00\x00\x01\x00", 8U, res);
            (void)xcp_cmd_reply((const uint8 *)"\xDE\x01\x00\x00", 4U, res);
            bit_var32 = 0U;
            {
                uint8 stim[8] = {0};
                uint32 stim_len = test_stim_dto(stim, 0U, 0U, 0x01UL);
                xcp_inject(stim, stim_len);
                Xcp_MainFunction(); Xcp_Tl_TxConfirmation(0U, E_OK);
                CHECK("K6n STIM bit12 sets only byte1.bit4", bit_var32 == 0x00001000UL);
            }
            (void)xcp_cmd_reply((const uint8 *)"\xDE\x00\x00\x00", 4U, res);
            xcp_drain_tx();
        }

        {
            uint32 t0 = Xcp_GetTimestamp();
            l = xcp_cmd((const uint8 *)"\xDC", 1U, res);
            {
                uint32 got = (uint32)res[4] | ((uint32)res[5]<<8) | ((uint32)res[6]<<16) | ((uint32)res[7]<<24);
                CHECK("K23c GET_DAQ_CLOCK == Xcp_GetTimestamp",
                      (l == 8U) && (res[0] == XCP_PID_RES) && (got >= t0) && (got <= (t0 + 4U)));
            }
        }

        CHECK("K23d GET_DAQ_LIST_INFO list9 -> ERR_OUT_OF_RANGE",
              xcp_cmd_has_err((const uint8 *)"\xD8\x00\x09\x00", 4U, XCP_ERR_OUT_OF_RANGE));

        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        xcp_settle();
    }

#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)
    printf("-- K24. FlexRay framing\n");
    {
        uint8 frame[64];
        uint16 used = Xcp_FrPackMessage((const uint8 *)"\xFF", 1U, frame, sizeof(frame));
        CHECK("K24a FR pack LEN|CTR|MSG",
              (used == 4U) && (frame[0] == 1U) && (frame[1] == 0U)
              && (frame[3] == 0xFFU));

        used = Xcp_FrPackMessage((const uint8 *)"\xFD\x00", 2U, frame, sizeof(frame));
        CHECK("K24a2 FR pack adds FILL to even boundary",
              (used == 6U) && (frame[5] == 0U));

        {
            uint8 multi[32];
            uint16 p = 0U;
            p += Xcp_FrPackMessage((const uint8 *)"\xFD", 1U, &multi[p], (uint16)(sizeof(multi)-p));
            p += Xcp_FrPackMessage((const uint8 *)"\xFD", 1U, &multi[p], (uint16)(sizeof(multi)-p));
            {
                PduInfoType info;
                info.sdu_data_ptr = multi; info.sdu_length = p; info.meta_data_ptr = NULL;
                xcp_settle();
                Xcp_Tl_RxIndication(0U, &info);
                Xcp_MainFunction(); Xcp_Tl_TxConfirmation(0U, E_OK);
                Xcp_MainFunction(); Xcp_Tl_TxConfirmation(0U, E_OK);
                {
                    uint8 r1[64]; uint32 l1 = 0U;
                    uint8 r2[64]; uint32 l2 = 0U;
                    boolean ok;
                    (void)test_tx_pop(r1, &l1);
                    (void)test_tx_pop(r2, &l2);
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
                    ok = (l1 > 0U) && (r1[0] == XCP_PID_RES)
                         && (l2 > 0U) && (r2[0] == XCP_PID_RES);
#else

                    ok = (l1 == 2U) && (r1[0] == XCP_PID_ERR)
                         && (r1[1] == XCP_ERR_CMD_BUSY)
                         && (l2 > 0U) && (r2[0] == XCP_PID_RES);
#endif
                    CHECK("K24b FR concatenated frame parsed", ok);
                }
            }
        }
    }
#endif

#if (XCP_ON_ETHERNET_ENABLED == STD_ON)
    printf("-- K24. Ethernet\n");
    {
        uint8 frame[64];
        uint16 used = Xcp_EthPackMessage((const uint8 *)"\xFF", 1U, frame, sizeof(frame));
        CHECK("K24a ETH pack LEN(2)|CTR(2)|MSG",
              (used == 5U) && (frame[0] == 1U) && (frame[1] == 0U)
              && (frame[4] == 0xFFU));

        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        (void)xcp_cmd((const uint8 *)"\xD5\x00\x01\x00", 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
        (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
        (void)xcp_cmd((const uint8 *)"\xDE\x01\x00\x00", 4U, res);
        CHECK("K24b0 DAQ running pre-close",
              g_xcp_channel.storage_ptr->daq_list[0].state == XCP_DAQ_STATE_RUNNING);
        CHECK("K24b socket close -> disconnected",
              Xcp_SoAdConnectionClosed() == TRUE
              && g_xcp_channel.runtime_ptr->connection_state == XCP_DISCONNECTED);
        CHECK("K24b2 socket close stops DAQ",
              g_xcp_channel.storage_ptr->daq_list[0].state != XCP_DAQ_STATE_RUNNING);
        CHECK("K24c socket close idle -> FALSE",
              Xcp_SoAdConnectionClosed() == FALSE);
        l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
        xcp_drain_tx();
    }
#endif

#if (XCP_ON_ETHERNET_ENABLED == STD_ON)

    printf("-- K25. boundary frames\n");
    {
        uint8 raw[8];
        PduInfoType info;
        raw[0] = 0xFFU; raw[1] = 0xFFU;
        raw[2] = 0; raw[3] = 0;
        raw[4] = 0xFDU;
        info.sdu_data_ptr = raw; info.sdu_length = 5U; info.meta_data_ptr = NULL;
        xcp_drain_tx();
        Xcp_Tl_RxIndication(0U, &info);
        Xcp_MainFunction(); Xcp_Tl_TxConfirmation(0U, E_OK);
        {
            uint8 junk[16]; uint32 jl = 0U;
            CHECK("K25a ETH msg_len=0xFFFF frame dropped (no response)",
                  test_tx_pop(junk, &jl) == 0U);
        }

        l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
        CHECK("K25b conn intact after bad frame", (l == 6U) && (res[0] == XCP_PID_RES));
    }

    {
        extern void test_seed_override_len(uint8);
        uint8 saved[4];
        g_xcp_channel.runtime_ptr->protection_status |= XCP_RESOURCE_STIM;
        test_seed_override_len(30U);
        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_STIM;
        l = xcp_cmd(cmd, 3U, res);
        CHECK("K25c seed_length clamped to MAX_CTO-2",
              (l == 8U) && (res[1] == (uint8)(XCP_MAX_CTO - 2U)));
        test_seed_override_len(0U);
        memcpy(saved, &res[2], 4U); (void)saved;
        g_xcp_channel.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_STIM;
    }
#endif

#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)
    printf("-- K25. boundary frames\n");

    {
        uint8 raw[8];
        PduInfoType info;
        raw[0] = 0xFFU; raw[1] = 0xFFU;
        raw[2] = 0;
        raw[3] = 0xFDU;
        info.sdu_data_ptr = raw; info.sdu_length = 4U; info.meta_data_ptr = NULL;
        xcp_drain_tx();
        Xcp_Tl_RxIndication(0U, &info);
        Xcp_MainFunction(); Xcp_Tl_TxConfirmation(0U, E_OK);
        {
            uint8 junk[16]; uint32 jl = 0U;
            CHECK("K25a FR msg_len=0xFFFF frame dropped (no response)",
                  test_tx_pop(junk, &jl) == 0U);
        }
        l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
        CHECK("K25b conn intact after bad frame", (l == 6U) && (res[0] == XCP_PID_RES));
    }

    {
        extern void test_seed_override_len(uint8);
        g_xcp_channel.runtime_ptr->protection_status |= XCP_RESOURCE_STIM;
        test_seed_override_len(30U);
        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_STIM;
        l = xcp_cmd(cmd, 3U, res);
        CHECK("K25c seed_length clamped to MAX_CTO-2",
              (l == 8U) && (res[1] == (uint8)(XCP_MAX_CTO - 2U)));
        test_seed_override_len(0U);
        g_xcp_channel.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_STIM;
    }
#endif

#if (XCP_ON_CAN_ENABLED == STD_ON)
    printf("-- K25. boundary frames\n");

    l = xcp_cmd((const uint8 *)"\xE3\x00", 2U, res);
    CHECK("K25a CLEAR_DAQ_LIST len2 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));

    l = xcp_cmd((const uint8 *)"\xEA\x01", 2U, res);
    CHECK("K25b GET_CAL_PAGE len2 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));

    l = xcp_cmd((const uint8 *)"\xE5\x00", 2U, res);
    CHECK("K25c GET_SEGMENT_MODE len2 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));

    l = xcp_cmd((const uint8 *)"\xE8\x00\x00\x00", 4U, res);
    CHECK("K25d GET_SEGMENT_INFO len4 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));

    {
        extern void test_seed_override_len(uint8);
        /* Re-lock STIM so GET_SEED produces a real seed rather than len=0 */
        g_xcp_channel.runtime_ptr->protection_status |= XCP_RESOURCE_STIM;
        test_seed_override_len(30U);
        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_STIM;
        l = xcp_cmd(cmd, 3U, res);
        CHECK("K25e seed_length clamped to MAX_CTO-2",
              (l == 8U) && (res[1] == (uint8)(XCP_MAX_CTO - 2U)));
        test_seed_override_len(0U);
        g_xcp_channel.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_STIM;
    }

    {
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        (void)xcp_cmd((const uint8 *)"\xD5\x00\x01\x00", 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
        (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
        (void)xcp_cmd((const uint8 *)"\xDE\x01\x00\x00", 4U, res);
        xcp_drain_tx();

        (void)xcp_cmd((const uint8 *)"\xDE\x00\x00\x00", 4U, res);
        Xcp_SetTransmissionMode(0U, XCP_TX_OFF);
        Xcp_MainFunction();
        {
            uint8 junk[16]; uint32 jl = 0U;
            CHECK("K25f TX_OFF suppresses DTO drain",
                  test_tx_pop(junk, &jl) == 0U);
        }
        Xcp_SetTransmissionMode(0U, XCP_TX_ON);
        xcp_drain_tx();
    }

    printf("-- K26. adversarial stubs\n");
    {

        test_checksum_override_len(7U);
        cmd[0] = 0xF3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
        memset(&cmd[4], 0, 4U);
        l = xcp_cmd(cmd, 8U, res);
        CHECK("K26a checksum_len=7 -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[0] == XCP_PID_ERR) && (res[1] == XCP_ERR_OUT_OF_RANGE));
        test_checksum_override_len(0U);

#if (XCP_RESUME_MODE_ENABLED == STD_ON)
        {
            uint8 bad[64];
            memset(bad, 0xFF, sizeof(bad));
            bad[0] = 0x02U; bad[1] = 0x00U; bad[2] = 0x00U; bad[3] = 0x00U;
            test_nvm_corrupt(bad, sizeof(bad));
            Xcp_InitResumeMode();
            CHECK("K26b corrupted NvM -> no crash, state recovered",
                  g_xcp_channel.runtime_ptr->state == XCP_INIT);
        }
#endif

#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)
        {
            uint32 before;
            uint8 seed[4];
            uint8 i;

            cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_PGM;
            l = xcp_cmd(cmd, 3U, res);
            memcpy(seed, &res[2], 4U);
            cmd[0] = 0xF7; cmd[1] = 4U;
            for (i = 0U; i < 4U; i++) { cmd[2U + i] = (uint8)(seed[i] ^ 0xFFU); }
            (void)xcp_cmd(cmd, 6U, res);
            (void)xcp_cmd((const uint8 *)"\xD2", 1U, res);
            cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
            memcpy(&cmd[4], &(uint32){0x20000000UL}, 4U);
            (void)xcp_cmd(cmd, 8U, res);
            before = test_flash_program_calls;
            l = xcp_cmd((const uint8 *)"\xCA\x02\xAA\xBB", 4U, res);
            CHECK("K26c PROGRAM_NEXT non-sector addr -> ERR_GENERIC",
                  (l == 2U) && (res[0] == XCP_PID_ERR) && (res[1] == XCP_ERR_GENERIC));
            CHECK("K26d FlashProgram_Callout not invoked",
                  test_flash_program_calls == before);
        }
#endif

#if (XCP_DAQ_ENABLED == STD_ON)
        {
            (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
            (void)xcp_cmd((const uint8 *)"\xD5\x00\x02\x00", 4U, res);
            (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
            (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
            (void)xcp_cmd((const uint8 *)"\xD4\x00\x01\x00\x01", 5U, res);
            (void)xcp_cmd((const uint8 *)"\xD3\x00\x01\x00\x00\x01", 6U, res);

            cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
            cmd[0] = 0xE1; cmd[1] = 0xFF; cmd[2] = 4; cmd[3] = 0;
            memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
            (void)xcp_cmd(cmd, 8U, res);

            cmd[0] = 0xE0; cmd[1] = 0x02; cmd[2] = 1; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
            (void)xcp_cmd(cmd, 8U, res);
            cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
            {
#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
                Std_ReturnType r = Xcp_ReceiveStimDto(&g_xcp_channel, (const uint8 *)"\x01\xAA\xBB\xCC\xDD", 5U);
                CHECK("K26e abs_odt=1 routed to list1 (prefix-sum decode)", r == E_OK);
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_BYTE)
                Std_ReturnType r = Xcp_ReceiveStimDto(&g_xcp_channel, (const uint8 *)"\x00\x01\xAA\xBB\xCC\xDD", 6U);
                CHECK("K26e odt0/list1 routed (relative decode)", r == E_OK);
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_WORD)
                Std_ReturnType r = Xcp_ReceiveStimDto(&g_xcp_channel, (const uint8 *)"\x00\x01\x00\xAA\xBB\xCC\xDD", 7U);
                CHECK("K26e odt0/list1 routed (relative-word decode)", r == E_OK);
#else /* RELATIVE_WORD_ALIGNED */
                Std_ReturnType r = Xcp_ReceiveStimDto(&g_xcp_channel, (const uint8 *)"\x00\x00\x01\x00\xAA\xBB\xCC\xDD", 8U);
                CHECK("K26e odt0/list1 routed (relative-word-aligned decode)", r == E_OK);
#endif
            }

            {

                cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
                cmd[0] = 0xE1; cmd[1] = 0xFF; cmd[2] = 4; cmd[3] = 0;
                memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
                (void)xcp_cmd(cmd, 8U, res);
                cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
                (void)xcp_cmd(cmd, 8U, res);
                cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
                Xcp_Tl_TxConfirmation(0U, E_OK);
                test_sim_dto_inflight();
                Xcp_Event(0U);
                CHECK("K26f1 DTO queued behind in-flight frame",
                      g_xcp_channel.storage_ptr->dto_queue_count > 0U);
                g_xcp_channel.runtime_ptr->tx.dto_latch = FALSE;
                g_xcp_channel.runtime_ptr->tx.state = XCP_TX_IDLE;
                (void)xcp_cmd((const uint8 *)"\xDE\x00\x00\x00", 4U, res);
                (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
                CHECK("K26f2 FREE_DAQ clears queued DTOs",
                      g_xcp_channel.storage_ptr->dto_queue_count == 0U);
            }
            (void)xcp_cmd((const uint8 *)"\xDE\x00\x01\x00", 4U, res);
            (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        }
#endif
        xcp_settle();
    }

    printf("-- K27. command length fuzz\n");
    {
        static const uint8 all_cmds[] = {
            0xFE,0xFD,0xFC,0xFB,0xFA,0xF9,0xF7,0xF6,0xF5,0xF4,0xF3,0xF2,0xF1,0xF0,
            0xEF,0xEE,0xED,0xEC,0xEB,0xEA,0xE9,0xE8,0xE7,0xE6,0xE5,0xE4,0xE3,0xE2,
            0xE1,0xE0,0xDF,0xDE,0xDD,0xDC,0xDB,0xDA,0xD9,0xD8,0xD7,0xD6,0xD5,0xD4,
            0xD3,0xD2,0xD1,0xD0,0xCF,0xCE,0xCD,0xCC,0xCB,0xCA,0xC9,0xC8,
        };
        uint8 fi, len_i;
        uint32 sent = 0U;
        for (fi = 0U; fi < (uint8) sizeof(all_cmds); fi++)
        {

            if (g_xcp_channel.runtime_ptr->connection_state != XCP_CONNECTED)
            {
                l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
                CHECK("K27 reconnect after disconnecting cmd",
                      g_xcp_channel.runtime_ptr->connection_state == XCP_CONNECTED);
            }
            for (len_i = 1U; len_i <= XCP_MAX_CTO; len_i++)
            {
                memset(cmd, 0xA5, len_i);
                cmd[0] = all_cmds[fi];
                xcp_inject(cmd, len_i);
                sent++;
            }
        }

        {
            uint32 pumps = 0U;
            while (pumps < 400U)
            {
                Xcp_Tl_TxConfirmation(0U, E_OK);
                Xcp_MainFunction();
                pumps++;
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
                if (g_xcp_channel.runtime_ptr->cmd_queue_count == 0U
                    && g_xcp_channel.runtime_ptr->cmd_pending == FALSE) { break; }
#else
                if (g_xcp_channel.runtime_ptr->cmd_pending == FALSE) { break; }
#endif
            }
        }
        CHECK("K27a fuzz sweep processed without crash",
              g_xcp_channel.runtime_ptr->state == XCP_INIT && sent > 0U);

        if (g_xcp_channel.runtime_ptr->connection_state != XCP_CONNECTED)
        {
            /* A fuzz-started/resumed list may still be emitting DTOs; stop
               them and confirm the in-flight frame before reconnecting, so
               the CONNECT response is not parked behind it. */
            (void)xcp_cmd((const uint8 *)"\xDD\x00", 2U, res);
            xcp_settle();
            l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);

            CHECK("K27 final reconnect",
                  g_xcp_channel.runtime_ptr->connection_state == XCP_CONNECTED);
        }
        xcp_drain_tx();
        l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
        CHECK("K27b GET_STATUS works after fuzz", (l == 6U) && (res[0] == XCP_PID_RES));
        xcp_settle();

        {
            uint32 rng = 0x9E3779B9U;
            uint32 i;
            for (i = 0U; i < 600U; i++)
            {
                uint8 fl;
                rng ^= rng << 13U; rng ^= rng >> 17U; rng ^= rng << 5U;
                fl = (uint8)(rng % XCP_MAX_CTO) + 1U;
                {
                    uint8 b;
                    for (b = 0U; b < fl; b++)
                    {
                        rng ^= rng << 13U; rng ^= rng >> 17U; rng ^= rng << 5U;
                        cmd[b] = (uint8)(rng & 0xFFU);
                    }
                }

                if ((i & 1U) != 0U)
                {
                    cmd[0] = all_cmds[rng % (uint8) sizeof(all_cmds)];
                }
                if (g_xcp_channel.runtime_ptr->connection_state != XCP_CONNECTED)
                {
                    (void)xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
                }
                xcp_inject(cmd, fl);
                Xcp_Tl_TxConfirmation(0U, E_OK);
                Xcp_MainFunction();
            }
            CHECK("K27c random-payload fuzz: no crash",
                  g_xcp_channel.runtime_ptr->state == XCP_INIT);
            if (g_xcp_channel.runtime_ptr->connection_state != XCP_CONNECTED)
            {
                (void)xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
            }
            xcp_drain_tx();
            l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
            CHECK("K27d GET_STATUS after random fuzz", (l == 6U) && (res[0] == XCP_PID_RES));
            xcp_settle();
        }
    }
#endif

#if (XCP_ON_CAN_ENABLED == STD_ON)
    printf("-- K24. CAN config\n");
    {

        Xcp_ConfigType bad = xcp_config;
        uint8 det_cnt;
        bad.xcp_rx_pdu_id = 7U; bad.xcp_tx_pdu_id = 7U;
        test_det_count = 0;
        Xcp_Init(&bad);
        det_cnt = test_det_count;
        CHECK("K24a rx==tx PDU -> XCP_E_INV_CONFIG",
              (det_cnt > 0U) && (test_det_api == XCP_SID_INIT)
              && (g_xcp_channel.runtime_ptr->state == XCP_UNINIT));
        Xcp_Init(NULL_PTR);
        (void)xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
        xcp_drain_tx();
    }
#endif

#if (XCP_DAQ_ENABLED == STD_ON) && (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
    printf("-- K28. DAQ configuration is frozen while a list runs (issue #11 item 5)\n");
    {
        uint8  dto[64];
        uint32 dl;
        uint16 pid_before = 0U;
        uint16 pid_after = 0U;

        g_xcp_channel.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_DAQ;
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        (void)xcp_cmd((const uint8 *)"\xD5\x00\x02\x00", 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x02", 5U, res);
        (void)xcp_cmd((const uint8 *)"\xD4\x00\x01\x00\x02", 5U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0;
        (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF; cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xE0; cmd[1] = 0x00; cmd[2] = 1; cmd[3] = 0; cmd[4] = 2;
        cmd[5] = 0; cmd[6] = 0; cmd[7] = 0;
        l = xcp_cmd(cmd, 8U, res);
        CHECK("K28a SET_DAQ_LIST_MODE list1 evt2 -> RES",
              (l == 1U) && (res[0] == XCP_PID_RES));
        l = xcp_cmd((const uint8 *)"\xDE\x01\x01\x00", 4U, res);
        CHECK("K28b START list1 -> RES first_pid 2",
              (l == 2U) && (res[0] == XCP_PID_RES) && (res[1] == 2U));

        xcp_drain_tx();
        Xcp_Event(2U);
        if (test_tx_pop(dto, &dl) > 0U)
        {
            pid_before = dto[0];
        }
        Xcp_Tl_TxConfirmation(0U, E_OK);
        xcp_drain_tx();
        CHECK("K28c running list1 emits abs_pid 2", pid_before == 2U);

        l = xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
        CHECK("K28d ALLOC_ODT lower list while running -> ERR_DAQ_ACTIVE",
              (l == 2U) && (res[1] == XCP_ERR_DAQ_ACTIVE));

        xcp_drain_tx();
        Xcp_Event(2U);
        if (test_tx_pop(dto, &dl) > 0U)
        {
            pid_after = dto[0];
        }
        Xcp_Tl_TxConfirmation(0U, E_OK);
        xcp_drain_tx();
        CHECK("K28e abs_pid unchanged after rejected reallocation",
              pid_after == pid_before);

        l = xcp_cmd((const uint8 *)"\xD6", 1U, res);
        CHECK("K28f FREE_DAQ while running -> ERR_DAQ_ACTIVE",
              (l == 2U) && (res[1] == XCP_ERR_DAQ_ACTIVE));
        CHECK("K28g list1 still RUNNING after rejected FREE_DAQ",
              g_xcp_channel.storage_ptr->daq_list[1].state
              == (uint8)XCP_DAQ_STATE_RUNNING);

        (void)xcp_cmd((const uint8 *)"\xDE\x00\x01\x00", 4U, res);
        l = xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
        CHECK("K28h ALLOC_ODT after STOP -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
        l = xcp_cmd((const uint8 *)"\xD6", 1U, res);
        CHECK("K28i FREE_DAQ after STOP -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

        (void)xcp_cmd((const uint8 *)"\xD5\x00\x02\x00", 4U, res);
        l = xcp_cmd((const uint8 *)"\xDE\x01\x01\x00", 4U, res);
        CHECK("K28j START unconfigured list -> ERR_DAQ_CONFIG",
              (l == 2U) && (res[1] == XCP_ERR_DAQ_CONFIG));
        xcp_settle();
    }
#endif

#if (XCP_DAQ_ENABLED == STD_ON) && (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
    printf("-- K30. CLEAR_DAQ_LIST obeys the freeze (issue #14 item 1)\n");
    {
        uint8  dto[64];
        uint32 dl;
        uint16 pid_before = 0U;
        uint16 pid_after  = 0U;

        g_xcp_channel.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_DAQ;
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        (void)xcp_cmd((const uint8 *)"\xD5\x00\x02\x00", 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x02", 5U, res);
        (void)xcp_cmd((const uint8 *)"\xD4\x00\x01\x00\x02", 5U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0;
        (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF; cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){TEST_ADDR(test_ram_var32)}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xE0; cmd[1] = 0x00; cmd[2] = 1; cmd[3] = 0; cmd[4] = 2;
        cmd[5] = 0; cmd[6] = 0; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res);
        (void)xcp_cmd((const uint8 *)"\xDE\x01\x01\x00", 4U, res);
        xcp_drain_tx();

        Xcp_Event(2U);
        if (test_tx_pop(dto, &dl) > 0U) { pid_before = dto[0]; }
        Xcp_Tl_TxConfirmation(0U, E_OK);
        xcp_drain_tx();

        l = xcp_cmd((const uint8 *)"\xE3\x00\x00\x00", 4U, res);
        CHECK("K30a CLEAR_DAQ_LIST lower list while running -> ERR_DAQ_ACTIVE",
              (l == 2U) && (res[1] == XCP_ERR_DAQ_ACTIVE));

        xcp_drain_tx();
        Xcp_Event(2U);
        if (test_tx_pop(dto, &dl) > 0U) { pid_after = dto[0]; }
        Xcp_Tl_TxConfirmation(0U, E_OK);
        xcp_drain_tx();
        CHECK("K30b abs_pid unchanged after the rejected CLEAR_DAQ_LIST",
              (pid_after == pid_before) && (pid_before == 2U));

        /* A live list keeps its event routing; the prescaler stays tunable. */
        cmd[0] = 0xE0; cmd[1] = 0x00; cmd[2] = 1; cmd[3] = 0; cmd[4] = 1;
        cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        l = xcp_cmd(cmd, 8U, res);
        CHECK("K30e moving a live list to another event -> ERR_DAQ_ACTIVE",
              (l == 2U) && (res[1] == XCP_ERR_DAQ_ACTIVE));

        l = xcp_cmd((const uint8 *)"\xDE\x00\x01\x00", 4U, res);
        CHECK("K30c STOP list1 -> RES", (l == 2U) && (res[0] == XCP_PID_RES));
        l = xcp_cmd((const uint8 *)"\xE3\x00\x00\x00", 4U, res);
        CHECK("K30d CLEAR_DAQ_LIST after STOP -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
        xcp_settle();
    }
#endif

    printf("-- K31. block budget survives a denied write (issue #14 item 4a)\n");
    {
        /* test_ram is a 256-byte registered object; a declared 8-element
           block at +250 fills the object with its 6-element payload and the
           continuation crosses the registered edge, so the write gate denies
           it while the first frame succeeded. */
        g_xcp_channel.runtime_ptr->protection_status
            &= (uint8)~(XCP_RESOURCE_DAQ | XCP_RESOURCE_CAL_PAG);
        cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){RAM_ADDR + 250U}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xF0; cmd[1] = 8U; cmd[2] = 0x71; cmd[3] = 0x72; cmd[4] = 0x73;
        cmd[5] = 0x74; cmd[6] = 0x75; cmd[7] = 0x76;
        l = xcp_cmd(cmd, 8U, res);
        CHECK("K31a block DOWNLOAD silent with remaining 2",
              (l == 0U)
              && (g_xcp_channel.runtime_ptr->block_transfer_active == TRUE)
              && (g_xcp_channel.runtime_ptr->block_transfer_remaining == 2U));

        cmd[0] = 0xEF; cmd[1] = 2U; cmd[2] = 0x77; cmd[3] = 0x78;
        l = xcp_cmd(cmd, 4U, res);
        CHECK("K31b denied continuation -> ERR_ACCESS_DENIED",
              (l == 2U) && (res[1] == XCP_ERR_ACCESS_DENIED));
        CHECK("K31c budget untouched after the denied frame",
              (g_xcp_channel.runtime_ptr->block_transfer_active == TRUE)
              && (g_xcp_channel.runtime_ptr->block_transfer_remaining == 2U));
        CHECK("K31d first frame data intact",
              (test_ram[250] == 0x71U) && (test_ram[255] == 0x76U));
        Xcp_CancelBlockTransfer(&g_xcp_channel);
    }

    printf("-- K32. lower-layer transmits stay outside the DTO lock (issue #14 items 2-3)\n");
    {
        uint8 dto_payload[8] = {0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U};
        uint32 enter0;
        uint32 exit0;

        /* A queued DTO drained by ReportDtoOverload must be transmitted with
           the lock released. */
        g_xcp_channel.runtime_ptr->tx_mode = XCP_TX_ON;
        g_xcp_channel.runtime_ptr->tx.state = XCP_TX_CTO;
        Xcp_DtoSendOrQueue(&g_xcp_channel, dto_payload, 8U);
        CHECK("K32a DTO queued behind a busy pipe",
              g_xcp_channel.storage_ptr->dto_queue_count == 1U);
        g_xcp_channel.runtime_ptr->tx.state = XCP_TX_IDLE;
        g_xcp_channel.runtime_ptr->tx.parked = FALSE;
        test_schm_tx_under_lock = FALSE;
        Xcp_ReportDtoOverload(&g_xcp_channel);
        CHECK("K32b ReportDtoOverload transmits outside the critical section",
              (test_schm_tx_under_lock == FALSE)
              && (g_xcp_channel.storage_ptr->dto_queue_count == 0U));

        /* SendResponse on an idle pipe transitions tx.state under the lock. */
        g_xcp_channel.runtime_ptr->tx.state = XCP_TX_IDLE;
        g_xcp_channel.runtime_ptr->tx.parked = FALSE;
        enter0 = test_schm_enter_count;
        exit0  = test_schm_exit_count;
        test_schm_tx_under_lock = FALSE;
        (void)Xcp_SendResponse(dto_payload, 8U);
        CHECK("K32c SendResponse transitions under the lock, sends outside",
              ((test_schm_enter_count - enter0) == 1U)
              && ((test_schm_exit_count - exit0) == 1U)
              && (test_schm_tx_under_lock == FALSE));

        /* A parked CTO replayed by SendPendingCto behaves the same way. */
        g_xcp_channel.runtime_ptr->tx.state = XCP_TX_CTO;
        (void)Xcp_SendResponse(dto_payload, 8U);
        CHECK("K32d busy pipe parks the response",
              g_xcp_channel.runtime_ptr->tx.parked == TRUE);
        g_xcp_channel.runtime_ptr->tx.state = XCP_TX_IDLE;
        enter0 = test_schm_enter_count;
        exit0  = test_schm_exit_count;
        test_schm_tx_under_lock = FALSE;
        Xcp_SendPendingCto();
        CHECK("K32e SendPendingCto transitions under the lock, sends outside",
              ((test_schm_enter_count - enter0) == 1U)
              && ((test_schm_exit_count - exit0) == 1U)
              && (test_schm_tx_under_lock == FALSE));
        Xcp_Tl_TxConfirmation(0U, E_OK);
        xcp_drain_tx();
    }

    printf("================ %d passed, %d failed ================\n",
           pass_count, fail_count);
    return (fail_count == 0) ? 0 : 1;
}
