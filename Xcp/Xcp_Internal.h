#ifndef XCP_INTERNAL_H
#define XCP_INTERNAL_H

#include "StandardTypes.h"
#include "Xcp_Config.h"
#include "Xcp_Types.h"

#if (XCP_DEV_ERROR_DETECT == STD_ON)
#include "Det.h"
#define XCP_DET_REPORT_ERROR(sid, err) (void)Det_ReportError(XCP_MODULE_ID, 0U, (sid), (err))
#else
#define XCP_DET_REPORT_ERROR(sid, err)
#endif

static inline uint16 Xcp_LeRead16(const uint8 *p)
{
    return (uint16)((uint16)p[0] | ((uint16)p[1] << 8U));
}
static inline uint32 Xcp_LeRead32(const uint8 *p)
{
    return ((uint32)p[0]) | ((uint32)p[1] << 8U) | ((uint32)p[2] << 16U) | ((uint32)p[3] << 24U);
}
static inline void Xcp_LeWrite16(uint8 *p, uint16 v)
{
    p[0] = (uint8)(v & 0xFFU);
    p[1] = (uint8)((v >> 8U) & 0xFFU);
}
static inline void Xcp_LeWrite32(uint8 *p, uint32 v)
{
    p[0] = (uint8)(v & 0xFFU);
    p[1] = (uint8)((v >> 8U) & 0xFFU);
    p[2] = (uint8)((v >> 16U) & 0xFFU);
    p[3] = (uint8)((v >> 24U) & 0xFFU);
}

#if (XCP_DEV_ERROR_DETECT == STD_ON)
#define XCP_TL_CHECK_INIT(sid)                                                        \
    if ((g_xcp_manager.runtime_ptr == NULL_PTR) ||                                   \
        (g_xcp_manager.runtime_ptr->state == XCP_UNINIT))                            \
    {                                                                              \
        XCP_DET_REPORT_ERROR((sid), XCP_E_UNINIT);                                   \
        return;                                                                    \
    }
#define XCP_TL_CHECK_INIT_R(sid)                                                      \
    if ((g_xcp_manager.runtime_ptr == NULL_PTR) ||                                   \
        (g_xcp_manager.runtime_ptr->state == XCP_UNINIT))                            \
    {                                                                              \
        XCP_DET_REPORT_ERROR((sid), XCP_E_UNINIT);                                   \
        return E_NOT_OK;                                                             \
    }

#define XCP_TL_CHECK_RX_PARAMS(sid, pdu_ptr, rx_id, exp_id)                           \
    if ((pdu_ptr) == NULL_PTR)                                                       \
    {                                                                              \
        XCP_DET_REPORT_ERROR((sid), XCP_E_PARAM_POINTER);                            \
        return;                                                                    \
    }                                                                              \
    if ((rx_id) != (exp_id))                                                         \
    {                                                                              \
        XCP_DET_REPORT_ERROR((sid), XCP_E_INVALID_PDUID);                            \
        return;                                                                    \
    }

#define XCP_TL_CHECK_TT_PARAMS(sid, pdu_ptr)                                          \
    if ((pdu_ptr) == NULL_PTR)                                                       \
    {                                                                              \
        XCP_DET_REPORT_ERROR((sid), XCP_E_PARAM_POINTER);                            \
        return E_NOT_OK;                                                             \
    }
#else
#define XCP_TL_CHECK_INIT(sid)   if (g_xcp_manager.runtime_ptr == NULL_PTR) { return; }
#define XCP_TL_CHECK_INIT_R(sid) if (g_xcp_manager.runtime_ptr == NULL_PTR) { return E_NOT_OK; }
#define XCP_TL_CHECK_RX_PARAMS(sid, pdu_ptr, rx_id, exp_id) \
    if ((pdu_ptr) == NULL_PTR) { return; }
#define XCP_TL_CHECK_TT_PARAMS(sid, pdu_ptr) \
    if ((pdu_ptr) == NULL_PTR) { return E_NOT_OK; }
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{

    uint8 cto_buffer[XCP_MAX_CTO];
    uint8 cto_length;

#if (XCP_DAQ_ENABLED == STD_ON)

    Xcp_DaqListType  daq_list[XCP_MAX_DAQ];
    Xcp_OdtType      odt[XCP_MAX_DAQ * XCP_MAX_ODT];
    Xcp_OdtEntryType odt_entry[XCP_MAX_DAQ * XCP_MAX_ODT * XCP_MAX_ODT_ENTRIES];
    uint8  dto_queue[XCP_MAX_ODT][XCP_MAX_DTO];
    uint8  dto_queue_count;
    uint8  dto_queue_head;
    uint8  dto_queue_tail;

#if (XCP_MAX_ODT > 255U)
    #error "XCP_MAX_ODT must be <= 255 for uint8 DTO ring buffer indices"
#endif
#endif
} Xcp_StorageType;

typedef enum
{
    XCP_TX_IDLE = 0,
    XCP_TX_CTO  = 1,
    XCP_TX_DTO  = 2
} Xcp_TxStateType;

typedef struct
{
    volatile Xcp_TxStateType state;
    volatile boolean         parked;
    volatile uint8           parked_len;
    uint8                    parked_data[XCP_MAX_CTO];
    volatile boolean         dto_latch;
    uint8                    last_error;
} Xcp_TxPipe;

typedef struct
{

    Xcp_StateType            state;
    Xcp_ConnectionStateType  connection_state;
    uint8                    session_status;
    Xcp_TransmissionModeType tx_mode;

    uint32 mta;
    uint8  mta_extension;

#if (XCP_DAQ_ENABLED == STD_ON)

    uint16 daq_list_ptr;
    uint8  odt_ptr;
    uint8  odt_entry_ptr;

    Xcp_DaqListType *daq_list;
    uint16           daq_count;

    uint32 event_channel_counter[XCP_MAX_EVENT_CHANNEL];
#endif

    /* 一条 Tx PDU 上的发送管道。state 是总线令牌（同时只有一帧）。
       parked 是等令牌的那一帧 CTO。dto_latch 不是令牌：DTO 确认后若改发
       停放的 CTO，闩保持，看门狗继续计，直到排空、失败确认或超时。 */
    Xcp_TxPipe tx;

    uint32 timestamp;
    uint32 os_timestamp_acc;

    uint8   protection_status;
    uint8   seed_resource;

    uint8   seed[XCP_MAX_KEY_LENGTH];
    uint8   seed_length;
    boolean seed_requested;

#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)

    uint8 active_segment;
    uint8 active_page_ecu;
    uint8 active_page_xcp;
#endif

#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)

    boolean pgm_started;
    uint8   pgm_processor;
    uint32  pgm_address;
    uint32  pgm_size;
#endif

    uint8  cmd_queue_buffer[XCP_MAX_CTO];
    uint8  cmd_queue_length;
    boolean cmd_pending;
    boolean busy_pending;

#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)

    uint8   cmd_queue[XCP_CMD_QUEUE_SIZE][XCP_MAX_CTO];
    uint8   cmd_queue_len[XCP_CMD_QUEUE_SIZE];
    uint8   cmd_queue_head;
    uint8   cmd_queue_tail;
    uint8   cmd_queue_count;
#endif

#if (XCP_STIM_TIMEOUT_ENABLED == STD_ON)

    uint32  stim_last_received[XCP_MAX_DAQ];
    boolean stim_timeout_flag[XCP_MAX_DAQ];
#endif

#if (XCP_BLOCK_TRANSFER_ENABLED == STD_ON)

    uint8   block_sequence_counter;
    uint8   expected_block_sequence;
    boolean block_transfer_active;
    uint32  block_transfer_remaining;
    uint8   block_transfer_type;
#endif
} Xcp_RuntimeType;

typedef struct
{
    const Xcp_ConfigType  *config_ptr;
    Xcp_StorageType       *storage_ptr;
    Xcp_RuntimeType       *runtime_ptr;
} Xcp_ManagerType;

extern Xcp_ManagerType g_xcp_manager;

/* 构造两字节 ERR 帧并返回 2。fr 即调用方收到的 res 缓冲（== cto_buffer）。 */
static inline sint16 Xcp_ResErr(Xcp_ResFrame *fr, uint8 error_code)
{
    fr[0] = XCP_PID_ERR;
    fr[1] = error_code;
    return 2;
}

/* 分发表 guard 列：未连接时仍可接收 CONNECT */
#define XCP_GUARD_NONE                (0U)
#define XCP_GUARD_ALLOW_DISCONNECTED  (1U)

typedef struct
{
    uint8      cmd;
    uint8      min_len;
    uint8      resource;
    uint8      guard;
    Xcp_CmdFn  fn;
} Xcp_CmdEntryType;


void Xcp_SendPositiveResponse(uint8 length);

void Xcp_SendErrorResponse(uint8 error_code);

Std_ReturnType Xcp_SendResponse(const uint8 *res_data, uint8 res_length);

void Xcp_SendPendingCto(void);

uint32 Xcp_GetTimestamp(void);

void Xcp_ProcessCommand(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_EnqueueCommand(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_ProcessCommandQueue(void);

void Xcp_DtoSendOrQueue(const uint8 *dto_data, uint8 dto_length);

void Xcp_DtoSendNext(void);

Std_ReturnType Xcp_Tl_SendCto(const uint8 *data, uint8 length);

Std_ReturnType Xcp_Tl_SendDto(const uint8 *data, uint8 length);

void Xcp_HandleTxConfirmation(Std_ReturnType result);

Std_ReturnType Xcp_HandleTriggerTransmit(PduInfoType *pdu_info_ptr);

void Xcp_HandleRxIndication(const PduInfoType *pdu_info_ptr);

#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)
extern Xcp_SegmentType Xcp_SegmentRuntime[XCP_MAX_SEGMENT];
#endif

#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)

#ifndef XCP_FR_HEADER_LEN
#define XCP_FR_HEADER_LEN (3U)
#endif
uint16 Xcp_FrPackMessage(const uint8 *msg, uint8 msg_len, uint8 *frame, uint16 frame_capacity);
Std_ReturnType Xcp_FrConfigureLPdu(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
