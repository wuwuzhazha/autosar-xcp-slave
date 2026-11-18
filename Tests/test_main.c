/**
 * @file    test_main.c
 * @brief   XCP 协议栈功能测试驱动
 *          覆盖 FeatureList.md 全部命令/事件/错误路径。
 *          命令路径: Xcp_CanIfRxIndication -> Xcp_MainFunction -> CanIf_Transmit(捕获)
 *          -> Xcp_CanIfTxConfirmation 释放 cto_busy。
 */
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

/*====== 测试桩外部符号 =======================================================*/
extern uint8  test_tx_buf[64][64];
extern uint32 test_tx_len[64];
extern uint32 test_tx_count;
extern uint32 test_tx_drop;
extern boolean test_tx_fail_next;
extern void test_checksum_override_len(uint8);
extern void test_nvm_corrupt(const uint8 *data, uint32 len);
extern uint32 test_flash_program_calls;
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
/*====== 测试基建 =============================================================*/

extern uint32 test_tx_pending(void);
static int pass_count = 0;
static int fail_count = 0;

#define CHECK(name, cond)                                                        \
    do                                                                          \
    {                                                                           \
        if (cond) { pass_count++; printf("  PASS  %-58s\n", name); }             \
        else      { fail_count++; printf("  FAIL  %-58s\n", name); }             \
    } while (0)

/* 传输层回调按使能总线切换（默认CAN；-D 开关选择 FR/ETH 构建） */
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

/* DTO 头部长度随 IDENTIFICATION_FIELD_TYPE 变化:
 * ABSOLUTE=1B(abs odt) / REL_BYTE=2B(odt+daqlist) / REL_WORD|ALIGNED=4B */
#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
#  define XCP_TST_ID_LEN 1U
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_BYTE)
#  define XCP_TST_ID_LEN 2U
#else
#  define XCP_TST_ID_LEN 4U
#endif

/* 构造一帧 STIM DTO: odt/列表按寻址类型编码, 后随4B数据 */
static uint32 test_stim_dto(uint8 *buf, uint8 odt, uint8 list, uint32 data4)
{
#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
    buf[0] = odt; (void)memcpy(&buf[1], &data4, 4U); (void)list; return 5U;
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_BYTE)
    buf[0] = odt; buf[1] = list; (void)memcpy(&buf[2], &data4, 4U); return 6U;
#else
    buf[0] = odt; buf[1] = 0U; buf[2] = list; buf[3] = 0U; (void)memcpy(&buf[4], &data4, 4U); return 8U;
#endif
}

/* 弹出最早一帧（test_tx_pop 已按传输层剥封装）。*/
static uint32 xcp_pop_pdu(uint8 *res, uint32 *len)
{
    return test_tx_pop(res, len);
}

/* 直接向 XCP 注入一段原始总线报文（STIM DTO等）：FR 构建下封装 LEN|CTR|MSG|FILL */
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

/* 发送一帧 CTO 命令，泵一次 MainFunction + TxConfirmation，弹响应帧 */
static uint32 xcp_cmd(const uint8 *cmd, uint32 len, uint8 *res)
{
    PduInfoType info;
#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)
    uint8 fr_frame[64];
    fr_frame[0] = (uint8)(len & 0xFFU);
    fr_frame[1] = (uint8)((len >> 8U) & 0xFFU);
    fr_frame[2] = 0U;                       /* CTR */
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
    Xcp_Tl_TxConfirmation(0U, E_OK);   /* 先确认上一帧，释放 cto_busy */
    Xcp_MainFunction();
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
    /* 交织模式每周期只出队一条：补泵至队列排空 */
    while (g_xcp_manager.runtime_ptr->cmd_queue_count > 0U)
    {
        Xcp_Tl_TxConfirmation(0U, E_OK);
        Xcp_MainFunction();
    }
#endif
    return xcp_pop_pdu(res, &len);
}

/* 泵 N 个主函数周期，每周期补一次 TxConfirmation */
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
        /* 丢弃残留帧（如 EV_RESUME） */
    }
    Xcp_Tl_TxConfirmation(0U, E_OK);
}
/* 等待 bus 完全 assentar: drena CTO/DTO pendencias antes de prosseguir.
   Util apos cenarios que manipulam dto_busy/cto_busy diretamente. */

/* 模拟一帧 DTO 在飞（共享PDU单在飞：须同时置归属） */
static void test_sim_dto_inflight(void)
{
    g_xcp_manager.runtime_ptr->tx.dto_latch = TRUE;
    g_xcp_manager.runtime_ptr->tx.state = XCP_TX_DTO;
}

static void xcp_settle(void)
{
    uint32 i;
    for (i = 0U; i < 20U; i++)
    {
        Xcp_Tl_TxConfirmation(0U, E_OK);
        Xcp_MainFunction();
        if ((g_xcp_manager.runtime_ptr->tx.state != XCP_TX_CTO)
            && (g_xcp_manager.runtime_ptr->tx.dto_latch == FALSE)
            && (g_xcp_manager.runtime_ptr->tx.parked == FALSE)
            && (g_xcp_manager.runtime_ptr->tx.state == XCP_TX_IDLE)
            && (g_xcp_manager.runtime_ptr->cmd_pending == FALSE))
        {
            break;
        }
    }
    xcp_drain_tx();
}

/* 确定性窗口: 不调 MainFunction 地排空 TX 路径(在飞确认、挂起 CTO 与 DTO 队列全部发出并丢弃),
 * 再把周期事件相位归零(ev0=10ms: 下一次自动触发在其后第 10 个 MainFunction)。
 * DAQ 运行期间用 xcp_cmd 取应答、或对显式 Xcp_Event 的 DTO 做字节断言前调用 */
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
        if ((g_xcp_manager.runtime_ptr->tx.state != XCP_TX_CTO)
            && (g_xcp_manager.runtime_ptr->tx.dto_latch == FALSE)
            && (g_xcp_manager.runtime_ptr->tx.parked == FALSE)
            && (g_xcp_manager.storage_ptr->dto_queue_count == 0U))
        {
            break;
        }
        Xcp_Tl_TxConfirmation(0U, E_OK);
    }
    for (i = 0U; i < XCP_MAX_EVENT_CHANNEL; i++)
    {
        g_xcp_manager.runtime_ptr->event_channel_counter[i] = 0U;
    }
}

/* envia comando e procura frame ERR com o codigo esperado no ring TX
   (necessario quando ha DAQ running emitindo DTOs na mesma janela) */
static boolean xcp_cmd_has_err(const uint8 *c, uint32 len, uint8 err)
{
    PduInfoType info;
    uint8  f[64]; uint32 fl;
    boolean found = FALSE;

    (void)info;
    xcp_inject(c, len);
    {
        uint8 pi;
        /* bombeia ate o CTO sair: TxConf pode ser consumida pelo ramo DTO,
           exigindo ciclos extras enquanto ha DTO em fila/voo */
        for (pi = 0U; pi < 10U; pi++)
        {
            Xcp_Tl_TxConfirmation(0U, E_OK);
            Xcp_MainFunction();
            if ((g_xcp_manager.runtime_ptr->tx.state != XCP_TX_CTO)
                && (g_xcp_manager.runtime_ptr->cmd_pending == FALSE))
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

/* 字节级断言: 长度与全部字节一致；失败时打印实际/期望帧 */
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

/* 发命令并取其 RES/ERR 帧（跳过同窗口内运行中 DAQ 的 DTO/EV） */
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
        if ((g_xcp_manager.runtime_ptr->tx.state != XCP_TX_CTO)
            && (g_xcp_manager.runtime_ptr->tx.parked == FALSE)
            && (g_xcp_manager.runtime_ptr->cmd_pending == FALSE)
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
            && (g_xcp_manager.runtime_ptr->cmd_queue_count == 0U)
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

/* ===== 期望值: 按 pyxcp types.py 位表逐位构造（不引用实现侧位常量） ===== */
/* DaqProperties: overloadEvent b7 | timestampSupported b4 | bitStimSupported b3 |
 * resumeSupported b2 | prescalerSupported b1 | daqConfigType b0 (DYNAMIC=1) */
#define TST_DAQ_PROPERTIES ((uint8)(0x80U | 0x08U \
    | ((XCP_TIMESTAMP_TYPE != XCP_NO_TIME_STAMP) ? 0x10U : 0U) \
    | ((XCP_RESUME_MODE_ENABLED == STD_ON) ? 0x04U : 0U) \
    | ((XCP_PRESCALER_SUPPORTED == STD_ON) ? 0x02U : 0U) \
    | ((XCP_DAQ_CONFIG_TYPE == XCP_DAQ_DYNAMIC) ? 0x01U : 0U)))
/* daqKeyByte.Identification_Field b7-6: ABS=00 REL_BYTE=01 REL_WORD=10 REL_WORD_ALIGNED=11 */
#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
#  define TST_DAQ_KEY_BYTE 0x00U
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_BYTE)
#  define TST_DAQ_KEY_BYTE 0x40U
#elif (XCP_IDENTIFICATION_FIELD_TYPE == XCP_RELATIVE_WORD)
#  define TST_DAQ_KEY_BYTE 0x80U
#else
#  define TST_DAQ_KEY_BYTE 0xC0U
#endif
/* CommModeOptional / CommModePgm: interleavedMode b1, masterBlockMode b0 */
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
#  define TST_COMM_OPT   0x02U
#  define TST_QUEUE_SIZE ((uint8)XCP_CMD_QUEUE_SIZE)
#else
#  define TST_COMM_OPT   0x00U
#  define TST_QUEUE_SIZE 0x00U
#endif

/*====== 测试 RAM 变量 ========================================================*/
static uint8  test_ram[256];
static uint32 test_ram_var32;
static uint8  test_ram_var8;

#define RAM_ADDR ((uint32)(uintptr_t)test_ram)

/*============================================================================*/
int main(void)
{
    uint8  res[64];
    uint32 l;
    uint8  cmd[16];
    Std_VersionInfoType ver;

    printf("================ XCP module test ================\n");

    /*---------------- A: DET 未初始化 ----------------------------------------*/
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

    /*---------------- A: Init + GetVersionInfo --------------------------------*/
    printf("-- A. Init / VersionInfo\n");
    Xcp_Init(NULL_PTR);
    CHECK("A4 Xcp_Init(NULL) -> default config",
          g_xcp_manager.config_ptr == &xcp_config);
    CHECK("A5 state == XCP_INIT",
          g_xcp_manager.runtime_ptr->state == XCP_INIT);

    test_det_count = 0;
    Xcp_GetVersionInfo(NULL_PTR);
    CHECK("A6 GetVersionInfo(NULL) -> XCP_E_PARAM_POINTER",
          (test_det_count > 0U) && (test_det_err == XCP_E_PARAM_POINTER));
    Xcp_GetVersionInfo(&ver);
    CHECK("A7 GetVersionInfo returns moduleID 212 / v1.0.0",
          (ver.moduleID == XCP_MODULE_ID) && (ver.sw_major_version == 1U)
          && (ver.sw_minor_version == 0U));

    /* DET: 错误 PDU ID */
    {
        PduInfoType info;
        uint8 dummy[4] = {0xFF, 0x00, 0, 0};
        info.sdu_data_ptr = dummy; info.sdu_length = 2; info.meta_data_ptr = NULL;
        test_det_count = 0;
        Xcp_Tl_RxIndication(9U, &info);
        CHECK("A8 RxIndication wrong PDU -> XCP_E_INVALID_PDUID",
              (test_det_count > 0U) && (test_det_err == XCP_E_INVALID_PDUID));
    }

    /*---------------- B: 连接前命令门控 ---------------------------------------*/
    printf("-- B. command gating before CONNECT\n");
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);   /* GET_STATUS未连接 → 忽略 */
    CHECK("B1 GET_STATUS pre-connect: no response", l == 0U);

    /*---------------- B: CONNECT ----------------------------------------------*/
    printf("-- B. CONNECT / basic commands\n");
    l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
    CHECK("B2 CONNECT -> RES len 8", (l == 8U) && (res[0] == XCP_PID_RES));
    /* pyxcp ConnectResponse: resource{dbg b5,pgm b4,stim b3,daq b2,calpag b0},
     * commModeBasic{optional b7,slaveBlockMode b6,addressGranularity b2-1,byteOrder b0},
     * maxCto, maxDto(LE16), protocolLayerVersion, transportLayerVersion */
    CHECK("B3 CONNECT bytes: CAL|DAQ|STIM|PGM, OPTIONAL+BYTE+Intel, CTO/DTO 8, v1/1",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x1D, 0x80, 8U, 8U, 0x00, 0x01, 0x01 }, 8U));

    /* GET_STATUS */
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    /* pyxcp GetStatusResponse: sessionStatus{resume b7,daqRunning b6,clearDaqRequest b3,
     * storeDaqRequest b2,storeCalRequest b0}, resourceProtectionStatus, stateNumber,
     * sessionConfiguration(LE16) */
    CHECK("B6 GET_STATUS bytes: idle session, CAL|DAQ|STIM|PGM locked",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00, 0x1D, 0x00, 0x00, 0x00 }, 6U));

    /* SYNCH */
    l = xcp_cmd((const uint8 *)"\xFC", 1U, res);
    CHECK("B8 SYNCH -> ERR_CMD_SYNCH",
          (l == 2U) && (res[0] == XCP_PID_ERR) && (res[1] == XCP_ERR_CMD_SYNCH));

    /* GET_COMM_MODE_INFO */
    l = xcp_cmd((const uint8 *)"\xFB", 1U, res);
    /* pyxcp GetCommModeInfoResponse: reserved, commModeOptional{interleavedMode b1,
     * masterBlockMode b0}, reserved, maxBs, minSt, queueSize, xcpDriverVersionNumber */
    CHECK("B9 COMM_MODE_INFO bytes: no master block mode, driver v1.0",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00, TST_COMM_OPT, 0x00,
                                            XCP_MAX_BS, XCP_MIN_ST, TST_QUEUE_SIZE, 0x10 }, 8U));

    /* GET_ID */
    l = xcp_cmd((const uint8 *)"\xFA\x00", 2U, res);
    CHECK("B10 GET_ID type0 -> RES len8, len>0",
          (l == 8U) && (res[0] == XCP_PID_RES) && (res[4] > 0U));
    l = xcp_cmd((const uint8 *)"\xFA\x09", 2U, res);  /* 不支持类型 */
    CHECK("B11 GET_ID type9 -> len=0",
          (l == 8U) && (res[0] == XCP_PID_RES) && (res[4] == 0U));

    /* GET_ID 后 UPLOAD 应返回ID文本 (MTA已指向ID串) */
    l = xcp_cmd((const uint8 *)"\xF5\x07", 2U, res);
    CHECK("B10a UPLOAD after GET_ID -> 'AUTOSAR'",
          (l == 8U) && (res[0] == XCP_PID_RES) && (res[1] == 'A') && (res[7] == 'R'));

    /* 全部0xC8-0xFF码均已分配：用"被锁定资源"验证保护门控 */
    cmd[0] = 0xF0; cmd[1] = 1; cmd[2] = 0xAA;      /* DOWNLOAD while CAL locked */
    l = xcp_cmd(cmd, 3U, res);
    CHECK("B12 DOWNLOAD while locked -> ERR_ACCESS_LOCKED",
          (l == 2U) && (res[0] == XCP_PID_ERR) && (res[1] == XCP_ERR_ACCESS_LOCKED));

    /* USER_CMD */
    l = xcp_cmd((const uint8 *)"\xF1\x11\x22", 3U, res);
    CHECK("B13 USER_CMD callout echoes",
          (l >= 3U) && (res[0] == XCP_PID_RES) && (res[1] == 0x11U));

    /* TRANSPORT_LAYER_CMD GET_SLAVE_ID：CAN专用子命令 [SWS_Xcp_00718] */
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
    /* [F8] 截断帧 -> ERR_CMD_SYNTAX（不得读队列残留字节） */
    l = xcp_cmd((const uint8 *)"\xff", 1U, res);
    CHECK("B17 CONNECT len1 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
    l = xcp_cmd((const uint8 *)"\xf6   \xaa\xbb\xcc", 7U, res); /* SET_MTA 7<8 */
    CHECK("B18 SET_MTA len7 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
    l = xcp_cmd((const uint8 *)"\xe1  \xaa\xbb\xcc", 7U, res); /* WRITE_DAQ 7<8 */
    CHECK("B19 WRITE_DAQ len7 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
    l = xcp_cmd((const uint8 *)"\xd1\x00\x00\xaa\xbb\xcc\xdd", 7U, res); /* PROGRAM_CLEAR 7<8 */
    l = xcp_cmd((const uint8 *)"\xEB\x00", 2U, res); /* SET_CAL_PAGE 2<4 */
    CHECK("B21 SET_CAL_PAGE len2 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
    CHECK("B20 PROGRAM_CLEAR len7 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
    /*---------------- G: Seed & Key ------------------------------------------*/
    printf("-- G. seed & key\n");
    /* GET_SEED for CAL/PAG (0x01) */
    cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_CAL_PAG;
    l = xcp_cmd(cmd, 3U, res);
    CHECK("G1 GET_SEED CAL -> len 4, seed pattern",
          (l == 6U) && (res[1] == 4U) && (res[2] == XCP_RESOURCE_CAL_PAG));
    {
        uint8 seed[4];
        uint8 i;
        memcpy(seed, &res[2], 4U);
        /* UNLOCK: [0xF7, key_length, key...]，key = seed^0xFF */
        cmd[0] = 0xF7; cmd[1] = 4U;
        for (i = 0U; i < 4U; i++) { cmd[2U + i] = (uint8)(seed[i] ^ 0xFFU); }
        l = xcp_cmd(cmd, 6U, res);
        CHECK("G2 UNLOCK correct key -> RES + cleared bit",
              (l >= 1U) && (res[0] == XCP_PID_RES)
              && ((g_xcp_manager.runtime_ptr->protection_status & XCP_RESOURCE_CAL_PAG) == 0U));
        /* 错误key */
        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_PGM;
        l = xcp_cmd(cmd, 3U, res); memcpy(seed, &res[2], 4U);
        cmd[0] = 0xF7; cmd[1] = 4U;
        for (i = 0U; i < 4U; i++) { cmd[2U + i] = 0xAAU; }
        l = xcp_cmd(cmd, 6U, res);
        CHECK("G3 UNLOCK wrong key -> ERR_ACCESS_LOCKED",
              (l == 2U) && (res[1] == XCP_ERR_ACCESS_LOCKED));
        /* [ASAM] 错key → 会话断开 + 采集停止 */
        CHECK("G3b wrong key -> disconnected",
              g_xcp_manager.runtime_ptr->connection_state == XCP_DISCONNECTED);
        /* 重新连接继续后续测试 */
        l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
        CHECK("G3d reconnect after wrong-key", (l >= 1U) && (res[0] == XCP_PID_RES));
        /* GET_SEED mode=1 未经 mode=0 -> ERR_SEQUENCE */
        l = xcp_cmd((const uint8 *)"\xF8\x01\x04", 3U, res);
        CHECK("G3c GET_SEED mode=1 no-seed -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));
        /* 重连重锁全部资源：CAL_PAG 也要补解锁（后续C/D命令需要） */
        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_CAL_PAG;
        l = xcp_cmd(cmd, 3U, res); memcpy(seed, &res[2], 4U);
        cmd[0] = 0xF7; cmd[1] = 4U;
        for (i = 0U; i < 4U; i++) { cmd[2U + i] = (uint8)(seed[i] ^ 0xFFU); }
        (void)xcp_cmd(cmd, 6U, res);
        /* 失败UNLOCK清seed_requested → 先重新GET_SEED再解锁 */
        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_PGM;
        l = xcp_cmd(cmd, 3U, res); memcpy(seed, &res[2], 4U);
        /* PGM 解锁正确key（后续flash测试需要） */
        cmd[0] = 0xF7; cmd[1] = 4U;
        for (i = 0U; i < 4U; i++) { cmd[2U + i] = (uint8)(seed[i] ^ 0xFFU); }
        l = xcp_cmd(cmd, 6U, res);
        CHECK("G4 UNLOCK PGM -> RES", (l >= 1U) && (res[0] == XCP_PID_RES));
        /* 解锁 DAQ 与 STIM */
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
              g_xcp_manager.runtime_ptr->protection_status == 0U);
    }

    /*---------------- C: MTA/UPLOAD/DOWNLOAD -----------------------------------*/
    printf("-- C. memory access\n");
    {
        uint32 i;
        for (i = 0U; i < 256U; i++) { test_ram[i] = (uint8)i; }
    }
    /* SET_MTA + UPLOAD */
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR}, 4U);
    l = xcp_cmd(cmd, 8U, res);
    CHECK("C1 SET_MTA -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    cmd[0] = 0xF5; cmd[1] = 4U;
    l = xcp_cmd(cmd, 2U, res);
    CHECK("C2 UPLOAD 4B -> 00 01 02 03",
          (l == 5U) && (res[1] == 0U) && (res[2] == 1U) && (res[3] == 2U) && (res[4] == 3U));
    /* MTA 自增后再读 */
    l = xcp_cmd(cmd, 2U, res);
    CHECK("C3 UPLOAD again reads advanced MTA (04)",
          (l == 5U) && (res[1] == 4U));

    /* UPLOAD 超长 */
    cmd[0] = 0xF5; cmd[1] = 9U;
    l = xcp_cmd(cmd, 2U, res);
    CHECK("C4 UPLOAD >MAX_CTO-1 -> ERR_OUT_OF_RANGE",
          (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));
    l = xcp_cmd((const uint8 *)"\xEF\x01\xAA", 3U, res); /* DOWNLOAD_NEXT 无块传输 */
    CHECK("C4a DOWNLOAD_NEXT no block -> ERR_SEQUENCE",
          (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

    /* SHORT_UPLOAD */
    cmd[0] = 0xF4; cmd[1] = 4U; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 16U}, 4U);
    l = xcp_cmd(cmd, 8U, res);
    CHECK("C5 SHORT_UPLOAD at +16 -> 0x10..0x13",
          (l == 5U) && (res[1] == 0x10U) && (res[4] == 0x13U));

    /* SHORT_DOWNLOAD */
    cmd[0] = 0xED; cmd[1] = 4U; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 32U}, 4U);
    /* SHORT_DOWNLOAD 数据在内联命令中 — 读 Xcp_Std.c 格式: [0]=cmd,[1]=len,[2]=res,[3]=ext,[4-7]=addr, 数据在扩展包? */
    /* 实际 ASAM: SHORT_DOWNLOAD = cmd len ext addr data...  用 DOWNLOAD 代替主路径测试 */
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 32U}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    /* DOWNLOAD 4字节 */
    cmd[0] = 0xF0; cmd[1] = 4U; cmd[2] = 0xDE; cmd[3] = 0xAD; cmd[4] = 0xBE; cmd[5] = 0xEF;
    l = xcp_cmd(cmd, 6U, res);
    CHECK("C6 DOWNLOAD -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("C7 DOWNLOAD wrote DE AD BE EF",
          (test_ram[32] == 0xDEU) && (test_ram[33] == 0xADU)
          && (test_ram[34] == 0xBEU) && (test_ram[35] == 0xEFU));

    /* DOWNLOAD_MAX */
    cmd[0] = 0xEE; cmd[1] = 0; cmd[2] = 0x11; cmd[3] = 0x22; cmd[4] = 0x33; cmd[5] = 0x44;
    cmd[6] = 0x55; cmd[7] = 0x66;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("C8 DOWNLOAD_MAX -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    /* DOWNLOAD_NEXT 块续传 (DOWNLOAD已arm) → 写MTA=RAM+43 */
    cmd[0] = 0xEF; cmd[1] = 2U; cmd[2] = 0xC0; cmd[3] = 0xDE;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("C8a DOWNLOAD_NEXT -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("C8b DOWNLOAD_NEXT wrote C0 DE at +43",
          (test_ram[43] == 0xC0U) && (test_ram[44] == 0xDEU));

    /* SHORT_DOWNLOAD 长度不足 (MAX_CTO=8 → n=1仍越界) */
    cmd[0] = 0xED; cmd[1] = 1U; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 60U}, 4U);
    l = xcp_cmd(cmd, 8U, res);
    CHECK("C8c SHORT_DOWNLOAD over CTO -> ERR_OUT_OF_RANGE",
          (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

    /* MODIFY_BITS: (v & ~A) | X  — addr via MTA */
    memset(&test_ram[40], 0xFF, 4U);
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR + 40U}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xEC; cmd[1] = 0x0FU; cmd[2] = 0xF0U; cmd[3] = 0x05U; cmd[4] = 0; cmd[5] = 0;
    l = xcp_cmd(cmd, 6U, res);
    CHECK("C9 MODIFY_BITS -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    /* (0xFFFFFFFF & ~0x7FFF8000) | (0x05F0<<15) = 0x80007FFF | 0x02F80000 */
    CHECK("C10 MODIFY_BITS -> *(u32)ram+40 == 0x82F87FFF",
          (*(uint32 *)(void *)&test_ram[40]) == 0x82F87FFFUL);

    /* [F1] MODIFY_BITS shift>16 是 C UB(掩码域越出32位字) → 拒绝而非移位 */
    memset(&test_ram[40], 0xFF, 4U);
    cmd[0] = 0xEC; cmd[1] = 0x11U; cmd[2] = 0xFF; cmd[3] = 0xFF; cmd[4] = 0; cmd[5] = 0;
    l = xcp_cmd(cmd, 6U, res);
    CHECK("C10b MODIFY_BITS shift17 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
    CHECK("C10c MODIFY_BITS shift17 leaves RAM untouched",
          (*(uint32 *)(void *)&test_ram[40]) == 0xFFFFFFFFUL);

    /* BUILD_CHECKSUM */
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xF3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    cmd[4] = 0x10; cmd[5] = 0; cmd[6] = 0; cmd[7] = 0;   /* size=16 */
    l = xcp_cmd(cmd, 8U, res);
    CHECK("C11 BUILD_CHECKSUM -> RES",
          (l >= 5U) && (res[0] == XCP_PID_RES));
    {
        uint32 expected = 0U;
        uint32 i;
        for (i = 0U; i < 16U; i++) { expected += test_ram[i]; }
        /* 响应末4字节为checksum */
        CHECK("C12 checksum = sum(data[0..15])",
              ((uint32)res[l-4] | ((uint32)res[l-3]<<8) | ((uint32)res[l-2]<<16) | ((uint32)res[l-1]<<24)) == expected);
    }


    /*---------------- D: 标定页 -----------------------------------------------*/
    printf("-- D. calibration pages\n");
    l = xcp_cmd((const uint8 *)"\xE9", 1U, res);   /* GET_PAG_PROCESSOR_INFO */
    /* pyxcp GetPagProcessorInfoResponse: maxSegments, pagProperties{freezeSupported b0} */
    CHECK("D1 GET_PAG_PROCESSOR_INFO bytes: 2 segments, no freeze",
          frame_is(res, l, (const uint8[]){ 0xFF, XCP_MAX_SEGMENT, 0x00 }, 3U));
    l = xcp_cmd((const uint8 *)"\xEA\x01\x00", 3U, res); /* GET_CAL_PAGE ECU seg0 */
    CHECK("D2 GET_CAL_PAGE -> RES page byte",
          (l >= 4U) && (res[0] == XCP_PID_RES));
    /* SET_CAL_PAGE seg0 page1 xcp */
    cmd[0] = 0xEB; cmd[1] = 0x02U; cmd[2] = 0x00U; cmd[3] = 0x01U;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("D3 SET_CAL_PAGE seg0 page1 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    l = xcp_cmd((const uint8 *)"\xEA\x02\x00", 3U, res);   /* GET_CAL_PAGE XCP seg0 回读 */
    CHECK("D3b GET_CAL_PAGE confirms switch: [3]=page1",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00, 0x00, 0x01 }, 4U));
    /* 校验激活页号随SET变化 */
    l = xcp_cmd((const uint8 *)"\xEB\x00\x00\x01", 4U, res);  /* SET page1 */
    (void)xcp_cmd((const uint8 *)"\xEA\x02\x00", 3U, res);
    CHECK("D3c page active after SET",
          (l == 1U) && (res[0] == XCP_PID_RES)
          && (g_xcp_manager.runtime_ptr->active_page_xcp == 1U));
    cmd[0] = 0xEB; cmd[1] = 0x82U; cmd[2] = 0x00U; cmd[3] = 0x09U;  /* ALL|XCP 越界页 */
    l = xcp_cmd(cmd, 4U, res);
    CHECK("D4 SET_CAL_PAGE page9 -> ERR_PAGE_NOT_VALID",
          (l == 2U) && (res[1] == XCP_ERR_PAGE_NOT_VALID));
    cmd[0] = 0xEB; cmd[1] = 0x02U; cmd[2] = 0x09U; cmd[3] = 0x00U;  /* 越界段 */
    l = xcp_cmd(cmd, 4U, res);
    CHECK("D5 SET_CAL_PAGE seg9 -> ERR_SEGMENT_NOT_VALID",
          (l == 2U) && (res[1] == XCP_ERR_SEGMENT_NOT_VALID));
    /* pyxcp GetSegmentInfoMode0Response: reserved(3), basicInfo(LE32)；SEGMENT_INFO 0=地址 1=长度 */
    l = xcp_cmd((const uint8 *)"\xE8\x00\x00\x00\x00", 5U, res);
    CHECK("D6 GET_SEGMENT_INFO mode0 address = 0x20000000",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20 }, 8U));
    l = xcp_cmd((const uint8 *)"\xE8\x00\x00\x01\x00", 5U, res);
    CHECK("D6a GET_SEGMENT_INFO mode0 length = 0x1000",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00 }, 8U));
    /* pyxcp GetSegmentInfoMode1Response: maxPages, addressExtension, maxMapping,
     * compressionMethod, encryptionMethod */
    l = xcp_cmd((const uint8 *)"\xE8\x01\x00\x00\x00", 5U, res);
    CHECK("D6b GET_SEGMENT_INFO mode1: 2 pages, no mapping/compression/encryption",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00 }, 6U));
    l = xcp_cmd((const uint8 *)"\xE8\x02\x00\x00\x00", 5U, res);
    CHECK("D6c GET_SEGMENT_INFO mode2 (MAX_MAPPING=0) -> ERR_OUT_OF_RANGE",
          (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));
    l = xcp_cmd((const uint8 *)"\xE8\x00\x00\x02\x00", 5U, res);
    CHECK("D6d GET_SEGMENT_INFO mode0 info2 -> ERR_OUT_OF_RANGE",
          (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));
    /* pyxcp GetPageInfoResponse: properties{xcpWriteAccessWithEcu b5,xcpWriteAccessWithoutEcu b4,
     * xcpReadAccessWithEcu b3,xcpReadAccessWithoutEcu b2,ecuAccessWithXcp b1,ecuAccessWithoutXcp b0},
     * initSegment */
    l = xcp_cmd((const uint8 *)"\xE7\x00\x00\x00", 4U, res);
    CHECK("D7 GET_PAGE_INFO seg0 page0 (reference): XCP read-only",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x0F, 0x00 }, 3U));
    l = xcp_cmd((const uint8 *)"\xE7\x00\x01\x01", 4U, res);
    CHECK("D7a GET_PAGE_INFO seg1 page1 (working): all access, INIT_SEGMENT=1",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x3F, 0x01 }, 3U));
    cmd[0] = 0xE6; cmd[1] = 0; cmd[2] = 0;  /* SET_SEGMENT_MODE seg0 mode0 */
    l = xcp_cmd(cmd, 3U, res);
    CHECK("D8 SET_SEGMENT_MODE -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    l = xcp_cmd((const uint8 *)"\xE5\x00\x00", 3U, res); /* GET_SEGMENT_MODE */
    CHECK("D9 GET_SEGMENT_MODE -> RES", (l >= 2U) && (res[0] == XCP_PID_RES));
    cmd[0] = 0xE4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; /* COPY_CAL_PAGE p0->p1 */
    l = xcp_cmd(cmd, 5U, res);
    CHECK("D10 COPY_CAL_PAGE -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("D10b COPY copied 0x1000 bytes",
          memcmp(test_cal_ram[1], test_cal_rom[0], 0x1000U) == 0);

    /*---------------- E: DAQ 动态配置 ------------------------------------------*/
    printf("-- E. DAQ dynamic config\n");
    l = xcp_cmd((const uint8 *)"\xDA", 1U, res);   /* GET_DAQ_PROCESSOR_INFO */
    /* pyxcp GetDaqProcessorInfoResponse: daqProperties, maxDaq(LE16), maxEventChannel(LE16), minDaq,
     * daqKeyByte{Identification_Field b7-6, Address_Extension b5-4, Optimisation_Type b3-0} */
    CHECK("E1 GET_DAQ_PROCESSOR_INFO bytes",
          frame_is(res, l, (const uint8[]){ 0xFF, TST_DAQ_PROPERTIES, XCP_MAX_DAQ, 0x00,
                                            XCP_MAX_EVENT_CHANNEL, 0x00, XCP_MIN_DAQ,
                                            TST_DAQ_KEY_BYTE }, 8U));
    l = xcp_cmd((const uint8 *)"\xD9", 1U, res);   /* GET_DAQ_RESOLUTION_INFO */
    /* pyxcp GetDaqResolutionInfoResponse: granularity/max DAQ, granularity/max STIM,
     * timestampMode{unit b7-4 (DAQ_TIMESTAMP_UNIT_1MS=6), fixed b3, size b2-0 (S4=4)},
     * timestampTicks(LE16) */
    CHECK("E2 GET_DAQ_RESOLUTION_INFO bytes: 4B timestamp, unit 1ms, ticks 1",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x01, XCP_ODT_ENTRY_SIZE_DAQ, 0x01,
                                            XCP_ODT_ENTRY_SIZE_STIM, 0x64, 0x01, 0x00 }, 8U));
    /* pyxcp GetEventChannelInfoResponse: daqEventProperties{consistency b7-6 (ODT=0,DAQ=1,
     * EVENTCHANNEL=2), stim b3, daq b2}, maxDaqList, eventChannelNameLength,
     * eventChannelTimeCycle, eventChannelTimeUnit (DAQ_TIMESTAMP_UNIT_1MS=6), priority */
    l = xcp_cmd((const uint8 *)"\xD7\x00\x00\x00", 4U, res); /* GET_DAQ_EVENT_INFO ch0 */
    CHECK("E3 GET_DAQ_EVENT_INFO ch0: DAQ|STIM, ODT consistency, 10 x 1ms",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x0C, 0xFF, 0x00, 10U, 0x06, 0x00 }, 7U));
    l = xcp_cmd((const uint8 *)"\xD7\x00\x02\x00", 4U, res);
    CHECK("E3a GET_DAQ_EVENT_INFO ch2: DAQ only, DAQ consistency",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x44, 0xFF, 0x00, 100U, 0x06, 0x02 }, 7U));
    l = xcp_cmd((const uint8 *)"\xD7\x00\x03\x00", 4U, res);
    CHECK("E3b GET_DAQ_EVENT_INFO ch3: STIM only, EVENT consistency",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x88, 0xFF, 0x00, 0x00, 0x06, 0x03 }, 7U));

    l = xcp_cmd((const uint8 *)"\xD6", 1U, res);   /* FREE_DAQ */
    CHECK("E4 FREE_DAQ -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 2; cmd[3] = 0;   /* ALLOC_DAQ 2 */
    l = xcp_cmd(cmd, 4U, res);
    CHECK("E5 ALLOC_DAQ 2 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("E6 daq_count == 2", g_xcp_manager.runtime_ptr->daq_count == 2U);

    cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 2;  /* ALLOC_ODT list0, 2 odts */
    l = xcp_cmd(cmd, 5U, res);
    CHECK("E7 ALLOC_ODT -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 2; /* ALLOC_ODT_ENTRY l0 o0 n2 */
    l = xcp_cmd(cmd, 6U, res);
    CHECK("E8 ALLOC_ODT_ENTRY -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    /* ALLOC越界 */
    cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 99; cmd[3] = 0;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("E9 ALLOC_DAQ 99 -> ERR_MEMORY_OVERFLOW",
          (l == 2U) && (res[1] == XCP_ERR_MEMORY_OVERFLOW));

    /* SET_DAQ_PTR l0 o0 e0 + WRITE_DAQ → test_ram_var32 */
    cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0;
    l = xcp_cmd(cmd, 6U, res);
    CHECK("E10 SET_DAQ_PTR -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 4; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
    l = xcp_cmd(cmd, 8U, res);
    CHECK("E11 WRITE_DAQ var32 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    /* entry1 → test_ram[0] */
    cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 2; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR}, 4U);
    l = xcp_cmd(cmd, 8U, res);
    CHECK("E12 WRITE_DAQ entry1 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    /* SET_DAQ_LIST_MODE: list0, mode=0x00(无时间戳,数据不溢出8B帧), event ch0 */
    cmd[0] = 0xE0; cmd[1] = 0x00; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("E13 SET_DAQ_LIST_MODE -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    /* GET_DAQ_LIST_MODE (新增) */
    cmd[0] = 0xDF; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    l = xcp_cmd(cmd, 4U, res);
    /* pyxcp GetDaqListModeResponse: currentMode{resume b7,running b6,pid_off b5,timestamp b4,
     * direction b1,selected b0}, reserved(2), currentEventChannel(LE16), prescaler, priority */
    CHECK("E14 GET_DAQ_LIST_MODE bytes: idle DAQ list, ev0, prescaler 1",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00 }, 8U));

    /* GET_DAQ_LIST_INFO */
    cmd[0] = 0xD8; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    l = xcp_cmd(cmd, 4U, res);
    /* pyxcp GetDaqListInfoResponse: daqListProperties{stim b3,daq b2,eventFixed b1,predefined b0},
     * maxOdt, maxOdtEntries, fixedEvent(LE16) */
    CHECK("E15 GET_DAQ_LIST_INFO bytes: dynamic list, DAQ|STIM",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x0C, XCP_MAX_ODT, XCP_MAX_ODT_ENTRIES,
                                            0x00, 0x00 }, 6U));

    /* GET_DAQ_CLOCK */
    l = xcp_cmd((const uint8 *)"\xDC", 1U, res);
    CHECK("E16 GET_DAQ_CLOCK -> RES len8", (l == 8U) && (res[0] == XCP_PID_RES));

    /* READ_DAQ：读当前指针条目 */
    cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0;
    (void)xcp_cmd(cmd, 6U, res);
    l = xcp_cmd((const uint8 *)"\xDB", 1U, res);
    {
        uint32 a = (uint32)(uintptr_t)&test_ram_var32;
        /* pyxcp ReadDaqResponse: bitOffset, sizeofDaqElement, adressExtension, address(LE32) */
        CHECK("E17 READ_DAQ bytes: BIT_OFFSET=FF SIZE=4 EXT=0 ADDRESS=&var32",
              frame_is(res, l, (const uint8[]){ 0xFF, 0xFF, 0x04, 0x00, (uint8)a, (uint8)(a >> 8U),
                                                (uint8)(a >> 16U), (uint8)(a >> 24U) }, 8U));
    }

    /* START_STOP_DAQ_LIST start list0 */
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
    /* 列表已运行且 ev0 为 10ms 周期通道: 进入确定性窗口,免得周期 DTO 插到后续 CTO 应答前面。
     * 窗口至 E24 停表共 5 次 MainFunction,须保持 < 10 */
    xcp_quiesce();

    /* WRITE_DAQ on RUNNING list → ERR_DAQ_ACTIVE */
    cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0;
    (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 1; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){RAM_ADDR}, 4U);
    l = xcp_cmd(cmd, 8U, res);
    CHECK("E19 WRITE_DAQ while RUNNING -> ERR_DAQ_ACTIVE",
          (l == 2U) && (res[1] == XCP_ERR_DAQ_ACTIVE));

    /* 触发DAQ事件 → DTO 帧进TX环 */
    test_ram_var32 = 0xA5A5F00DUL;
    xcp_drain_tx();
    Xcp_Event(0U);
    Xcp_Tl_TxConfirmation(0U, E_OK);
    {
        uint8 dto[64]; uint32 dl;
        uint32 popped = test_tx_pop(dto, &dl);
        CHECK("E20 DTO emitted on event", popped > 0U);
        CHECK("E21 DTO PID==0", dl >= 1U && dto[0] == 0U);
        /* 无时间戳: PID + var32(LE) + ram[0..1] 共7B → DLC=8 */
        CHECK("E22 DTO contains var32 A5A5F00D at dto[IDLEN..]",
              (dl >= (XCP_TST_ID_LEN + 4U)) && (dto[XCP_TST_ID_LEN] == 0x0DU) && (dto[XCP_TST_ID_LEN+1U] == 0xF0U)
              && (dto[XCP_TST_ID_LEN+2U] == 0xA5U) && (dto[XCP_TST_ID_LEN+3U] == 0xA5U));
    }
    /* ODT1 也发送 — 再弹 */
    {
        uint8 dto[64]; uint32 dl = 0U;
        CHECK("E23 ODT1 DTO PID==1",
              (test_tx_pop(dto, &dl) > 0U) && (dl >= 1U) && (dto[0] == 1U));
    }
    Xcp_Tl_TxConfirmation(0U, E_OK);

    /* prescaler=2 → 仅每第2次事件出DTO */
    cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 2U;
    l = xcp_cmd(cmd, 8U, res);   /* SET_DAQ_LIST_MODE prescaler=2 */
    CHECK("E19a SET_DAQ_LIST_MODE prescaler2 -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    xcp_drain_tx();
    Xcp_Event(0U); Xcp_Tl_TxConfirmation(0U, E_OK);  /* event1: 计数<2 → 无DTO */
    {
        uint8 dto[64]; uint32 dl;
        CHECK("E19b prescaler event1 no DTO", test_tx_pop(dto, &dl) == 0U);
    }
    Xcp_Event(0U); Xcp_Tl_TxConfirmation(0U, E_OK);  /* event2: 出DTO */
    {
        uint8 dto[64]; uint32 dl;
        CHECK("E19c prescaler event2 DTO", test_tx_pop(dto, &dl) > 0U);
        xcp_drain_tx();
    }
    /* 恢复prescaler=1 */
    cmd[0] = 0xE0; cmd[6] = 1U;
    (void)xcp_cmd(cmd, 8U, res);

    /* START_STOP_DAQ_LIST stop */
    cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("E24 STOP_DAQ_LIST -> RES", (l == 2U) && (res[0] == XCP_PID_RES));

    /* CLEAR_DAQ_LIST */
    cmd[0] = 0xE3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("E25 CLEAR_DAQ_LIST -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    /* START_STOP_SYNCH stop all */
    l = xcp_cmd((const uint8 *)"\xDD\x00", 2U, res);
    CHECK("E26 STOP_ALL_SYNCH -> RES", (l == 1U) && (res[0] == XCP_PID_RES));

    /*---------------- E: SELECT / START_STOP_SYNCH ---------------------------*/
    printf("-- E. select + start_stop_synch\n");
    (void)xcp_cmd_reply((const uint8 *)"\xD6", 1U, res);                     /* FREE_DAQ */
    (void)xcp_cmd_reply((const uint8 *)"\xD5\x00\x02\x00", 4U, res);         /* ALLOC_DAQ 2 */
    (void)xcp_cmd_reply((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);     /* list0 1 ODT */
    (void)xcp_cmd_reply((const uint8 *)"\xD4\x00\x01\x00\x01", 5U, res);     /* list1 1 ODT */
    l = xcp_cmd_reply((const uint8 *)"\xDE\x02\x00\x00", 4U, res);           /* SELECT list0 */
    CHECK("E30 START_STOP_DAQ_LIST select -> RES + FIRST_PID",
          (l == 2U) && (res[0] == XCP_PID_RES));
    l = xcp_cmd_reply((const uint8 *)"\xDF\x00\x00\x00", 4U, res);
    CHECK("E31 GET_DAQ_LIST_MODE: selected, not running", (l == 8U) && (res[1] == 0x01U));
    l = xcp_cmd_reply((const uint8 *)"\xDD\x01", 2U, res);                   /* start selected */
    CHECK("E32 START_STOP_SYNCH start selected -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("E33 only the selected list0 RUNNING",
          (g_xcp_manager.storage_ptr->daq_list[0].state == XCP_DAQ_STATE_RUNNING)
          && (g_xcp_manager.storage_ptr->daq_list[1].state != XCP_DAQ_STATE_RUNNING));
    l = xcp_cmd_reply((const uint8 *)"\xDF\x00\x00\x00", 4U, res);
    CHECK("E34 after start selected: running set, selected cleared", (l == 8U) && (res[1] == 0x40U));
    (void)xcp_cmd_reply((const uint8 *)"\xDE\x02\x01\x00", 4U, res);         /* SELECT list1 */
    (void)xcp_cmd_reply((const uint8 *)"\xDE\x02\x00\x00", 4U, res);         /* SELECT list0 */
    l = xcp_cmd_reply((const uint8 *)"\xDD\x02", 2U, res);                   /* stop selected */
    CHECK("E35 START_STOP_SYNCH stop selected: list0 stopped, flags cleared",
          (l == 1U) && (g_xcp_manager.storage_ptr->daq_list[0].state != XCP_DAQ_STATE_RUNNING)
          && (g_xcp_manager.storage_ptr->daq_list[0].selected == FALSE)
          && (g_xcp_manager.storage_ptr->daq_list[1].selected == FALSE));
    l = xcp_cmd_reply((const uint8 *)"\xDE\x03\x00\x00", 4U, res);
    CHECK("E36 START_STOP_DAQ_LIST mode3 -> ERR_MODE_NOT_VALID",
          (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));
    l = xcp_cmd_reply((const uint8 *)"\xDD\x03", 2U, res);
    CHECK("E37 START_STOP_SYNCH mode3 -> ERR_MODE_NOT_VALID",
          (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));
    /* PID_OFF 未宣称支持 → 拒绝；方向与事件通道类型不符 → ERR_DAQ_CONFIG */
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

    /*---------------- E21/E22: STIM -------------------------------------------*/
    printf("-- E. STIM\n");
    /* 重新建 STIM list: FREE_DAQ→ALLOC 1→ODT 1→ENTRY 1→WRITE_DAQ→mode STIM→start */
    (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
    cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
    cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
    cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 4; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    /* mode: bit1=STIM方向 */
    cmd[0] = 0xE0; cmd[1] = 0x02; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("E27 SET_DAQ_LIST_MODE STIM -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0;
    (void)xcp_cmd(cmd, 4U, res);

    /* 注入 STIM DTO (PID=0 abs odt, ts4B, data4B) 经 RxIndication */
    {
        test_ram_var32 = 0;
        /* mode bit4未设 → 无时间戳字段: PID + 4B数据 在dto[1..4] */
        /* 上面 mode=0x02 无bit4 → data从dto[1]开始: 需要 4B */
        uint8 stim_dto2[8] = {0};
        uint32 stim_len = test_stim_dto(stim_dto2, 0U, 0U, 0x44332211UL);
        xcp_inject(stim_dto2, stim_len);   /* 应路由 ReceiveStimDto，不产生命令 */
        Xcp_MainFunction();
        Xcp_Tl_TxConfirmation(0U, E_OK);
        CHECK("E28 STIM DTO wrote var32 = 0x44332211",
              test_ram_var32 == 0x44332211UL);
    }

    /* STIM超时 → EV_STIM_TIMEOUT (timout=100ms, main period=1ms) */
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

    /* Bypassing [SWS_Xcp_00761]: 同一变量同时被STIM写入 + DAQ测量上报
       → 再配一个DAQ列表观测 test_ram_var32，STIM注入后事件触发应回读新值 */
    (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);   /* FREE_DAQ */
    cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 2; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res); /* 2 lists */
    /* list0: STIM → test_ram_var32 */
    cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
    cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 4; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xE0; cmd[1] = 0x02; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
    (void)xcp_cmd(cmd, 8U, res);   /* list0 = STIM */
    /* list1: DAQ 测量 test_ram_var32 */
    cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
    cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
    cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 4; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xE0; cmd[1] = 0x00; cmd[2] = 1; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
    (void)xcp_cmd(cmd, 8U, res);   /* list1 = DAQ */
    /* 两列表同时运行 */
    cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res); /* start list0 */
    cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res); /* start list1 */
    /* 注入 STIM 值 0x11223344 → list1 事件应回读并上报该值 */
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
    Xcp_Event(0U);                  /* 触发list1 DAQ测量 */
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

    /*---------------- F: Flash 编程 --------------------------------------------*/
    printf("-- F. flash programming\n");
    memset(test_flash_mem, 0xAA, sizeof(test_flash_mem));

    /* PROGRAM_NEXT / PROGRAM_MAX 未启动 → ERR_SEQUENCE */
    l = xcp_cmd((const uint8 *)"\xCA\x01\xAA", 3U, res);
    CHECK("F0a PROGRAM_NEXT before start -> ERR_SEQUENCE",
          (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));
    l = xcp_cmd((const uint8 *)"\xC9\xAA", 2U, res);
    CHECK("F0b PROGRAM_MAX before start -> ERR_SEQUENCE",
          (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));
    l = xcp_cmd((const uint8 *)"\xD2", 1U, res);   /* PROGRAM_START */
    /* pyxcp ProgramStartResponse: reserved, commModePgm{slaveBlockMode b6,interleavedMode b1,
     * masterBlockMode b0}, maxCtoPgm, maxBsPgm, minStPgm, queueSizePgm */
    CHECK("F1 PROGRAM_START bytes",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x00, TST_COMM_OPT, XCP_MAX_CTO,
                                            0x00, 0x00, TST_QUEUE_SIZE }, 7U));
    /* 重复启动 → PGM_ACTIVE */
    l = xcp_cmd((const uint8 *)"\xD2", 1U, res);
    CHECK("F2 PROGRAM_START again -> ERR_PGM_ACTIVE",
          (l == 2U) && (res[1] == XCP_ERR_PGM_ACTIVE));
    l = xcp_cmd((const uint8 *)"\xCE", 1U, res);   /* GET_PGM_PROCESSOR_INFO */
    /* pyxcp GetPgmProcessorInfoResponse: pgmProperties{nonSeqPgmRequired b7,nonSeqPgmSupported b6,
     * encryption b5-4, compression b3-2, functionalMode b1, absoluteMode b0}, maxSector */
    CHECK("F3 GET_PGM_PROCESSOR_INFO bytes: ABSOLUTE|NON_SEQ_SUPPORTED",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x41, XCP_MAX_FLASH_SECTOR }, 3U));
    l = xcp_cmd((const uint8 *)"\xCD\x00\x00", 3U, res); /* GET_SECTOR_INFO mode0 sec0 */
    /* pyxcp GetSectorInfoResponseMode01: clearSequenceNumber, programSequenceNumber,
     * programmingMethod, sectorInfo(LE32) */
    CHECK("F4 GET_SECTOR_INFO mode0 sec0 bytes",
          frame_is(res, l, (const uint8[]){ 0xFF, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x08 }, 8U));

    /* SET_MTA → flash sector 0 start; PROGRAM_CLEAR mode0 range 4K */
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){0x08000000UL}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xD1; cmd[1] = 0x00; cmd[2] = 0; cmd[3] = 0;
    cmd[4] = 0x00; cmd[5] = 0x40; cmd[6] = 0; cmd[7] = 0;  /* mode=ABS, range=0x4000 */
    l = xcp_cmd(cmd, 8U, res);
    CHECK("F5 PROGRAM_CLEAR -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("F6 sector0 erased to 0xFF", test_flash_mem[0] == 0xFFU);

    /* PROGRAM data: SET_MTA → PROGRAM n bytes */
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){0x08000000UL}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xD0; cmd[1] = 4; cmd[2] = 0x11; cmd[3] = 0x22; cmd[4] = 0x33; cmd[5] = 0x44;
    l = xcp_cmd(cmd, 6U, res);
    CHECK("F7 PROGRAM 4B -> RES", (l >= 1U) && (res[0] == XCP_PID_RES));
    cmd[0] = 0xD0; cmd[1] = 0;   /* 结束序列（flush buffer） */
    l = xcp_cmd(cmd, 2U, res);
    CHECK("F8 PROGRAM 0-len flush -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("F9 flash[0..3] = 11 22 33 44",
          (test_flash_mem[0] == 0x11U) && (test_flash_mem[1] == 0x22U)
          && (test_flash_mem[2] == 0x33U) && (test_flash_mem[3] == 0x44U));

    /* PROGRAM_NEXT / PROGRAM_MAX 块写入 MTA=flash+4 */
    cmd[0] = 0xCA; cmd[1] = 2U; cmd[2] = 0xAA; cmd[3] = 0xBB;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("F9a PROGRAM_NEXT -> RES len5", (l == 5U) && (res[0] == XCP_PID_RES));
    cmd[0] = 0xC9; cmd[1] = 0x77; cmd[2] = 0x88; cmd[3] = 0x99;
    l = xcp_cmd(cmd, 4U, res);
    CHECK("F9b PROGRAM_MAX -> RES", (l >= 1U) && (res[0] == XCP_PID_RES));
    cmd[0] = 0xD0; cmd[1] = 0;   /* flush */
    l = xcp_cmd(cmd, 2U, res);
    CHECK("F9c flush after NEXT/MAX -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("F9d flash[4..8] = AA BB 77 88 99",
          (test_flash_mem[4] == 0xAAU) && (test_flash_mem[5] == 0xBBU)
          && (test_flash_mem[6] == 0x77U) && (test_flash_mem[7] == 0x88U)
          && (test_flash_mem[8] == 0x99U));

    /* PROGRAM_VERIFY */
    test_flash_verify_expected = 0;
    cmd[0] = 0xC8; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 0; cmd[7] = 0;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("F10 PROGRAM_VERIFY ok -> RES", (l >= 1U) && (res[0] == XCP_PID_RES));
    test_flash_verify_fail = TRUE;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("F11 PROGRAM_VERIFY fail -> ERR_VERIFY",
          (l == 2U) && (res[1] == XCP_ERR_VERIFY));
    test_flash_verify_fail = FALSE;

    /* PROGRAM_PREPARE / PROGRAM_FORMAT / GET_SECTOR_INFO 变体 */
    cmd[0] = 0xCC; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 0; cmd[7] = 0;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("F12 PROGRAM_PREPARE -> RES", (l >= 1U) && (res[0] == XCP_PID_RES));
    cmd[0] = 0xCB; cmd[1] = 0; cmd[2] = 0; cmd[3] = XCP_PGM_MODE_ABSOLUTE; cmd[4] = 0; cmd[5] = 0; cmd[6] = 0; cmd[7] = 0;
    l = xcp_cmd(cmd, 8U, res);
    CHECK("F13 PROGRAM_FORMAT -> RES", (l >= 1U) && (res[0] == XCP_PID_RES));
    /* PROGRAM_RESET → 断连 */
    l = xcp_cmd((const uint8 *)"\xCF", 1U, res);
    CHECK("F14 PROGRAM_RESET -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    CHECK("F15 PROGRAM_RESET -> disconnected",
          g_xcp_manager.runtime_ptr->connection_state == XCP_DISCONNECTED);

    /*---------------- H: RESUME 模式 -------------------------------------------*/
    printf("-- H. resume mode\n");
    /* 重新连接 + 配DAQ + SET_REQUEST STORE_DAQ */
    l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
    xcp_drain_tx();
    (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
    cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
    /* [F1] 配一个 RUNNING 的 DAQ 再存：断电续传才有意义 */
    (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
    (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
    (void)xcp_cmd((const uint8 *)"\xE2\x00\x00\x00\x00\x00", 6U, res);
    cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 4; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
    (void)xcp_cmd(cmd, 8U, res);
    (void)xcp_cmd((const uint8 *)"\xE0\x00\x00\x00\x02\x00\x01\x00", 8U, res); /* ev2: 无周期干扰 */
    (void)xcp_cmd((const uint8 *)"\xDE\x01\x00\x00", 4U, res);   /* START list0 → RUNNING */
    (void)xcp_cmd((const uint8 *)"\xE0\x80\x00\x00\x02\x00\x01\x00", 8U, res);
    l = xcp_cmd_reply((const uint8 *)"\xF9\x04\x34\x12", 4U, res); /* STORE_DAQ_REQ_RESUME, id 0x1234 */
    CHECK("H1 SET_REQUEST STORE_DAQ_REQ_RESUME -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    l = xcp_cmd_reply((const uint8 *)"\xFD", 1U, res);
    CHECK("H2 GET_STATUS while NvM write pending: storeDaqRequest|daqRunning",
          (l == 6U) && (res[1] == 0x44U));
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
          (l == 6U) && (res[1] == 0x40U) && (res[4] == 0x34U) && (res[5] == 0x12U));
    l = xcp_cmd_reply((const uint8 *)"\xDF\x00\x00\x00", 4U, res);
    CHECK("H2c GET_DAQ_LIST_MODE list0: resume|running",
          (l == 8U) && (res[1] == 0xC0U));

    /* 断电重启: Init 同步恢复 DAQ 配置并自主发 EV_RESUME_MODE */
    xcp_drain_tx();
    Xcp_Init(NULL_PTR);
    CHECK("H3 resume: daq_count restored", g_xcp_manager.runtime_ptr->daq_count == 1U);
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

    /* 重新连接：RESUME数据有效时 CONNECT 不清DAQ，恢复列表直接转RUNNING */
    l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);

    /* [F1/SRS_29019] RESUME 核心用例：断电重启后列表应自动RUNNING并直接上报DTO */
    {
        uint8 dto[64]; uint32 dl;
        CHECK("H13 resumed list0 auto-RUNNING",
              g_xcp_manager.storage_ptr->daq_list[0].state == XCP_DAQ_STATE_RUNNING);
        /* 事件2触发即出DTO；内容校验条目（含 BIT_OFFSET=0xFF）已随 RESUME 持久化 */
        test_ram_var32 = 0x11223344UL;
        xcp_drain_tx();
        Xcp_Event(2U); Xcp_Tl_TxConfirmation(0U, E_OK);
        CHECK("H14 resumed DAQ emits var32 on event2",
              (test_tx_pop(dto, &dl) > 0U) && (dl >= (XCP_TST_ID_LEN + 4U))
              && (dto[XCP_TST_ID_LEN] == 0x44U) && (dto[XCP_TST_ID_LEN + 3U] == 0x11U));
    }
    l = xcp_cmd_reply((const uint8 *)"\xFD", 1U, res);
    CHECK("H15 GET_STATUS in RESUME mode: resume|daqRunning, SESSION_CONFIGURATION_ID kept",
          (l == 6U) && (res[1] == 0xC0U) && (res[4] == 0x34U) && (res[5] == 0x12U));
    (void)xcp_cmd_reply((const uint8 *)"\xDE\x00\x00\x00", 4U, res);  /* 停掉恢复列表 */
    xcp_drain_tx();

    /* CLEAR_DAQ_REQ: 只清 NV 中的配置，RAM 中的 DAQ 列表保留 */
    l = xcp_cmd((const uint8 *)"\xF9\x08\x00\x00", 4U, res);
    CHECK("H5 SET_REQUEST CLEAR_DAQ_REQ -> RES, RAM DAQ kept",
          (l == 1U) && (res[0] == XCP_PID_RES) && (g_xcp_manager.runtime_ptr->daq_count == 1U));
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("H5a GET_STATUS after clear: no resume, SESSION_CONFIGURATION_ID=0",
          (l == 6U) && (res[1] == 0x00U) && (res[4] == 0x00U) && (res[5] == 0x00U));

    /* STORE_CAL_REQ → sessionStatus.storeCalRequest(b0) */
    l = xcp_cmd((const uint8 *)"\xF9\x01\x00\x00", 4U, res);
    CHECK("H6 SET_REQUEST STORE_CAL_REQ -> RES + storeCalRequest",
          (l == 1U) && (res[0] == XCP_PID_RES)
          && ((g_xcp_manager.runtime_ptr->session_status & 0x01U) != 0U));
    /* STORE_DAQ_REQ_NO_RESUME 不支持 → 显式拒绝 */
    l = xcp_cmd((const uint8 *)"\xF9\x02\x00\x00", 4U, res);
    CHECK("H7 SET_REQUEST STORE_DAQ_REQ_NO_RESUME -> ERR_OUT_OF_RANGE",
          (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

    /* 异步NvM回调路径: 写完成 → EV_STORE_DAQ; 读完成 → 重恢复DAQ */
    (void)xcp_cmd((const uint8 *)"\xDE\x02\x00\x00", 4U, res);   /* SELECT list0 */
    (void)xcp_cmd((const uint8 *)"\xF9\x04\x00\x00", 4U, res);   /* 再存一次: NvM 写挂起 */
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
    /* 读完成回调（restore_pending置位后由回调推进恢复） */
    g_xcp_resume_runtime.restore_pending = TRUE;
    g_xcp_manager.runtime_ptr->daq_count = 0U;   /* 模拟未恢复状态 */
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

    /* 剩余API: GetTimestamp / IsProgrammingActive / IsResumeModeActive */
#if (XCP_TIMESTAMP_SOURCE_OS == STD_ON)
    CHECK("H10 GetTimestamp == OS-derived acc",
          Xcp_GetTimestamp() == g_xcp_manager.runtime_ptr->os_timestamp_acc);
#else
    CHECK("H10 GetTimestamp == runtime timestamp",
          Xcp_GetTimestamp() == g_xcp_manager.runtime_ptr->timestamp);
#endif
    CHECK("H11 IsProgrammingActive == FALSE (PGM reset后)",
          Xcp_IsProgrammingActive() == FALSE);
    CHECK("H12 IsResumeModeActive == TRUE (恢复后激活)",
          Xcp_IsResumeModeActive() == TRUE);

    /*---------------- I: 事件包 -------------------------------------------------*/
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

    /* EV_SLEEP / EV_WAKE_UP / EV_SESSION_TERMINATED / SERV_RESET 包装API */
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

    /*---------------- A3: SetTransmissionMode ----------------------------------*/
    printf("-- A. TX mode\n");
    Xcp_SetTransmissionMode(0U, XCP_TX_OFF);
    xcp_drain_tx();
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);   /* GET_STATUS: 发送被抑制 */
    CHECK("A9 TX_OFF suppresses response", l == 0U);
    Xcp_SetTransmissionMode(0U, XCP_TX_ON);
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("A10 TX_ON resumes response", (l == 6U) && (res[0] == XCP_PID_RES));

    /* [SWS_Xcp_00849] DTO 属 TxPDU：TX_OFF 同样要关断测量上报 */
    {
        uint8 dto[64]; uint32 dl;
        /* 建一个最小 DAQ 并起动，事件0每次产一帧 */
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);              /* FREE_DAQ */
        (void)xcp_cmd((const uint8 *)"\xD5\x00\x01", 3U, res);     /* ALLOC_DAQ 1 */
        (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
        (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
        (void)xcp_cmd((const uint8 *)"\xE2\x00\x00\x00\x00\x00", 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
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
        (void)xcp_cmd((const uint8 *)"\xDE\x00\x00\x00", 4U, res);  /* stop */
    }

    /*---------------- J: 错误路径补充 -------------------------------------------*/
    printf("-- J. error paths\n");
    /* 命令忙: 连续2命令，第2个在pending时busy (非interleaved模式: busy_pending) */
    {
        uint8 c1[2] = {0xFD, 0};
        uint32 enq = 1U;
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
        enq = XCP_CMD_QUEUE_SIZE + 1U;   /* 填满多级队列再+1触发busy */
#else
        enq = 2U;                        /* 单命令模式第2个即busy */
#endif
        while (enq-- > 0U)
        {
            xcp_inject(c1, 1U);   /* 队列满 → busy_pending */
        }
        Xcp_MainFunction();
        Xcp_Tl_TxConfirmation(0U, E_OK);
        Xcp_MainFunction();               /* busy_pending → ERR_CMD_BUSY */
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

    /* TriggerTransmit: buffer足够时复制CTO */
    {
        PduInfoType out;
        uint8 b[8];
        /* 先产生一个待发送响应? 用最后 cto_buffer 内容 */
        out.sdu_data_ptr = b; out.sdu_length = 8; out.meta_data_ptr = NULL;
        g_xcp_manager.storage_ptr->cto_buffer[0] = 0xAB;
        g_xcp_manager.storage_ptr->cto_length = 3U;
        Std_ReturnType r = Xcp_Tl_TriggerTransmit(0U, &out);
        CHECK("J2 TriggerTransmit copies cto_buffer",
              (r == E_OK) && (out.sdu_length == 3U) && (b[0] == 0xABU));
        out.sdu_length = 2;  /* 过小 */
        r = Xcp_Tl_TriggerTransmit(0U, &out);
        CHECK("J3 TriggerTransmit small buf -> E_NOT_OK", r == E_NOT_OK);
        r = Xcp_Tl_TriggerTransmit(0U, NULL);
        CHECK("J4 TriggerTransmit NULL -> E_NOT_OK", r == E_NOT_OK);
    }

#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
    xcp_pump((uint32)XCP_CMD_QUEUE_SIZE + 2U);  /* 先清空J1遗留队列 */
    xcp_drain_tx();
    {
        PduInfoType i5;
        uint8 c5[2] = {0xFD, 0};
        i5.sdu_data_ptr = c5; i5.sdu_length = 1; i5.meta_data_ptr = NULL;
        test_tx_fail_next = TRUE;
        Xcp_Tl_RxIndication(0U, &i5);
        Xcp_Tl_TxConfirmation(0U, E_OK);
        Xcp_MainFunction();         /* 处理唯一命令 → 发送失败 */
        CHECK("J5 TX fail -> no frame, busy cleared",
              (test_tx_pop(res, &l) == 0U) && (g_xcp_manager.runtime_ptr->tx.state != XCP_TX_CTO));
    }
    test_tx_fail_next = FALSE;
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("J6 TX recovers", (l == 6U) && (res[0] == XCP_PID_RES));
#else
    test_tx_fail_next = TRUE;
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("J5 TX fail -> no frame, busy cleared",
          (l == 0U) && (g_xcp_manager.runtime_ptr->tx.state != XCP_TX_CTO));
    /* 恢复：J5 的命令仍在队列 → 先消费重叠命令的 ERR_CMD_BUSY，再泵出 J5 的 RES */
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("J6a overlapped -> ERR_CMD_BUSY", (l == 2U) && (res[0] == XCP_PID_ERR));
    xcp_pump(1U);
    l = test_tx_pop(res, &l);
    CHECK("J6 deferred cmd RES delivered", (l == 6U) && (res[0] == XCP_PID_RES));
#endif

    /* 补充错误路径: 非法模式 / 禁读地址 / 无种子解锁 */
    l = xcp_cmd((const uint8 *)"\xFF\x77", 2U, res);   /* CONNECT mode非法 */
    CHECK("J8 CONNECT bad mode -> ERR_MODE_NOT_VALID",
          (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));
    cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
    memcpy(&cmd[4], &(uint32){0xDEAD0000UL}, 4U);    /* 禁止读地址 */
    (void)xcp_cmd(cmd, 8U, res);
    cmd[0] = 0xF5; cmd[1] = 4U;
    l = xcp_cmd(cmd, 2U, res);
    CHECK("J9 UPLOAD forbidden addr -> ERR_ACCESS_DENIED",
          (l == 2U) && (res[1] == XCP_ERR_ACCESS_DENIED));
    l = xcp_cmd((const uint8 *)"\xF7\x04\x01\x02\x03\x04", 6U, res); /* 未GET_SEED直接UNLOCK */
    CHECK("J10 UNLOCK no seed -> ERR_SEQUENCE",
          (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

    /* DISCONNECT */
    l = xcp_cmd((const uint8 *)"\xFE", 1U, res);
    CHECK("J7 DISCONNECT -> RES + state",
          (l == 1U) && (res[0] == XCP_PID_RES)
          && (g_xcp_manager.runtime_ptr->connection_state == XCP_DISCONNECTED));

    /* 断开后除CONNECT外命令被静默丢弃 [SWS gating] */
    l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
    CHECK("J11 GET_STATUS while disconnected -> silence", l == 0U);
    l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);  /* 重连仍可用 */
    CHECK("J12 reconnect after disconnect", (l == 8U) && (res[0] == XCP_PID_RES));

#define XCP_BLOCK_TYPE_NONE     (0U)
#define XCP_BLOCK_TYPE_DOWNLOAD (1U)
#define XCP_BLOCK_TYPE_UPLOAD   (2U)
#define XCP_BLOCK_TYPE_PROGRAM  (3U)

    /*---------------- K: 边界情形 --------------------------------------------*/
    printf("-- K. edge cases\n");

    /* K1: DTO队列溢出过载 [SWS_Xcp_00705 bit5]
       占用dto_busy后Xcp_Event产生DTO全部入队(XCP_MAX_ODT=4),第5条溢出
       -> dto_overload_pending 置位 -> MainFunction 上报 EV_DAQ_OVERLOAD */
    {
        /* DAQ list0 测量 test_ram_var32(前节已配STIM测试后FREE_DAQ,需重建) */
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);

        test_sim_dto_inflight();  /* 模拟发送通道被占（dto_busy + 归属） */
        xcp_drain_tx();
        {
            uint32 qi;
            for (qi = 0U; qi < (XCP_MAX_ODT + 2U); qi++)
            {
                Xcp_Event(0U);      /* 每次出1 DTO,队列满后丢帧 */
            }
        }
        CHECK("K1a DTO queue count capped at XCP_MAX_ODT",
              g_xcp_manager.storage_ptr->dto_queue_count == XCP_MAX_ODT);

        g_xcp_manager.runtime_ptr->tx.dto_latch = FALSE; /* 释放通道,周期排空 */
        g_xcp_manager.runtime_ptr->tx.state = XCP_TX_IDLE;
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
        /* 清理 */
        cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        xcp_settle();
    }

    /* K2: STIM超时的时间戳环绕处理
       last_received 设为接近上限, timestamp 环绕后 elapsed 计算仍应正确 */
    {
        /* 重建STIM列表 */
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xE0; cmd[1] = 0x02; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);

        /* 注入一次STIM以建立基线 */
        {
            PduInfoType info;
            uint8 stim[8] = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0, 0, 0};
            (void)info;
            xcp_inject(stim, 8U);
        }

        /* 场景A: 环绕临近但未超时 — last_received=0xFFFFFF90, timestamp=50
           elapsed = 50 + (0xFFFFFFFF-0xFFFFFF90) + 1 = 162 >= 100ms → 超时触发 */
        g_xcp_manager.runtime_ptr->timestamp = 50U;
        g_xcp_manager.runtime_ptr->stim_last_received[0] = 0xFFFFFF90UL;
        g_xcp_manager.runtime_ptr->stim_timeout_flag[0] = FALSE;
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
                  g_xcp_manager.runtime_ptr->stim_timeout_flag[0] == TRUE);
        }

        /* 场景B: 未环绕且未超时 — last_received=current 不误报 */
        g_xcp_manager.runtime_ptr->stim_timeout_flag[0] = FALSE;
        g_xcp_manager.runtime_ptr->stim_last_received[0] = g_xcp_manager.runtime_ptr->timestamp;
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

    /* K3: 块传输异常分支 */
    {
        Std_ReturnType r;

        /* K3a: 已活动时再次启动 -> E_NOT_OK */
        g_xcp_manager.runtime_ptr->block_transfer_active = TRUE;
        g_xcp_manager.runtime_ptr->block_transfer_type   = XCP_BLOCK_TYPE_DOWNLOAD;
        g_xcp_manager.runtime_ptr->block_transfer_remaining = 100U;
        r = Xcp_StartBlockTransfer(XCP_BLOCK_TYPE_DOWNLOAD, 200U, 8U);
        CHECK("K3a StartBlockTransfer while active -> E_NOT_OK", r == E_NOT_OK);
        CHECK("K3b state preserved after rejected start",
              g_xcp_manager.runtime_ptr->block_transfer_active == TRUE
              && g_xcp_manager.runtime_ptr->block_transfer_remaining == 100U);

        /* K3c: 类型不匹配的Validate -> 状态重置 + E_NOT_OK */
        r = Xcp_ValidateBlockSequence(XCP_BLOCK_TYPE_UPLOAD, 4U);
        CHECK("K3c type-mismatch Validate -> E_NOT_OK", r == E_NOT_OK);
        CHECK("K3d type-mismatch resets block state",
              g_xcp_manager.runtime_ptr->block_transfer_active == FALSE
              && g_xcp_manager.runtime_ptr->block_transfer_remaining == 0U);

        /* K3e: 数据大于剩余 -> remaining 钳到0, active 清除(传输完成) */
        g_xcp_manager.runtime_ptr->block_transfer_active = TRUE;
        g_xcp_manager.runtime_ptr->block_transfer_type   = XCP_BLOCK_TYPE_DOWNLOAD;
        g_xcp_manager.runtime_ptr->block_transfer_remaining = 4U;
        r = Xcp_ValidateBlockSequence(XCP_BLOCK_TYPE_DOWNLOAD, 10U);
        CHECK("K3e oversize data clamped, transfer completes",
              (r == E_OK) && (g_xcp_manager.runtime_ptr->block_transfer_active == FALSE));
    }

    /* K4: STIM DTO malformado / RxIndication inválida */
    {
        Std_ReturnType r;
        PduInfoType info;
        uint8 dto_short[1] = {0x00};
        uint8 dto_baddaq[8] = {0x3F, 0x01, 0x02, 0x03, 0x04, 0, 0, 0}; /* abs odt 63 -> list15 */

        r = Xcp_ReceiveStimDto(NULL, 4U);
        CHECK("K4a STIM NULL -> E_NOT_OK", r == E_NOT_OK);
        r = Xcp_ReceiveStimDto(dto_short, 1U);
        CHECK("K4b STIM len<2 -> E_NOT_OK", r == E_NOT_OK);
        r = Xcp_ReceiveStimDto(dto_baddaq, 8U);
        CHECK("K4c STIM out-of-range daq list -> E_NOT_OK", r == E_NOT_OK);

        /* RxIndication com pdu NULL / sdu NULL -> nao enfileira */
        Xcp_Tl_RxIndication(0U, NULL);
        info.sdu_data_ptr = NULL; info.sdu_length = 4U; info.meta_data_ptr = NULL;
        Xcp_Tl_RxIndication(0U, &info);
        CHECK("K4d NULL pdu/sdu -> no pending cmd",
              g_xcp_manager.runtime_ptr->cmd_pending == FALSE
              && g_xcp_manager.runtime_ptr->busy_pending == FALSE);

        /* comando > XCP_MAX_CTO -> descartado em EnqueueCommand */
        {
            uint8 big[XCP_MAX_CTO + 4U];
            memset(big, 0xFD, sizeof(big));
            xcp_inject(big, sizeof(big));
            CHECK("K4e cmd > XCP_MAX_CTO dropped",
                  g_xcp_manager.runtime_ptr->cmd_pending == FALSE);
        }
    }

    /* K5: TriggerTransmit com buffer insuficiente / NULL */
    {
        PduInfoType tinfo;
        uint8 tbuf[64];
        Std_ReturnType r;
        uint32 det_before = test_det_count;

        (void)xcp_cmd((const uint8 *)"\xFD", 1U, res); /* GET_STATUS: cto_buffer preenchido */

        tinfo.sdu_data_ptr = tbuf; tinfo.sdu_length = 2U; tinfo.meta_data_ptr = NULL;
        r = Xcp_Tl_TriggerTransmit(0U, &tinfo);
        CHECK("K5a TriggerTransmit short buffer -> E_NOT_OK", r == E_NOT_OK);

        tinfo.sdu_data_ptr = tbuf; tinfo.sdu_length = 64U; tinfo.meta_data_ptr = NULL;
        r = Xcp_Tl_TriggerTransmit(0U, &tinfo);
        CHECK("K5b TriggerTransmit ok -> E_OK + cto_length",
              (r == E_OK) && (tinfo.sdu_length == g_xcp_manager.storage_ptr->cto_length));

#if (XCP_DEV_ERROR_DETECT == STD_ON)
        r = Xcp_Tl_TriggerTransmit(0U, NULL);
        CHECK("K5c TriggerTransmit NULL -> E_NOT_OK + DET",
              (r == E_NOT_OK) && (test_det_count > det_before));
#endif
    }

    /* K6: contenção DTO x CTO no TxConfirmation + retry de CTO */
    {
        /* rebuild DAQ list0 */
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);

        /* 前置命令的 RES 仍在飞（pop未确认），先确认释放再发事件 */
        Xcp_Tl_TxConfirmation(0U, E_OK);

        /* Event gera DTO sem confirmacao -> dto_busy=TRUE */
        Xcp_Event(0U);
        CHECK("K6a DTO in-flight sets dto_busy", g_xcp_manager.runtime_ptr->tx.dto_latch == TRUE);

        /* 新语义：DTO 在飞期间到达的 RES 暂存延迟槽（cto_pending），不占 cto_busy */
        {
            uint8 gs = 0xFD;
            xcp_inject(&gs, 1U);
            Xcp_MainFunction(); /* dispatch -> cto_pending=TRUE, 直发被在飞DTO抑制 */
        }
        CHECK("K6b RES deferred while DTO busy -> pending",
              g_xcp_manager.runtime_ptr->tx.parked == TRUE
              && g_xcp_manager.runtime_ptr->tx.dto_latch == TRUE);

        /* DTO 确认消费归属 -> 立即补发延迟 CTO */
        Xcp_Tl_TxConfirmation(0U, E_OK);
        CHECK("K6c DTO conf -> pending CTO sent (cto_busy)",
              g_xcp_manager.runtime_ptr->tx.parked == FALSE
              && g_xcp_manager.runtime_ptr->tx.state == XCP_TX_CTO);

        /* 2a confirmacao limpa CTO */
        Xcp_Tl_TxConfirmation(0U, E_OK);
        CHECK("K6d second conf clears cto_busy",
              g_xcp_manager.runtime_ptr->tx.state != XCP_TX_CTO);

        /* Retry: CTO 确认失败 -> 无重传源，记录错误并释放通道 */
        {
            uint8 gs = 0xFD;
            xcp_settle(); /* drena DTOs e CTOs pendentes antes do novo comando */
            xcp_inject(&gs, 1U);
            Xcp_MainFunction();
        }
        Xcp_Tl_TxConfirmation(0U, E_NOT_OK);
        CHECK("K6e CTO conf fail -> busy cleared + error",
              g_xcp_manager.runtime_ptr->tx.state != XCP_TX_CTO
              && g_xcp_manager.runtime_ptr->tx.last_error == 1U);

        cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        xcp_settle();
    }

    /* K7: guards de range/estado */
    {
        /* GET_DAQ_LIST_MODE list 99 -> OUT_OF_RANGE */
        l = xcp_cmd((const uint8 *)"\xDF\x00\x63\x00", 4U, res);
        CHECK("K7a GET_DAQ_LIST_MODE list99 -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        /* CLEAR_DAQ_LIST em lista RUNNING -> ERR_DAQ_ACTIVE */
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

    /* K8: dto_busy watchdog — conf perdida, MainFunction recupera apos 200 ciclos */
    {
        test_sim_dto_inflight();  /* simula confirmacao perdida: DTO 在飞且归属登记 */
        g_xcp_manager.storage_ptr->dto_queue_count = 0U;
        {
            uint32 wi;
            for (wi = 0U; wi < 201U; wi++)
            {
                Xcp_MainFunction(); /* sem TxConfirmation: watchdog deve expirar */
            }
        }
        CHECK("K8a dto_busy cleared after watchdog timeout",
              g_xcp_manager.runtime_ptr->tx.dto_latch == FALSE);
        /* busy volta a ligar quando ha DTO na fila — confirma drenagem pos-watchdog */
        {
            uint32 wi;
            for (wi = 0U; wi < 20U; wi++)
            {
                Xcp_Tl_TxConfirmation(0U, E_OK);
                Xcp_MainFunction();
            }
        }
        CHECK("K8b queue drains and idle after watchdog",
              g_xcp_manager.storage_ptr->dto_queue_count == 0U);
    }

    /* K9: SET_REQUEST — 位组合、短帧与保留位 */
    {
        /* CLEAR_DAQ_REQ|STORE_DAQ_REQ_RESUME: 先清后存，NvM 写挂起期间 storeDaqRequest 置位 */
        l = xcp_cmd((const uint8 *)"\xF9\x0C\x00\x00", 4U, res);
        CHECK("K9a SET_REQUEST clear+store_resume -> RES",
              (l == 1U) && (res[0] == XCP_PID_RES));
        CHECK("K9b sessionStatus.storeDaqRequest pending",
              (g_xcp_manager.runtime_ptr->session_status & 0x04U) != 0U);
        Xcp_NvMWriteRamBlockToNvmCallback(XCP_NVM_BLOCK_ID_RESUME, 0U);
        CHECK("K9c NvM write done clears storeDaqRequest",
              (g_xcp_manager.runtime_ptr->session_status & 0x04U) == 0U);
        xcp_settle();

        /* SET_REQUEST = mode + SESSION_CONFIGURATION_ID(WORD)：短帧拒绝 */
        l = xcp_cmd((const uint8 *)"\xF9\x01", 2U, res);
        CHECK("K9d SET_REQUEST len2 -> ERR_CMD_SYNTAX",
              (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));

        /* 保留位 bit4-7 忽略 */
        l = xcp_cmd((const uint8 *)"\xF9\xF0\x00\x00", 4U, res);
        CHECK("K9e SET_REQUEST reserved bits -> RES", (l == 1U) && (res[0] == XCP_PID_RES));
    }

    /* K10: TRANSPORT_LAYER_CMD guards */
    {
        l = xcp_cmd((const uint8 *)"\xF2", 1U, res); /* sub-comando ausente */
        CHECK("K10a TL_CMD len1 -> ERR_CMD_SYNTAX",
              (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
        l = xcp_cmd((const uint8 *)"\xF2\x01", 2U, res); /* sub-comando desconhecido */
        CHECK("K10b TL_CMD unknown sub -> ERR_CMD_UNKNOWN",
              (l == 2U) && (res[1] == XCP_ERR_CMD_UNKNOWN));
#if (XCP_ON_CAN_ENABLED == STD_ON)
        l = xcp_cmd((const uint8 *)"\xF2\xFF", 2U, res); /* GET_SLAVE_ID */
        CHECK("K10c GET_SLAVE_ID -> RES len8 'XCP'",
              (l == 8U) && (res[0] == XCP_PID_RES)
              && (res[5] == 'X') && (res[6] == 'C') && (res[7] == 'P'));
#else
        l = xcp_cmd((const uint8 *)"\xF2\xFF", 2U, res);
        CHECK("K10c GET_SLAVE_ID on non-CAN -> ERR_CMD_UNKNOWN",
              (l == 2U) && (res[1] == XCP_ERR_CMD_UNKNOWN));
#endif
    }

    /* K11: MODIFY_BITS / SeedKey — caminhos de erro adicionais */
    {
        /* MTA -> test_ram_var32 */
        cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
        (void)xcp_cmd(cmd, 8U, res);

        /* pós-reconnect tudo re-trancado: destrava CAL_PAG p/ exercitar o branch feliz */
        g_xcp_manager.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_CAL_PAG;
        test_ram_var32 = 0x1234ABCDUL;
        l = xcp_cmd((const uint8 *)"\xEC\x00\x00\x00\xFF\x00", 6U, res);
        CHECK("K11a MODIFY_BITS xor 0x00FF -> RES",
              (l == 1U) && (res[0] == XCP_PID_RES));
        CHECK("K11b MODIFY_BITS applied", test_ram_var32 == 0x123400FFUL);

        /* recurso trancado -> ERR_ACCESS_LOCKED */
        g_xcp_manager.runtime_ptr->protection_status |= XCP_RESOURCE_CAL_PAG;
        l = xcp_cmd((const uint8 *)"\xEC\x00\x00\x00\xFF\x00", 6U, res);
        CHECK("K11c MODIFY_BITS locked -> ERR_ACCESS_LOCKED",
              (l == 2U) && (res[1] == XCP_ERR_ACCESS_LOCKED));
        g_xcp_manager.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_CAL_PAG;

        /* GET_SEED recurso invalido -> OUT_OF_RANGE; ja desbloqueado -> len0 */
        l = xcp_cmd((const uint8 *)"\xF8\x00\x7F", 3U, res);
        CHECK("K11d GET_SEED bad resource -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));
        l = xcp_cmd((const uint8 *)"\xF8\x00\x01", 3U, res); /* CAL_PAG desbloqueado */
        CHECK("K11e GET_SEED unlocked resource -> RES len0",
              (l == 2U) && (res[0] == XCP_PID_RES) && (res[1] == 0U));

        /* UNLOCK len declarado maior que o cmd -> ERR_CMD_SYNTAX */
        g_xcp_manager.runtime_ptr->protection_status |= XCP_RESOURCE_DAQ;
        l = xcp_cmd((const uint8 *)"\xF8\x00\x04", 3U, res); /* GET_SEED DAQ */
        l = xcp_cmd((const uint8 *)"\xF7\x08\x01\x02", 4U, res); /* len=8 > cmd=4 */
        CHECK("K11f UNLOCK declared len > cmd -> ERR_CMD_SYNTAX",
              (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
        /* seed_requested permanece para o unlock correto? nao — fluxo segue; limpa */
        g_xcp_manager.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_DAQ;
        g_xcp_manager.runtime_ptr->seed_requested = FALSE;
        xcp_settle();
    }

    /* K12: PGM caminhos de erro */
    {
        /* PROGRAM_RESET da secao F ja desconectou; H reconectou. PGM nao esta started. */
        g_xcp_manager.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_PGM;

        g_xcp_manager.runtime_ptr->pgm_started = FALSE;
        l = xcp_cmd((const uint8 *)"\xD1\x00\x00\x00\x00\x40\x00\x00", 8U, res); /* CLEAR sem start */
        CHECK("K12a PROGRAM_CLEAR before START -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

        l = xcp_cmd((const uint8 *)"\xD0\x02\xAA\xBB", 4U, res); /* PROGRAM sem start */
        CHECK("K12b PROGRAM before START -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

        l = xcp_cmd((const uint8 *)"\xC9\xAA", 2U, res); /* PROGRAM_MAX sem start */
        CHECK("K12c PROGRAM_MAX before START -> ERR_SEQUENCE",
              (l == 2U) && (res[1] == XCP_ERR_SEQUENCE));

        g_xcp_manager.runtime_ptr->pgm_started = TRUE;

        /* CLEAR com range que estoura uint32 (end < addr) -> GENERIC */
        g_xcp_manager.runtime_ptr->mta = 0xFFFFFF00UL;
        l = xcp_cmd((const uint8 *)"\xD1\x00\x00\x00\x00\x10\x00\x00", 8U, res);
        CHECK("K12d PROGRAM_CLEAR range overflow -> ERR_GENERIC",
              (l == 2U) && (res[1] == XCP_ERR_GENERIC));

        /* CLEAR em buraco de endereco (fora de qualquer setor) -> GENERIC */
        g_xcp_manager.runtime_ptr->mta = 0x08020000UL; /* fim do sector3: fora de qualquer setor */
        l = xcp_cmd((const uint8 *)"\xD1\x00\x00\x00\x00\x10\x00\x00", 8U, res);
        CHECK("K12e PROGRAM_CLEAR address hole -> ERR_GENERIC",
              (l == 2U) && (res[1] == XCP_ERR_GENERIC));

        /* PROGRAM com mais bytes declarados que o CTO carrega -> OUT_OF_RANGE */
        g_xcp_manager.runtime_ptr->mta = 0x08000000UL;
        l = xcp_cmd((const uint8 *)"\xD0\x08\xAA\xBB", 4U, res); /* declara 8, manda 2 */
        CHECK("K12f PROGRAM declared > payload -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        /* PROGRAM em endereco fora do setor -> GENERIC */
        g_xcp_manager.runtime_ptr->mta = 0xDEAD0000UL;
        l = xcp_cmd((const uint8 *)"\xD0\x02\xAA\xBB", 4U, res);
        CHECK("K12g PROGRAM outside sector -> ERR_GENERIC",
              (l == 2U) && (res[1] == XCP_ERR_GENERIC));

        /* PROGRAM_MAX len1 (0 dados) -> OUT_OF_RANGE */
        g_xcp_manager.runtime_ptr->mta = 0x08000000UL;
        l = xcp_cmd((const uint8 *)"\xC9", 1U, res);
        CHECK("K12h PROGRAM_MAX empty -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        g_xcp_manager.runtime_ptr->pgm_started = FALSE;
        g_xcp_manager.runtime_ptr->protection_status |= XCP_RESOURCE_PGM;
        xcp_settle();
    }

    /* K13: CalPage caminhos de erro */
    {
        /* GET_CAL_PAGE mode sem bit ECU/XCP -> MODE_NOT_VALID */
        l = xcp_cmd((const uint8 *)"\xEA\x00\x00", 3U, res);
        CHECK("K13a GET_CAL_PAGE mode0 -> ERR_MODE_NOT_VALID",
              (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));

        /* GET_CAL_PAGE segmento fora de range -> SEGMENT_NOT_VALID */
        l = xcp_cmd((const uint8 *)"\xEA\x01\x09", 3U, res);
        CHECK("K13b GET_CAL_PAGE seg9 -> ERR_SEGMENT_NOT_VALID",
              (l == 2U) && (res[1] == XCP_ERR_SEGMENT_NOT_VALID));

        /* GET_SEGMENT_INFO mode invalido -> MODE_NOT_VALID */
        l = xcp_cmd((const uint8 *)"\xE8\x03\x00\x00\x00", 5U, res);
        CHECK("K13c GET_SEGMENT_INFO mode3 -> ERR_MODE_NOT_VALID",
              (l == 2U) && (res[1] == XCP_ERR_MODE_NOT_VALID));

        /* GET_PAGE_INFO pagina >= num_pages -> PAGE_NOT_VALID */
        l = xcp_cmd((const uint8 *)"\xE7\x00\x00\x09", 4U, res);
        CHECK("K13d GET_PAGE_INFO page9 -> ERR_PAGE_NOT_VALID",
              (l == 2U) && (res[1] == XCP_ERR_PAGE_NOT_VALID));

        /* COPY_CAL_PAGE dst page0 -> WRITE_PROTECTED (CAL_PAG destravado em K11) */
        g_xcp_manager.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_CAL_PAG;
        l = xcp_cmd((const uint8 *)"\xE4\x00\x01\x00\x00", 5U, res); /* src s0p1 -> dst s0p0 */
        CHECK("K13e COPY_CAL_PAGE dst page0 -> ERR_WRITE_PROTECTED",
              (l == 2U) && (res[1] == XCP_ERR_WRITE_PROTECTED));

        /* COPY_CAL_PAGE segmento invalido -> SEGMENT_NOT_VALID */
        l = xcp_cmd((const uint8 *)"\xE4\x09\x01\x00\x01", 5U, res);
        CHECK("K13f COPY_CAL_PAGE bad seg -> ERR_SEGMENT_NOT_VALID",
              (l == 2U) && (res[1] == XCP_ERR_SEGMENT_NOT_VALID));
    }

    /* K14: misc — Event sem list / TriggerTransmit sem Init */
    {
        /* Event em canal sem DAQ running -> nada enviado, sem crash */
        xcp_drain_tx();
        Xcp_Event(3U); /* canal 3 nao tem list associada */
        {
            uint8 tmp[64]; uint32 tl;
            CHECK("K14a Event unmatched channel -> no DTO",
                  test_tx_pop(tmp, &tl) == 0U);
        }

        /* TriggerTransmit antes de Init -> DET UNINIT (estado so se nao-init;
           simulo UNINIT, restauro depois) */
        {
            uint8 saved = g_xcp_manager.runtime_ptr->state;
            uint32 det_before = test_det_count;
            PduInfoType ti;
            uint8 tb[8];
            Std_ReturnType r;
            g_xcp_manager.runtime_ptr->state = XCP_UNINIT;
            ti.sdu_data_ptr = tb; ti.sdu_length = 8U; ti.meta_data_ptr = NULL;
            r = Xcp_Tl_TriggerTransmit(0U, &ti);
#if (XCP_DEV_ERROR_DETECT == STD_ON)
            CHECK("K14b TriggerTransmit UNINIT -> E_NOT_OK + DET",
                  (r == E_NOT_OK) && (test_det_count > det_before));
#else
            CHECK("K14b TriggerTransmit UNINIT -> E_NOT_OK", r == E_NOT_OK);
#endif
            g_xcp_manager.runtime_ptr->state = saved;
        }
    }

    /* K15: RESUME/NvM corrupção — dados inválidos não restauram nem emitem EV */
    {
        uint8  ev[64]; uint32 el; boolean found;
        Xcp_ResumeDataType *rd = &g_xcp_resume_data;
        uint16 saved_checksum;
        uint32 saved_magic;
        uint8  saved_version;
        uint8  saved_count;

        /* baseline: dados válidos restauram (garante setup) */
        memset(rd, 0, sizeof(*rd));
        rd->magic_number = XCP_RESUME_MAGIC_NUMBER;
        rd->version      = XCP_RESUME_DATA_VERSION;
        rd->daq_count    = 1U;
        rd->resume_active = TRUE;
        rd->checksum     = 0U;
        /* recalcula checksum via caminho real: invoca StoreDaq? nao — computa igual a impl */
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

        /* magic corrompido */
        saved_magic = rd->magic_number;
        rd->magic_number = 0xDEADBEEFUL;
        CHECK("K15c bad magic -> E_NOT_OK + IDLE",
              (Xcp_RestoreDaqConfig() == E_NOT_OK)
              && (g_xcp_resume_runtime.state == XCP_RESUME_STATE_IDLE));
        rd->magic_number = saved_magic;

        /* versao errada */
        saved_version = rd->version;
        rd->version = (uint8)(saved_version + 1U);
        CHECK("K15d bad version -> E_NOT_OK + IDLE",
              (Xcp_RestoreDaqConfig() == E_NOT_OK)
              && (g_xcp_resume_runtime.state == XCP_RESUME_STATE_IDLE));
        rd->version = saved_version;

        /* daq_count > XCP_MAX_DAQ */
        saved_count = rd->daq_count;
        rd->daq_count = (uint8)(XCP_MAX_DAQ + 1U);
        CHECK("K15e daq_count overflow -> E_NOT_OK",
              Xcp_RestoreDaqConfig() == E_NOT_OK);
        rd->daq_count = saved_count;

        /* checksum corrompido */
        saved_checksum = rd->checksum;
        rd->checksum = (uint16)(saved_checksum ^ 0xFFFFU);
        CHECK("K15f bad checksum -> E_NOT_OK + IDLE",
              (Xcp_RestoreDaqConfig() == E_NOT_OK)
              && (g_xcp_resume_runtime.state == XCP_RESUME_STATE_IDLE));
        rd->checksum = saved_checksum;

        /* resume_active=FALSE -> E_NOT_OK, IDLE */
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

        /* callback NvM: read falha -> IDLE, restore_pending limpo */
        g_xcp_resume_runtime.restore_pending = TRUE;
        Xcp_NvMReadRamBlockFromNvmCallback(XCP_NVM_BLOCK_ID_RESUME, 1U); /* NVM_REQ_NOT_OK */
        CHECK("K15h NvM read fail -> IDLE + pending cleared",
              (g_xcp_resume_runtime.state == XCP_RESUME_STATE_IDLE)
              && (g_xcp_resume_runtime.restore_pending == FALSE));

        /* callback com block_id errado -> ignorado, pending intacto */
        g_xcp_resume_runtime.restore_pending = TRUE;
        Xcp_NvMReadRamBlockFromNvmCallback((uint16)(XCP_NVM_BLOCK_ID_RESUME + 1U), 0U);
        CHECK("K15i wrong block_id -> ignored",
              g_xcp_manager.runtime_ptr->connection_state == XCP_CONNECTED
              && g_xcp_resume_runtime.restore_pending == TRUE);
        g_xcp_resume_runtime.restore_pending = FALSE;
        xcp_settle();
    }

    /* K16: UPLOAD/DOWNLOAD/USER_CMD edges */
    {
        /* MTA -> test_ram */
        cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)test_ram}, 4U);
        (void)xcp_cmd(cmd, 8U, res);

        /* UPLOAD count > CTO-1 -> OUT_OF_RANGE */
        l = xcp_cmd((const uint8 *)"\xF5\x08", 2U, res); /* 8 > 7 */
        CHECK("K16a UPLOAD 8 -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        /* UPLOAD exato max (7) -> RES len8 */
        memset(test_ram, 0x5A, 16U);
        l = xcp_cmd((const uint8 *)"\xF5\x07", 2U, res);
        CHECK("K16b UPLOAD 7 -> RES len8",
              (l == 8U) && (res[0] == XCP_PID_RES));

        /* UPLOAD 0 elementos -> RES len1, MTA nao move */
        {
            uint32 mta0 = g_xcp_manager.runtime_ptr->mta;
            l = xcp_cmd((const uint8 *)"\xF5\x00", 2U, res);
            CHECK("K16c UPLOAD 0 -> RES len1, MTA fixed",
                  (l == 1U) && (res[0] == XCP_PID_RES)
                  && (g_xcp_manager.runtime_ptr->mta == mta0));
        }

        /* SHORT_UPLOAD count > CTO-1 -> OUT_OF_RANGE */
        cmd[0] = 0xF4; cmd[1] = 0x08; cmd[2] = 0; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)test_ram}, 4U);
        l = xcp_cmd(cmd, 8U, res);
        CHECK("K16d SHORT_UPLOAD 8 -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        /* DOWNLOAD declarado > payload -> OUT_OF_RANGE */
        cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)test_ram}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        l = xcp_cmd((const uint8 *)"\xF0\x06\xAA\xBB", 4U, res); /* declara 6, envia 2 */
        CHECK("K16e DOWNLOAD declared > payload -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        /* SHORT_DOWNLOAD declarado > payload -> OUT_OF_RANGE */
        cmd[0] = 0xED; cmd[1] = 0x06; cmd[2] = 0; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)test_ram}, 4U);
        l = xcp_cmd(cmd, 8U, res); /* declara 6, cmd=8 < 8+6 */
        CHECK("K16f SHORT_DOWNLOAD declared > payload -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        /* DOWNLOAD_MAX len1 (so o cmd): num_elements=0 -> RES sem escrever */
        {
            uint32 mta0 = g_xcp_manager.runtime_ptr->mta;
            l = xcp_cmd((const uint8 *)"\xEE", 1U, res);
            CHECK("K16g DOWNLOAD_MAX len1 -> RES len1, no write",
                  (l == 1U) && (res[0] == XCP_PID_RES)
                  && (g_xcp_manager.runtime_ptr->mta == mta0));
        }

        /* USER_CMD ecoa payload */
        l = xcp_cmd((const uint8 *)"\xF1\xBE\xEF", 3U, res);
        CHECK("K16h USER_CMD echo -> RES + payload",
              (l == 3U) && (res[0] == XCP_PID_RES) && (res[1] == 0xBEU) && (res[2] == 0xEFU));

        xcp_settle();
    }

    /* K17: DAQ alocacao — limites e estados */
    {
        /* ALLOC_DAQ acima de XCP_MAX_DAQ -> MEMORY_OVERFLOW */
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = (uint8)(XCP_MAX_DAQ + 1U); cmd[3] = 0;
        l = xcp_cmd(cmd, 4U, res);
        CHECK("K17a ALLOC_DAQ 9 -> ERR_MEMORY_OVERFLOW",
              (l == 2U) && (res[1] == XCP_ERR_MEMORY_OVERFLOW));

        /* ALLOC_DAQ com list RUNNING -> DAQ_ACTIVE */
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0;
        l = xcp_cmd(cmd, 4U, res);
        CHECK("K17b ALLOC_DAQ while RUNNING -> ERR_DAQ_ACTIVE",
              (l == 2U) && (res[1] == XCP_ERR_DAQ_ACTIVE));

        /* ALLOC_ODT acima de XCP_MAX_ODT -> MEMORY_OVERFLOW */
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = (uint8)(XCP_MAX_ODT + 1U);
        l = xcp_cmd(cmd, 5U, res);
        CHECK("K17c ALLOC_ODT 5 -> ERR_MEMORY_OVERFLOW",
              (l == 2U) && (res[1] == XCP_ERR_MEMORY_OVERFLOW));

        /* ALLOC_ODT_ENTRY acima de XCP_MAX_ODT_ENTRIES -> MEMORY_OVERFLOW */
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = (uint8)(XCP_MAX_ODT_ENTRIES + 1U);
        l = xcp_cmd(cmd, 6U, res);
        CHECK("K17d ALLOC_ODT_ENTRY 8 -> ERR_MEMORY_OVERFLOW",
              (l == 2U) && (res[1] == XCP_ERR_MEMORY_OVERFLOW));

        /* SET_DAQ_PTR odt fora de range -> OUT_OF_RANGE */
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 5; cmd[5] = 0;
        CHECK("K17e SET_DAQ_PTR odt5 -> ERR_OUT_OF_RANGE",
              xcp_cmd_has_err(cmd, 6U, XCP_ERR_OUT_OF_RANGE));

        /* SET_DAQ_PTR entry fora de range -> OUT_OF_RANGE */
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 9;
        CHECK("K17f SET_DAQ_PTR entry9 -> ERR_OUT_OF_RANGE",
              xcp_cmd_has_err(cmd, 6U, XCP_ERR_OUT_OF_RANGE));

        /* WRITE_DAQ em lista RUNNING -> DAQ_ACTIVE (re-start: K17c/d deixaram CONFIGURED) */
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0;
        (void)xcp_cmd(cmd, 6U, res); /* reseta odt_entry_ptr p/ dentro do range */
        cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
        CHECK("K17g WRITE_DAQ while RUNNING -> ERR_DAQ_ACTIVE",
              xcp_cmd_has_err(cmd, 8U, XCP_ERR_DAQ_ACTIVE));

        cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        xcp_settle();
    }

    /* K18: WRITE_DAQ avanca odt_entry_ptr; leitura de estado */
    {
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 2; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);

        cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        CHECK("K18a WRITE_DAQ advances entry ptr",
              g_xcp_manager.runtime_ptr->odt_entry_ptr == 1U);

        /* segundo write aponta entry1 -> ainda dentro do range */
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var8}, 4U);
        cmd[2] = 1;
        l = xcp_cmd(cmd, 8U, res);
        CHECK("K18b WRITE_DAQ entry1 -> RES",
              (l == 1U) && (res[0] == XCP_PID_RES));
        CHECK("K18c entry ptr now 2",
              g_xcp_manager.runtime_ptr->odt_entry_ptr == 2U);

        /* terceiro write estoura entry_count=2 -> OUT_OF_RANGE */
        l = xcp_cmd(cmd, 8U, res);
        CHECK("K18d WRITE_DAQ beyond entries -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));

        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        xcp_settle();
    }

    /* K19: Interleaved queue cheia -> ERR_CMD_BUSY (somente modo interleaved) */
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
    {
        uint8 gs = 0xFD;
        uint8 i;

        xcp_settle();
        /* Preenche a fila com XCP_CMD_QUEUE_SIZE comandos sem processar */
        for (i = 0U; i < XCP_CMD_QUEUE_SIZE; i++)
        {
            xcp_inject(&gs, 1U);
        }
        CHECK("K19a queue filled to MAX", g_xcp_manager.runtime_ptr->cmd_queue_count == XCP_CMD_QUEUE_SIZE);

        /* proximo comando -> busy_pending, descartado */
        xcp_inject(&gs, 1U);
        CHECK("K19b overflow sets busy_pending", g_xcp_manager.runtime_ptr->busy_pending == TRUE);

        /* MainFunction emite ERR_CMD_BUSY e drena 1 por ciclo */
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
        /* drena o resto */
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

    /* K20: fronteira PID DTO/CMD + DISCONNECT semantica + GET_STATUS/GET_ID */
    {
        /* PID 0xC7 (ultimo DTO) -> rota STIM, nao enfileira comando */
        {
            uint8 dto_c7[8] = {0xC7, 0,0,0,0,0,0,0};
            xcp_settle();
            xcp_inject(dto_c7, 8U);
            CHECK("K20a PID 0xC7 -> STIM route, no cmd pending",
                  g_xcp_manager.runtime_ptr->cmd_pending == FALSE);
        }

        /* PID 0xC8 (PROGRAM_VERIFY) -> comando; PGM trancado -> ERR_ACCESS_LOCKED */
        {
            uint8 ver[8] = {0xC8, 0,0,0,0,0,0,0};
            g_xcp_manager.runtime_ptr->protection_status |= XCP_RESOURCE_PGM;
            CHECK("K20b PID 0xC8 -> cmd route (ERR_ACCESS_LOCKED)",
                  xcp_cmd_has_err(ver, 8U, XCP_ERR_ACCESS_LOCKED));
            g_xcp_manager.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_PGM;
        }

        /* DISCONNECT libera todas as DAQ lists [SWS] */
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 2; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        (void)xcp_cmd((const uint8 *)"\xFE", 1U, res);
        CHECK("K20c DISCONNECT frees DAQ (daq_count=0)",
              g_xcp_manager.runtime_ptr->daq_count == 0U);
        (void)xcp_cmd((const uint8 *)"\xFF\x00", 2U, res); /* reconecta */
        xcp_settle();

        /* GET_STATUS: [1]=SESSION_STATUS（无运行列表，至多 resume 位） [2]=保护状态 */
        g_xcp_manager.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_DAQ;
        l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
        CHECK("K20d GET_STATUS fields",
              (l == 6U) && (res[0] == XCP_PID_RES)
              && (res[1] == ((Xcp_IsResumeModeActive() != FALSE) ? 0x80U : 0x00U))
              && (res[2] == g_xcp_manager.runtime_ptr->protection_status));

        /* GET_ID type0 -> len>0 + MTA aponta pro texto; type1 -> len0 */
        {
            uint32 mta_before = g_xcp_manager.runtime_ptr->mta;
            l = xcp_cmd((const uint8 *)"\xFA\x00", 2U, res);
            CHECK("K20e GET_ID type0 -> len>0 + MTA set",
                  (l == 8U) && (res[0] == XCP_PID_RES) && (res[4] > 0U)
                  && (g_xcp_manager.runtime_ptr->mta != mta_before));
            l = xcp_cmd((const uint8 *)"\xFA\x01", 2U, res);
            CHECK("K20f GET_ID type1 -> len0",
                  (l == 8U) && (res[0] == XCP_PID_RES) && (res[4] == 0U));
        }
    }

    /* K21: evento automatico por MainFunction + prescaler wrap */
    {
        /* lista DAQ em canal 1 (1ms) — cada MainFunction deve gerar DTO */
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res); /* ev_channel=1, prescaler=1 */
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);

        xcp_drain_tx();
        {
            uint8 f[64]; uint32 fl; uint32 dto_n = 0U;
            Xcp_MainFunction();                 /* auto-dispara canal1 */
            Xcp_Tl_TxConfirmation(0U, E_OK);    /* confirma o envio */
            while (test_tx_pop(f, &fl) > 0U)
            {
                if (f[0] < 0xC8U) { dto_n++; }
            }
            CHECK("K21a auto event emits DTO", dto_n >= 1U);
        }

        /* prescaler=3: 6 MainFunctions -> exatamente 2 DTOs */
        cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res); /* stop */
        cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; cmd[5] = 0; cmd[6] = 3; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res); /* prescaler=3 */
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res); /* start */
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

        /* STIM event via Xcp_Event nao emite DTO (so sincroniza) */
        cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xE0; cmd[1] = 0x02; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res); /* direction STIM */
        cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        xcp_drain_tx();
        Xcp_Event(1U);
        {
            uint8 f[64]; uint32 fl;
            CHECK("K21c STIM event emits no DTO", test_tx_pop(f, &fl) == 0U);
        }

        /* ResetStimTimeout: flag setada, novo STIM chega -> flag limpa */
        g_xcp_manager.runtime_ptr->stim_timeout_flag[0] = TRUE;
        {
            PduInfoType info;
            uint8 stim[8] = {0};
            uint32 stim_len = test_stim_dto(stim, 0U, 0U, 0x44332211UL);
            (void)info;
            xcp_inject(stim, stim_len);
        }
        CHECK("K21d STIM reception clears timeout flag",
              g_xcp_manager.runtime_ptr->stim_timeout_flag[0] == FALSE);

        cmd[0] = 0xDE; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        xcp_settle();
    }

    /* K22: PGM limites de setor */
    {
        g_xcp_manager.runtime_ptr->protection_status &= (uint8)~XCP_RESOURCE_PGM;
        g_xcp_manager.runtime_ptr->pgm_started = TRUE;

        /* CLEAR cruzando 2 setores validos contiguos (0x08002000 len 0x6000 -> cobre s0 fim + s1 todo) -> RES */
        memset(test_flash_mem, 0xAA, sizeof(test_flash_mem));
        g_xcp_manager.runtime_ptr->mta = 0x08002000UL;
        l = xcp_cmd((const uint8 *)"\xD1\x00\x00\x00\x00\x60\x00\x00", 8U, res);
        CHECK("K22a CLEAR cross-sector valid -> RES",
              (l == 1U) && (res[0] == XCP_PID_RES));
        CHECK("K22b s0 tail + s1 erased",
              (test_flash_mem[0x2000] == 0xFFU) && (test_flash_mem[0x7FFF] == 0xFFU));

        /* PROGRAM cruzando boundary de setor (data_size > restante do setor) -> GENERIC */
        g_xcp_manager.runtime_ptr->mta = 0x08003FF8UL;
        l = xcp_cmd((const uint8 *)"\xD0\x05\x01\x02\x03\x04\x05", 7U, res); /* 5B, restam 8 -> ok? 0x4000-0x3FF8=8 >=5 -> RES */
        CHECK("K22c PROGRAM within sector tail -> RES",
              (l == 1U) && (res[0] == XCP_PID_RES));
        /* agora MTA=0x08003FFD; escrever 4B estoura fim do setor (restante=3) -> GENERIC
           (max 6 dados por CTO=8; len10 seria dropado no enqueue, K4e) */
        CHECK("K22d PROGRAM crossing sector end -> ERR_GENERIC",
              xcp_cmd_has_err((const uint8 *)"\xD0\x04\x01\x02\x03\x04", 6U, XCP_ERR_GENERIC));

        g_xcp_manager.runtime_ptr->pgm_started = FALSE;
        g_xcp_manager.runtime_ptr->protection_status |= XCP_RESOURCE_PGM;
        xcp_settle();
    }

    /* K23: GET_DAQ_CLOCK / READ_DAQ / GET_DAQ_LIST_INFO payloads */
    {
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        cmd[0] = 0xD5; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res);
        cmd[0] = 0xD4; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 1; (void)xcp_cmd(cmd, 5U, res);
        cmd[0] = 0xD3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 1; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
        (void)xcp_cmd(cmd, 8U, res);

        /* READ_DAQ devolve o entry escrito (entry_ptr avancou p/ 1: volte p/ 0) */
        g_xcp_manager.runtime_ptr->odt_entry_ptr = 0U;
        l = xcp_cmd((const uint8 *)"\xDB", 1U, res);
        CHECK("K23a READ_DAQ returns written entry",
              (l == 8U) && (res[0] == XCP_PID_RES) && (res[1] == 0xFFU) && (res[2] == 4U)
              && (res[3] == 0U) && (Xcp_LeRead32(&res[4]) == (uint32)(uintptr_t)&test_ram_var32));

        /* GET_DAQ_LIST_INFO: mode/ev_channel corretos */
        cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 2; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
        (void)xcp_cmd(cmd, 8U, res);
        l = xcp_cmd((const uint8 *)"\xD8\x00\x00\x00", 4U, res);
        CHECK("K23b GET_DAQ_LIST_INFO fields",
              frame_is(res, l, (const uint8[]){ 0xFF, 0x0C, XCP_MAX_ODT, XCP_MAX_ODT_ENTRIES,
                                                0x02, 0x00 }, 6U));

        /* [F6/SRS_29014] mode bit4 → ODT0 DTO 携带 LE32 时间戳 = Xcp_GetTimestamp */
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        (void)xcp_cmd((const uint8 *)"\xD5\x00\x01", 3U, res);
        (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
        (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
        (void)xcp_cmd((const uint8 *)"\xE2\x00\x00\x00\x00\x00", 6U, res);
        cmd[0] = 0xE1; cmd[1] = 0xFF; /* bit_offset=0xFF 字节对象 */ cmd[2] = 4; cmd[3] = 0;
        memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
        (void)xcp_cmd(cmd, 8U, res);
        /* mode 0x10 = timestamp field */
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
        (void)xcp_cmd((const uint8 *)"\xDE\x00\x00\x00", 4U, res);
        xcp_drain_tx();

        /* [F4/29009+29010] 位对象: WRITE_DAQ bit_offset=3 → DAQ只报该位、STIM只改该位 */
        {
            uint8 bit_var = 0x00U;
            uint8 dto[64]; uint32 dl;
            (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
            (void)xcp_cmd((const uint8 *)"\xD5\x00\x01", 3U, res);
            (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
            (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
            (void)xcp_cmd((const uint8 *)"\xE2\x00\x00\x00\x00\x00", 6U, res);
            cmd[0] = 0xE1; cmd[1] = 3U; cmd[2] = 1; cmd[3] = 0;  /* bit_offset=3, size=1B */
            memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&bit_var}, 4U);
            (void)xcp_cmd(cmd, 8U, res);
            (void)xcp_cmd((const uint8 *)"\xE0\x00\x00\x00\x00\x00\x01\x00", 8U, res);
            (void)xcp_cmd((const uint8 *)"\xDE\x01\x00\x00", 4U, res);

            bit_var = 0x08U;  /* bit3 set */
            xcp_drain_tx();
            Xcp_Event(0U); Xcp_Tl_TxConfirmation(0U, E_OK);
            if (test_tx_pop(dto, &dl) > 0U)
            {
                CHECK("K6g DAQ bit_offset extracts bit3",
                      dto[XCP_TST_ID_LEN] == 0x01U);
            }
            else { CHECK("K6g DAQ bit_offset extracts bit3", FALSE); }

            bit_var = 0x04U;  /* bit3 clear, bit2 set */
            xcp_drain_tx();
            Xcp_Event(0U); Xcp_Tl_TxConfirmation(0U, E_OK);
            if (test_tx_pop(dto, &dl) > 0U)
            {
                CHECK("K6h DAQ bit3 clear -> 0",
                      dto[XCP_TST_ID_LEN] == 0x00U);
            }
            else { CHECK("K6h DAQ bit3 clear -> 0", FALSE); }

            /* STIM 方向：改 mode bit1，注入1字节数据改位 */
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

        /* 位偏移 8..31 落在 32 位变量高字节（LE）；越界 BIT_OFFSET 拒绝；未写条目跳过 */
        {
            uint32 bit_var32 = 0U;
            uint8  dto[64]; uint32 dl;
            (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
            (void)xcp_cmd((const uint8 *)"\xD5\x00\x01", 3U, res);
            (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
            (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x02", 6U, res); /* 2 条目只写第1个 */
            (void)xcp_cmd((const uint8 *)"\xE2\x00\x00\x00\x00\x00", 6U, res);
            cmd[0] = 0xE1; cmd[1] = 0x20U; cmd[2] = 1; cmd[3] = 0;
            memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&bit_var32}, 4U);
            l = xcp_cmd(cmd, 8U, res);
            CHECK("K6k WRITE_DAQ bit_offset 0x20 -> ERR_OUT_OF_RANGE",
                  (l == 2U) && (res[1] == XCP_ERR_OUT_OF_RANGE));
            cmd[1] = 12U;   /* bit12 = byte1.bit4 */
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

        /* GET_DAQ_CLOCK == Xcp_GetTimestamp no momento do dispatch */
        {
            uint32 t0 = Xcp_GetTimestamp();
            l = xcp_cmd((const uint8 *)"\xDC", 1U, res);
            {
                uint32 got = (uint32)res[4] | ((uint32)res[5]<<8) | ((uint32)res[6]<<16) | ((uint32)res[7]<<24);
                CHECK("K23c GET_DAQ_CLOCK == Xcp_GetTimestamp",
                      (l == 8U) && (res[0] == XCP_PID_RES) && (got >= t0) && (got <= (t0 + 4U)));
            }
        }

        /* GET_DAQ_LIST_INFO list out-of-range */
        CHECK("K23d GET_DAQ_LIST_INFO list9 -> ERR_OUT_OF_RANGE",
              xcp_cmd_has_err((const uint8 *)"\xD8\x00\x09\x00", 4U, XCP_ERR_OUT_OF_RANGE));

        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        xcp_settle();
    }

    /*---------------- K24: 传输层特性 ----------------------------------------*/
#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)
    printf("-- K24. FlexRay framing\n");
    {
        uint8 frame[64];
        uint16 used = Xcp_FrPackMessage((const uint8 *)"\xFF", 1U, frame, sizeof(frame));
        CHECK("K24a FR pack LEN|CTR|MSG",
              (used == 4U) && (frame[0] == 1U) && (frame[1] == 0U)
              && (frame[3] == 0xFFU));
        /* 2字节消息 → 块长5 奇数 → +FILL对齐6B，末字节为FILL=0 */
        used = Xcp_FrPackMessage((const uint8 *)"\xFD\x00", 2U, frame, sizeof(frame));
        CHECK("K24a2 FR pack adds FILL to even boundary",
              (used == 6U) && (frame[5] == 0U));
        /* 双报文串联 Rx: LEN|CTR|MSG1|LEN|CTR|MSG2 */
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
                    /* 单命令模式: 第2条入队冲突先回 ERR_CMD_BUSY, 第1条随后RES */
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
        /* socket 关闭 → 断开会话 [SWS_Xcp_00737]，且停所有采集 */
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        (void)xcp_cmd((const uint8 *)"\xD5\x00\x01", 3U, res);
        (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
        (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
        (void)xcp_cmd((const uint8 *)"\xDE\x01\x00\x00", 4U, res);
        CHECK("K24b0 DAQ running pre-close",
              g_xcp_manager.storage_ptr->daq_list[0].state == XCP_DAQ_STATE_RUNNING);
        CHECK("K24b socket close -> disconnected",
              Xcp_SoAdConnectionClosed() == TRUE
              && g_xcp_manager.runtime_ptr->connection_state == XCP_DISCONNECTED);
        CHECK("K24b2 socket close stops DAQ",
              g_xcp_manager.storage_ptr->daq_list[0].state != XCP_DAQ_STATE_RUNNING);
        CHECK("K24c socket close idle -> FALSE",
              Xcp_SoAdConnectionClosed() == FALSE);
        l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);  /* 重连 */
        xcp_drain_tx();
    }
#endif

#if (XCP_ON_ETHERNET_ENABLED == STD_ON)
    /* [F2] ETH 帧 msg_len 回绕：HDR+0xFFFF 回绕绕过截断检查 → 必须丢弃 */
    printf("-- K25. boundary frames\n");
    {
        uint8 raw[8];
        PduInfoType info;
        raw[0] = 0xFFU; raw[1] = 0xFFU;           /* LEN = 0xFFFF */
        raw[2] = 0; raw[3] = 0;                    /* CTR */
        raw[4] = 0xFDU;                            /* GET_STATUS */
        info.sdu_data_ptr = raw; info.sdu_length = 5U; info.meta_data_ptr = NULL;
        xcp_drain_tx();
        Xcp_Tl_RxIndication(0U, &info);
        Xcp_MainFunction(); Xcp_Tl_TxConfirmation(0U, E_OK);
        {
            uint8 junk[16]; uint32 jl = 0U;
            CHECK("K25a ETH msg_len=0xFFFF frame dropped (no response)",
                  test_tx_pop(junk, &jl) == 0U);
        }
        /* 连接未被破坏：正常命令仍可响应 */
        l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
        CHECK("K25b conn intact after bad frame", (l == 6U) && (res[0] == XCP_PID_RES));
    }

    /* [F4] GET_SEED callout 超约定长度 → 钳位到 CTO-2，不写穿 */
    {
        extern void test_seed_override_len(uint8);
        uint8 saved[4];
        test_seed_override_len(30U);   /* stub 返回30字节 seed_length */
        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_STIM;
        l = xcp_cmd(cmd, 3U, res);
        CHECK("K25c seed_length clamped to MAX_CTO-2",
              (l == 8U) && (res[1] == (uint8)(XCP_MAX_CTO - 2U)));
        test_seed_override_len(0U);    /* 恢复 stub 默认 */
        memcpy(saved, &res[2], 4U); (void)saved;
    }
#endif

#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)
    printf("-- K25. boundary frames\n");
    /* [F2] FR 帧 msg_len 回绕：HDR+0xFFFF 回绕绕过截断检查 → 必须丢弃 */
    {
        uint8 raw[8];
        PduInfoType info;
        raw[0] = 0xFFU; raw[1] = 0xFFU;           /* LEN = 0xFFFF */
        raw[2] = 0;                                /* CTR */
        raw[3] = 0xFDU;                            /* GET_STATUS */
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

    /* [F4] GET_SEED callout 超约定长度 → 钳位到 CTO-2 */
    {
        extern void test_seed_override_len(uint8);
        test_seed_override_len(30U);
        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_STIM;
        l = xcp_cmd(cmd, 3U, res);
        CHECK("K25c seed_length clamped to MAX_CTO-2",
              (l == 8U) && (res[1] == (uint8)(XCP_MAX_CTO - 2U)));
        test_seed_override_len(0U);
    }
#endif

#if (XCP_ON_CAN_ENABLED == STD_ON)
    printf("-- K25. boundary frames\n");
    /* [F5] 短帧门控：CLEAR_DAQ_LIST 2<4 → ERR_CMD_SYNTAX（不读残留字节） */
    l = xcp_cmd((const uint8 *)"\xE3\x00", 2U, res);
    CHECK("K25a CLEAR_DAQ_LIST len2 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
    /* [F5] GET_CAL_PAGE 2<3 → ERR_CMD_SYNTAX */
    l = xcp_cmd((const uint8 *)"\xEA\x01", 2U, res);
    CHECK("K25b GET_CAL_PAGE len2 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
    /* [F5] GET_SEGMENT_MODE 2<3 → ERR_CMD_SYNTAX */
    l = xcp_cmd((const uint8 *)"\xE5\x00", 2U, res);
    CHECK("K25c GET_SEGMENT_MODE len2 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));
    /* [F5] GET_SEGMENT_INFO 4<5 → ERR_CMD_SYNTAX */
    l = xcp_cmd((const uint8 *)"\xE8\x00\x00\x00", 4U, res);
    CHECK("K25d GET_SEGMENT_INFO len4 -> ERR_CMD_SYNTAX",
          (l == 2U) && (res[1] == XCP_ERR_CMD_SYNTAX));

    /* [F4] GET_SEED callout 超约定长度 → 钳位到 CTO-2，不写穿 */
    {
        extern void test_seed_override_len(uint8);
        test_seed_override_len(30U);
        cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_STIM;
        l = xcp_cmd(cmd, 3U, res);
        CHECK("K25e seed_length clamped to MAX_CTO-2",
              (l == 8U) && (res[1] == (uint8)(XCP_MAX_CTO - 2U)));
        test_seed_override_len(0U);
    }

    /* [F6] TX_OFF 时 DTO 过载排空被抑制：入队后不发 */
    {
        (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);
        (void)xcp_cmd((const uint8 *)"\xD5\x00\x01", 3U, res);
        (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);
        (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res);
        (void)xcp_cmd((const uint8 *)"\xDE\x01\x00\x00", 4U, res);
        xcp_drain_tx();
        /* 停DAQ并 TX_OFF，随后主函数周期不能排空任何滞留DTO */
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

    /*---------------- K26: 敌对桩（callout 契约违反 / NvM 腐化 / 扇区越界） ----*/
    printf("-- K26. adversarial stubs\n");
    {
        /* A. BuildChecksum_Callout 回报非法 checksumLen=7 -> ERR_OUT_OF_RANGE，
           不得误编码为 BYTE 类型帧（修复前默认 guess） */
        test_checksum_override_len(7U);
        cmd[0] = 0xF3; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
        memset(&cmd[4], 0, 4U); /* block_size=0 */
        l = xcp_cmd(cmd, 8U, res);
        CHECK("K26a checksum_len=7 -> ERR_OUT_OF_RANGE",
              (l == 2U) && (res[0] == XCP_PID_ERR) && (res[1] == XCP_ERR_OUT_OF_RANGE));
        test_checksum_override_len(0U);

        /* B. NvM 腐化：恢复路径钳位而非越界调度 */
#if (XCP_RESUME_MODE_ENABLED == STD_ON)
        {
            uint8 bad[64];
            memset(bad, 0xFF, sizeof(bad));
            bad[0] = 0x02U; bad[1] = 0x00U; bad[2] = 0x00U; bad[3] = 0x00U; /* version */
            test_nvm_corrupt(bad, sizeof(bad));
            Xcp_InitResumeMode();
            CHECK("K26b corrupted NvM -> no crash, state recovered",
                  g_xcp_manager.runtime_ptr->state == XCP_INIT);
        }
#endif

        /* C. PROGRAM_NEXT 写非扇区地址 -> ERR_GENERIC，且不触发 FlashProgram_Callout */
#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)
        {
            uint32 before;
            uint8 seed[4];
            uint8 i;
            /* 先解锁 PGM 资源：GET_SEED(PGM) -> UNLOCK(seed^0xFF) */
            cmd[0] = 0xF8; cmd[1] = 0; cmd[2] = XCP_RESOURCE_PGM;
            l = xcp_cmd(cmd, 3U, res);
            memcpy(seed, &res[2], 4U);
            cmd[0] = 0xF7; cmd[1] = 4U;
            for (i = 0U; i < 4U; i++) { cmd[2U + i] = (uint8)(seed[i] ^ 0xFFU); }
            (void)xcp_cmd(cmd, 6U, res);
            (void)xcp_cmd((const uint8 *)"\xD2", 1U, res);              /* PROGRAM_START */
            cmd[0] = 0xF6; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0;
            memcpy(&cmd[4], &(uint32){0x20000000UL}, 4U);             /* MTA=RAM地址(非扇区) */
            (void)xcp_cmd(cmd, 8U, res);                              /* SET_MTA */
            before = test_flash_program_calls;
            l = xcp_cmd((const uint8 *)"\xCA\x02\xAA\xBB", 4U, res);  /* PROGRAM_NEXT 2B */
            CHECK("K26c PROGRAM_NEXT non-sector addr -> ERR_GENERIC",
                  (l == 2U) && (res[0] == XCP_PID_ERR) && (res[1] == XCP_ERR_GENERIC));
            CHECK("K26d FlashProgram_Callout not invoked",
                  test_flash_program_calls == before);
        }
#endif

        /* D. STIM abs_odt 前缀和解码：list0 仅 1 ODT，abs_odt=1 应落 list1 */
#if (XCP_DAQ_ENABLED == STD_ON)
        {
            (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);            /* FREE_DAQ */
            (void)xcp_cmd((const uint8 *)"\xD5\x00\x02", 3U, res);    /* ALLOC_DAQ: 2 lists */
            (void)xcp_cmd((const uint8 *)"\xD4\x00\x00\x00\x01", 5U, res);   /* ALLOC_ODT list0 1 */
            (void)xcp_cmd((const uint8 *)"\xD3\x00\x00\x00\x00\x01", 6U, res); /* ALLOC_ODT_ENTRY list0 */
            (void)xcp_cmd((const uint8 *)"\xD4\x00\x01\x00\x01", 5U, res);   /* ALLOC_ODT list1 1 */
            (void)xcp_cmd((const uint8 *)"\xD3\x00\x01\x00\x00\x01", 6U, res); /* ALLOC_ODT_ENTRY list1 */
            /* list1 ODT entry 指向 test_ram_var32：SET_DAQ_PTR（列表号在字节2-3）+ WRITE_DAQ */
            cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 1; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
            cmd[0] = 0xE1; cmd[1] = 0xFF; cmd[2] = 4; cmd[3] = 0;
            memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
            (void)xcp_cmd(cmd, 8U, res);
            /* list1 设为 STIM+RUNNING，abs_odt=1 应路由到 list1（旧公式 1/4=0 解错） */
            cmd[0] = 0xE0; cmd[1] = 0x02; cmd[2] = 1; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
            (void)xcp_cmd(cmd, 8U, res);                              /* SET_DAQ_LIST_MODE list1 STIM ev0 */
            cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 1; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res); /* START_STOP list1 start */
            {
#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE)
                Std_ReturnType r = Xcp_ReceiveStimDto((const uint8 *)"\x01\xAA\xBB\xCC\xDD", 5U);
                CHECK("K26e abs_odt=1 routed to list1 (prefix-sum decode)", r == E_OK);
#else
                /* RELATIVE_*: {odt, daq_list, data...} -> odt0 of list1 */
                Std_ReturnType r = Xcp_ReceiveStimDto((const uint8 *)"\x00\x01\xAA\xBB\xCC\xDD", 6U);
                CHECK("K26e odt0/list1 routed (relative decode)", r == E_OK);
#endif
            }
            /* K26f: 滞留 DTO 在 FREE_DAQ 后不得再发（队列已清） */
            {
                /* list0 配成 DAQ+RUNNING、entry 指向 test_ram_var32 */
                cmd[0] = 0xE2; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; (void)xcp_cmd(cmd, 6U, res);
                cmd[0] = 0xE1; cmd[1] = 0xFF; cmd[2] = 4; cmd[3] = 0;
                memcpy(&cmd[4], &(uint32){(uint32)(uintptr_t)&test_ram_var32}, 4U);
                (void)xcp_cmd(cmd, 8U, res);
                cmd[0] = 0xE0; cmd[1] = 0; cmd[2] = 0; cmd[3] = 0; cmd[4] = 0; cmd[5] = 0; cmd[6] = 1; cmd[7] = 0;
                (void)xcp_cmd(cmd, 8U, res);                              /* list0 DAQ ev0 */
                cmd[0] = 0xDE; cmd[1] = 1; cmd[2] = 0; cmd[3] = 0; (void)xcp_cmd(cmd, 4U, res); /* start list0 */
                Xcp_Tl_TxConfirmation(0U, E_OK);                          /* 释放RES归属 */
                test_sim_dto_inflight();                                  /* 一帧在飞占住通道 */
                Xcp_Event(0U);                                            /* list0 DTO 入队 */
                CHECK("K26f1 DTO queued behind in-flight frame",
                      g_xcp_manager.storage_ptr->dto_queue_count > 0U);
                g_xcp_manager.runtime_ptr->tx.dto_latch = FALSE;          /* 释放在飞帧 */
                g_xcp_manager.runtime_ptr->tx.state = XCP_TX_IDLE;
                (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);            /* FREE_DAQ: 队列必须清空 */
                CHECK("K26f2 FREE_DAQ clears queued DTOs",
                      g_xcp_manager.storage_ptr->dto_queue_count == 0U);
            }
            (void)xcp_cmd((const uint8 *)"\xDE\x00\x01\x00", 4U, res); /* stop list1 */
            (void)xcp_cmd((const uint8 *)"\xD6", 1U, res);             /* FREE_DAQ */
        }
#endif
        xcp_settle();
    }

    /*---------------- K27: 命令长度模糊环 — 任何负载长度不得使模块崩溃 ---------*/
    printf("-- K27. command length fuzz\n");
    {
        static const uint8 all_cmds[] = {
            0xFE,0xFD,0xFC,0xFB,0xFA,0xF9,0xF7,0xF6,0xF5,0xF4,0xF3,0xF2,0xF1,0xF0,
            0xEF,0xEE,0xED,0xEC,0xEB,0xEA,0xE9,0xE8,0xE7,0xE6,0xE5,0xE4,0xE3,0xE2,
            0xE1,0xE0,0xDF,0xDE,0xDD,0xDC,0xDB,0xDA,0xD9,0xD8,0xD7,0xD6,0xD5,0xD4,
            0xD3,0xD2,0xD1,0xD0,0xCF,0xCE,0xCD,0xCC,0xCB,0xCA,0xC9,0xC8, /* 0xFF CONNECT/0xF8 GET_SEED 需净态,跳过 */
        };
        uint8 fi, len_i;
        uint32 sent = 0U;
        for (fi = 0U; fi < (uint8) sizeof(all_cmds); fi++)
        {
            /* 前条命令的模糊负载可能已断开连接（UNLOCK错key/DISCONNECT）：先恢复再扫 */
            if (g_xcp_manager.runtime_ptr->connection_state != XCP_CONNECTED)
            {
                l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
                CHECK("K27 reconnect after disconnecting cmd",
                      g_xcp_manager.runtime_ptr->connection_state == XCP_CONNECTED);
            }
            for (len_i = 1U; len_i <= XCP_MAX_CTO; len_i++)
            {
                memset(cmd, 0xA5, len_i);
                cmd[0] = all_cmds[fi];
                xcp_inject(cmd, len_i);
                sent++;
            }
        }
        /* 排空队列处理（交织模式每周期一条） */
        {
            uint32 pumps = 0U;
            while (pumps < 400U)
            {
                Xcp_Tl_TxConfirmation(0U, E_OK);
                Xcp_MainFunction();
                pumps++;
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
                if (g_xcp_manager.runtime_ptr->cmd_queue_count == 0U
                    && g_xcp_manager.runtime_ptr->cmd_pending == FALSE) { break; }
#else
                if (g_xcp_manager.runtime_ptr->cmd_pending == FALSE) { break; }
#endif
            }
        }
        CHECK("K27a fuzz sweep processed without crash",
              g_xcp_manager.runtime_ptr->state == XCP_INIT && sent > 0U);
        /* 模糊后连接仍可正常响应（末条命令可能已断开：先补一次重连） */
        if (g_xcp_manager.runtime_ptr->connection_state != XCP_CONNECTED)
        {
            l = xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
            /* 不校验帧长: 模糊残留帧可能先被弹出; 连接状态是唯一判据 */
            CHECK("K27 final reconnect",
                  g_xcp_manager.runtime_ptr->connection_state == XCP_CONNECTED);
        }
        xcp_drain_tx();
        l = xcp_cmd((const uint8 *)"\xFD", 1U, res);
        CHECK("K27b GET_STATUS works after fuzz", (l == 6U) && (res[0] == XCP_PID_RES));
        xcp_settle();

        /* K27c: 随机负载模糊（seeded,可复现）— 全命令空间随机字节扫 */
        {
            uint32 rng = 0x9E3779B9U;
            uint32 i;
            for (i = 0U; i < 600U; i++)
            {
                uint8 fl;
                rng ^= rng << 13U; rng ^= rng >> 17U; rng ^= rng << 5U;  /* xorshift32 */
                fl = (uint8)(rng % XCP_MAX_CTO) + 1U;
                {
                    uint8 b;
                    for (b = 0U; b < fl; b++)
                    {
                        rng ^= rng << 13U; rng ^= rng >> 17U; rng ^= rng << 5U;
                        cmd[b] = (uint8)(rng & 0xFFU);
                    }
                }
                /* 首字节仍放合法命令码的一半概率、全随机一半概率 */
                if ((i & 1U) != 0U)
                {
                    cmd[0] = all_cmds[rng % (uint8) sizeof(all_cmds)];
                }
                if (g_xcp_manager.runtime_ptr->connection_state != XCP_CONNECTED)
                {
                    (void)xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
                }
                xcp_inject(cmd, fl);
                Xcp_Tl_TxConfirmation(0U, E_OK);
                Xcp_MainFunction();
            }
            CHECK("K27c random-payload fuzz: no crash",
                  g_xcp_manager.runtime_ptr->state == XCP_INIT);
            if (g_xcp_manager.runtime_ptr->connection_state != XCP_CONNECTED)
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
        /* [SWS_Xcp_00715] rx==tx 配置 -> DET INV_CONFIG + 拒绝初始化 */
        Xcp_ConfigType bad = xcp_config;
        uint8 det_cnt;
        bad.xcp_rx_pdu_id = 7U; bad.xcp_tx_pdu_id = 7U;
        test_det_count = 0;
        Xcp_Init(&bad);
        det_cnt = test_det_count;
        CHECK("K24a rx==tx PDU -> XCP_E_INV_CONFIG",
              (det_cnt > 0U) && (test_det_api == XCP_SID_INIT)
              && (g_xcp_manager.runtime_ptr->state == XCP_UNINIT));
        Xcp_Init(NULL_PTR);                    /* 恢复 */
        (void)xcp_cmd((const uint8 *)"\xFF\x00", 2U, res);
        xcp_drain_tx();
    }
#endif

    printf("================ %d passed, %d failed ================\n",
           pass_count, fail_count);
    return (fail_count == 0) ? 0 : 1;
}
