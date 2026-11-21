#ifndef XCP_INTERNAL_H
#define XCP_INTERNAL_H

#include "StandardTypes.h"
#include "Xcp_Config.h"
#include "Xcp_Types.h"

/* XCP addresses are 32-bit. XCP_MEM_PTR turns an address into a dereferenceable
   pointer; XCP_MEM_ADDR maps a pointer back (used by GET_ID). The defaults are
   the identity cast for 32-bit targets; a 64-bit port overrides both in its
   config header with a base offset or a lookup table. */
#ifndef XCP_MEM_PTR
#define XCP_MEM_PTR(addr) ((uint8 *)(uintptr_t)(addr))
#endif
#ifndef XCP_MEM_ADDR
#define XCP_MEM_ADDR(ptr) ((uint32)(uintptr_t)(ptr))
#endif

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
    if ((g_xcp_channel.runtime_ptr == NULL_PTR) ||                                   \
        (g_xcp_channel.runtime_ptr->state == XCP_UNINIT))                            \
    {                                                                              \
        XCP_DET_REPORT_ERROR((sid), XCP_E_UNINIT);                                   \
        return;                                                                    \
    }
#define XCP_TL_CHECK_INIT_R(sid)                                                      \
    if ((g_xcp_channel.runtime_ptr == NULL_PTR) ||                                   \
        (g_xcp_channel.runtime_ptr->state == XCP_UNINIT))                            \
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
#define XCP_TL_CHECK_INIT(sid)   if (g_xcp_channel.runtime_ptr == NULL_PTR) { return; }
#define XCP_TL_CHECK_INIT_R(sid) if (g_xcp_channel.runtime_ptr == NULL_PTR) { return E_NOT_OK; }
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

#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)
    uint8  fr_msg_counter;
#endif
#if (XCP_ON_ETHERNET_ENABLED == STD_ON)
    uint16 eth_msg_counter;
#endif

#if (XCP_DAQ_ENABLED == STD_ON)

    Xcp_DaqListType  daq_list[XCP_MAX_DAQ];
    Xcp_OdtType      odt[XCP_MAX_DAQ * XCP_MAX_ODT];
    Xcp_OdtEntryType odt_entry[XCP_MAX_DAQ * XCP_MAX_ODT * XCP_MAX_ODT_ENTRIES];
    uint8  dto_queue[XCP_DTO_QUEUE_DEPTH][XCP_MAX_DTO];
    uint8  dto_queue_len[XCP_DTO_QUEUE_DEPTH];
    uint8  dto_queue_count;
    uint8  dto_queue_head;
    uint8  dto_queue_tail;

    uint16 active_daq_idx[XCP_MAX_DAQ];
    uint16 active_daq_count;

#if (XCP_ENABLE_DTO_DROP_STATS == STD_ON)
    uint32 dropped_dto_counter;
#endif
    boolean dto_overload_pending;
    uint16  overload_ev_cooldown;
    uint16  dto_busy_watchdog_cnt;

#if (XCP_DTO_QUEUE_DEPTH > 255U) || (XCP_DTO_QUEUE_DEPTH < XCP_MAX_ODT)
    #error "XCP_DTO_QUEUE_DEPTH must be XCP_MAX_ODT..255 so one busy event can queue its burst"
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

    Xcp_TxPipe tx;

    uint32 timestamp;
    uint32 os_timestamp_acc;
    uint32 os_last_tick;

    uint8   protection_status;
    uint8   seed_resource;

    uint8   seed[XCP_MAX_KEY_LENGTH];
    uint8   seed_length;
    uint8   seed_total;
    boolean seed_requested;

#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)
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

    boolean block_transfer_active;
    uint32  block_transfer_remaining;
    uint8   block_transfer_type;
} Xcp_RuntimeType;

struct Xcp_Channel
{
    const Xcp_ConfigType  *config_ptr;
    Xcp_StorageType       *storage_ptr;
    Xcp_RuntimeType       *runtime_ptr;
};

extern Xcp_Channel g_xcp_channel;

static inline sint16 Xcp_ResErr(Xcp_ResFrame *fr, uint8 error_code)
{
    fr[0] = XCP_PID_ERR;
    fr[1] = error_code;
    return 2;
}

#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)
extern Xcp_SegmentType Xcp_SegmentRuntime[XCP_MAX_SEGMENT];
#endif

/* ext validity: when high nibble is non-zero it must map to a configured segment */
static inline boolean Xcp_ExtValid(Xcp_Channel *ch, uint8 ext)
{
#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)
    uint8 seg = (uint8)(ext >> 4U);
    (void)ch;
    return ((seg == 0U) || (seg <= XCP_MAX_SEGMENT)) ? TRUE : FALSE;
#else
    (void)ch;
    return (ext == 0U) ? TRUE : FALSE;
#endif
}

/* Address extension: ext high nibble selects address space.
   0 = default (addr used as-is); s = CalPage segment s-1, translated via
   the currently-active XCP page. No translation when CALPAGE is off or ext==0. */
static inline uint32 Xcp_MemTranslate(Xcp_Channel *ch, uint8 ext, uint32 addr)
{
#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)
    uint8 seg = (uint8)(ext >> 4U);

    if (seg == 0U)
    {
        return addr;
    }
    seg = (uint8)(seg - 1U);
    if (seg >= XCP_MAX_SEGMENT)
    {
        return addr;
    }
    {
        const Xcp_SegmentConfigType *cfg = &ch->config_ptr->xcp_segment_config[seg];
        uint32 page_len = (cfg->num_pages != 0U) ? (cfg->length / cfg->num_pages) : 0U;

        return cfg->start_address
             + ((uint32)Xcp_SegmentRuntime[seg].active_page_xcp * page_len)
             + addr;
    }
#else
    (void)ch; (void)ext;
    return addr;
#endif
}

/* Memory port: translate + access-check + physical pointer. NULL = denied. */
static inline uint8 *Xcp_MemPtrRead(Xcp_Channel *ch, uint8 ext, uint32 addr, uint32 size)
{
    uint32 phys = Xcp_MemTranslate(ch, ext, addr);

    if (XCP_CHECK_READ_ACCESS_EX(ext, phys, size) != 0U)
    {
        return NULL_PTR;
    }
    return XCP_MEM_PTR(phys);
}

static inline uint8 *Xcp_MemPtrWrite(Xcp_Channel *ch, uint8 ext, uint32 addr, uint32 size)
{
    uint32 phys = Xcp_MemTranslate(ch, ext, addr);

    if (XCP_CHECK_WRITE_ACCESS_EX(ext, phys, size) != 0U)
    {
        return NULL_PTR;
    }
    return XCP_MEM_PTR(phys);
}

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

void Xcp_SessionOpen(void);

void Xcp_SessionClose(void);

void Xcp_EnqueueCommand(const uint8 *cmd_data, uint8 cmd_length);

void Xcp_ProcessCommandQueue(void);

void Xcp_DtoSendOrQueue(Xcp_Channel *ch, const uint8 *dto_data, uint8 dto_length);

void Xcp_DtoSendNext(Xcp_Channel *ch);

Std_ReturnType Xcp_Tl_SendCto(const uint8 *data, uint8 length);

Std_ReturnType Xcp_Tl_SendDto(const uint8 *data, uint8 length);

void Xcp_HandleTxConfirmation(Std_ReturnType result);

Std_ReturnType Xcp_HandleTriggerTransmit(PduInfoType *pdu_info_ptr);

void Xcp_HandleRxIndication(const PduInfoType *pdu_info_ptr);

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
