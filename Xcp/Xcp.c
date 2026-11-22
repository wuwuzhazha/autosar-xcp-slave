#include "Xcp.h"
#include "Xcp_Internal.h"
#include "Xcp_Std.h"
#include "Xcp_Daq.h"
#include "Xcp_SeedKey.h"
#include "Xcp_CalPage.h"
#include "Xcp_FlashProg.h"
#include "Xcp_Resume.h"

#if (XCP_TIMESTAMP_TYPE != XCP_NO_TIME_STAMP) && (XCP_TIMESTAMP_SOURCE_OS == STD_ON)
#include "Os.h"

static void Xcp_PrimeOsTick(Xcp_RuntimeType *runtime)
{
    (void)GetCounterValue(XCP_OS_COUNTER, &runtime->os_last_tick);
}
#else
static void Xcp_PrimeOsTick(Xcp_RuntimeType *runtime)
{
    (void)runtime;
}
#endif

#include <string.h>

extern const Xcp_ConfigType xcp_config;

Xcp_StorageType g_xcp_storage;
Xcp_RuntimeType g_xcp_runtime;
Xcp_Channel g_xcp_channel;

/* Locked. Reserve the room one response needs before its command runs, so a
   command is never consumed when the pipe cannot take the answer. */
static boolean Xcp_CtoReserveLocked(Xcp_RuntimeType *runtime)
{
    Xcp_TxPipe *tx = &runtime->tx;

    if (runtime->tx_mode != XCP_TX_ON)
    {
        return TRUE;
    }

    if ((tx->cto_count + tx->cto_claimed) >= XCP_CTO_QUEUE_DEPTH)
    {
        return FALSE;
    }

    tx->cto_claimed++;
    return TRUE;
}

/* Locked. Hand a reservation back when its response was never stored. */
static void Xcp_CtoReleaseLocked(Xcp_TxPipe *tx)
{
    if (tx->cto_claimed > 0U)
    {
        tx->cto_claimed--;
    }
}

/* Locked. Append one response, spending the caller's reservation if it holds
   one, so responses leave in the order the commands produced them. */
static boolean Xcp_CtoStoreLocked(Xcp_TxPipe *tx, const uint8 *res_data, uint8 res_length)
{
    uint8 slot;

    if (tx->cto_claimed > 0U)
    {
        tx->cto_claimed--;
    }
    else if (tx->cto_count >= XCP_CTO_QUEUE_DEPTH)
    {
        return FALSE;
    }

    slot = tx->cto_tail;
    (void)memcpy(tx->cto_data[slot], res_data, res_length);
    tx->cto_len[slot] = res_length;
    tx->cto_tail = (uint8)((slot + 1U) % XCP_CTO_QUEUE_DEPTH);
    tx->cto_count++;
    return TRUE;
}

static boolean Xcp_CtoReserve(void)
{
    boolean reserved;

    XCP_DTO_ENTER_CRITICAL();
    reserved = Xcp_CtoReserveLocked(g_xcp_channel.runtime_ptr);
    XCP_DTO_EXIT_CRITICAL();

    return reserved;
}

static void Xcp_CtoRelease(void)
{
    XCP_DTO_ENTER_CRITICAL();
    Xcp_CtoReleaseLocked(&g_xcp_channel.runtime_ptr->tx);
    XCP_DTO_EXIT_CRITICAL();
}

/* Copy the oldest queued command out of the queue. The command runs from the
   caller's copy, so the producer may wrap around and reuse the slot while the
   handler still holds the bytes. */
static boolean Xcp_CmdTake(uint8 *cmd_copy, uint8 *cmd_length)
{
    Xcp_RuntimeType *runtime = g_xcp_channel.runtime_ptr;
    boolean          taken   = FALSE;

    XCP_DTO_ENTER_CRITICAL();

#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)

    if (runtime->cmd_queue_count > 0U)
    {
        uint8 head = runtime->cmd_queue_head;

        *cmd_length = runtime->cmd_queue_len[head];
        (void)memcpy(cmd_copy, runtime->cmd_queue[head], *cmd_length);
        runtime->cmd_queue_head = (uint8)((head + 1U) % XCP_CMD_QUEUE_SIZE);
        runtime->cmd_queue_count--;
        taken = TRUE;
    }

#else

    if (runtime->cmd_pending != FALSE)
    {
        *cmd_length = runtime->cmd_queue_length;
        (void)memcpy(cmd_copy, runtime->cmd_queue_buffer, *cmd_length);
        runtime->cmd_pending = FALSE;
        taken = TRUE;
    }

#endif

    XCP_DTO_EXIT_CRITICAL();

    return taken;
}

void Xcp_EnqueueCommand(const uint8 *cmd_data, uint8 cmd_length)
{
    Xcp_RuntimeType *runtime = g_xcp_channel.runtime_ptr;

    if ((cmd_data == NULL) || (cmd_length == 0U)
#if (XCP_MAX_CTO < 255U)
        || (cmd_length > XCP_MAX_CTO)
#endif
    )
    {
        return;
    }

    XCP_DTO_ENTER_CRITICAL();

#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)

    if (runtime->cmd_queue_count >= XCP_CMD_QUEUE_SIZE)
    {
        runtime->busy_pending = TRUE;
        XCP_DTO_EXIT_CRITICAL();
        return;
    }

    (void)memcpy(runtime->cmd_queue[runtime->cmd_queue_tail], cmd_data, cmd_length);
    runtime->cmd_queue_len[runtime->cmd_queue_tail] = cmd_length;

    runtime->cmd_queue_tail = (uint8)((runtime->cmd_queue_tail + 1U) % XCP_CMD_QUEUE_SIZE);
    runtime->cmd_queue_count++;
    runtime->cmd_pending = TRUE;

#else

    if (runtime->cmd_pending == TRUE)
    {

        runtime->busy_pending = TRUE;
        XCP_DTO_EXIT_CRITICAL();
        return;
    }

    (void)memcpy(runtime->cmd_queue_buffer, cmd_data, cmd_length);
    runtime->cmd_queue_length = cmd_length;
    runtime->cmd_pending      = TRUE;
#endif

    XCP_DTO_EXIT_CRITICAL();
}

void Xcp_ProcessCommandQueue(void)
{
    Xcp_RuntimeType *runtime = g_xcp_channel.runtime_ptr;
    uint8            cmd_copy[XCP_MAX_CTO];
    uint8            cmd_length;
    uint8            budget;

    if (runtime->busy_pending != FALSE)
    {
        if (Xcp_CtoReserve() != FALSE)
        {
            Xcp_SendErrorResponse(XCP_ERR_CMD_BUSY);

            runtime->busy_pending = FALSE;
            Xcp_CtoRelease();
        }

        return;
    }

    /* A command costs one response slot. Running out of slots ends the call
       with the command still queued and its side effects unapplied. */
    for (budget = (uint8)XCP_CMD_QUEUE_SIZE; budget > 0U; budget--)
    {
        if (Xcp_CtoReserve() == FALSE)
        {
            break;
        }

        if (Xcp_CmdTake(cmd_copy, &cmd_length) == FALSE)
        {
            Xcp_CtoRelease();
            break;
        }

        if (Xcp_IsBlockUploadActive(&g_xcp_channel) != FALSE)
        {
            Xcp_CancelBlockTransfer(&g_xcp_channel);
        }

        Xcp_ProcessCommand(cmd_copy, cmd_length);

        Xcp_CtoRelease();
    }

    /* The flag follows the hand-off, not one handler run: while the queue
       still holds a command the confirmation path must not start a
       block-upload frame in the middle of the burst. */
    XCP_DTO_ENTER_CRITICAL();
    if (Xcp_CmdQueueEmpty(runtime) != FALSE)
    {
        runtime->cmd_pending = FALSE;
    }
    XCP_DTO_EXIT_CRITICAL();
}

void Xcp_Init(const Xcp_ConfigType *Xcp_ConfigPtr)
{

    (void)memset(&g_xcp_storage, 0, sizeof(Xcp_StorageType));

    (void)memset(&g_xcp_runtime, 0, sizeof(Xcp_RuntimeType));

    (void)memset(&g_xcp_channel, 0, sizeof(Xcp_Channel));

    g_xcp_channel.config_ptr = (Xcp_ConfigPtr != NULL_PTR) ? Xcp_ConfigPtr : &xcp_config;
    g_xcp_channel.storage_ptr = &g_xcp_storage;
    g_xcp_channel.runtime_ptr = &g_xcp_runtime;

    g_xcp_channel.runtime_ptr->state = XCP_INIT;

    g_xcp_channel.runtime_ptr->tx_mode  = XCP_TX_ON;
    g_xcp_channel.runtime_ptr->tx.state = XCP_TX_IDLE;

    Xcp_PrimeOsTick(&g_xcp_runtime);

#if (XCP_ON_CAN_ENABLED == STD_ON) && (XCP_DEV_ERROR_DETECT == STD_ON)

    if (g_xcp_channel.config_ptr->xcp_rx_pdu_id == g_xcp_channel.config_ptr->xcp_tx_pdu_id)
    {
        XCP_DET_REPORT_ERROR(XCP_SID_INIT, XCP_E_INV_CONFIG);
        g_xcp_channel.runtime_ptr->state = XCP_UNINIT;
        return;
    }
#endif

#if (XCP_DAQ_ENABLED == STD_ON)
    Xcp_InitDaqLists(&g_xcp_channel);
#if (XCP_STIM_TIMEOUT_ENABLED == STD_ON)
    Xcp_InitStimTimeout(&g_xcp_channel);
#endif
#endif

#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)
    Xcp_InitCalPage(&g_xcp_channel);
#endif

#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)
    Xcp_InitFlashProg(&g_xcp_channel);
#endif

#if (XCP_RESUME_MODE_ENABLED == STD_ON)

    Xcp_InitResumeMode();
#endif

#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)

    (void)Xcp_FrConfigureLPdu();
#endif
}

#if (XCP_SUPPRESS_TX_SUPPORT == STD_ON)

void Xcp_SetTransmissionMode(NetworkHandleType channel, Xcp_TransmissionModeType mode)
{

    (void)channel;

    g_xcp_channel.runtime_ptr->tx_mode = mode;

    if (mode == XCP_TX_ON)
    {
        Xcp_SendPendingCto();
    }
}

#endif

#if (XCP_VERSION_INFO_API == STD_ON)

void Xcp_GetVersionInfo(Std_VersionInfoType *versioninfo)
{
#if (XCP_DEV_ERROR_DETECT == STD_ON)

    if (versioninfo == NULL_PTR)
    {
        XCP_DET_REPORT_ERROR(XCP_SID_GET_VERSION_INFO, XCP_E_PARAM_POINTER);
        return;
    }
#endif
    versioninfo->vendorID         = XCP_VENDOR_ID;
    versioninfo->moduleID         = XCP_MODULE_ID;
    versioninfo->sw_major_version = XCP_SW_MAJOR_VERSION;
    versioninfo->sw_minor_version = XCP_SW_MINOR_VERSION;
    versioninfo->sw_patch_version = XCP_SW_PATCH_VERSION;
}
#endif

void Xcp_HandleTxConfirmation(Std_ReturnType result)
{
    Xcp_Channel     *ch = &g_xcp_channel;
    Xcp_RuntimeType *runtime = ch->runtime_ptr;
    Xcp_TxStateType  confirmed = XCP_TX_IDLE;

    XCP_DTO_ENTER_CRITICAL();

    if (runtime->tx.state == XCP_TX_DTO)
    {
        runtime->tx.state = XCP_TX_IDLE;
        confirmed = XCP_TX_DTO;
    }
    else if (runtime->tx.state == XCP_TX_CTO)
    {
        runtime->tx.state = XCP_TX_IDLE;
        confirmed = XCP_TX_CTO;
    }

    XCP_DTO_EXIT_CRITICAL();

    if (confirmed == XCP_TX_DTO)
    {
        if (result == E_OK)
        {

            if (runtime->tx.cto_count > 0U)
            {
                Xcp_SendPendingCto();
            }
            else if ((runtime->cmd_pending != FALSE)
                     || (Xcp_ContinueBlockUpload(ch) == FALSE))
            {
#if (XCP_DAQ_ENABLED == STD_ON)
                Xcp_DtoSendNext(&g_xcp_channel);
#endif
            }
        }
        else
        {
            runtime->tx.dto_latch = FALSE;

            if (runtime->tx.cto_count > 0U)
            {
                Xcp_SendPendingCto();
            }
            else if (runtime->cmd_pending == FALSE)
            {
                (void)Xcp_ContinueBlockUpload(ch);
            }
        }

        return;
    }

    if (confirmed == XCP_TX_CTO)
    {
        if (result == E_OK)
        {
            runtime->tx.last_error = 0U;
        }
        else
        {
            runtime->tx.last_error = 1U;
            Xcp_CancelBlockTransfer(ch);
        }

        if ((runtime->cmd_pending == FALSE)
            && (Xcp_ContinueBlockUpload(ch) != FALSE))
        {
            return;
        }

        if (runtime->tx.cto_count > 0U)
        {
            Xcp_SendPendingCto();
        }
        else
        {
#if (XCP_DAQ_ENABLED == STD_ON)
            Xcp_DtoSendNext(&g_xcp_channel);
#endif
        }
    }
}

Std_ReturnType Xcp_HandleTriggerTransmit(PduInfoType *pdu_info_ptr)
{
    if (pdu_info_ptr->sdu_length < g_xcp_channel.storage_ptr->cto_length)
    {
        return E_NOT_OK;
    }

    (void)memcpy(pdu_info_ptr->sdu_data_ptr,
                 g_xcp_channel.storage_ptr->cto_buffer,
                 g_xcp_channel.storage_ptr->cto_length);
    pdu_info_ptr->sdu_length = g_xcp_channel.storage_ptr->cto_length;
    return E_OK;
}

void Xcp_HandleRxIndication(const PduInfoType *pdu_info_ptr)
{
    if ((pdu_info_ptr == NULL_PTR) || (pdu_info_ptr->sdu_data_ptr == NULL_PTR))
    {
        return;
    }
#if (XCP_DAQ_ENABLED == STD_ON)
    if ((pdu_info_ptr->sdu_length > 0U)
        && (pdu_info_ptr->sdu_data_ptr[0] < XCP_CMD_PROGRAM_VERIFY))
    {

        (void)Xcp_ReceiveStimDto(&g_xcp_channel, pdu_info_ptr->sdu_data_ptr,
                               (uint8)pdu_info_ptr->sdu_length);
        return;
    }
#endif

    Xcp_EnqueueCommand(pdu_info_ptr->sdu_data_ptr, (uint8)pdu_info_ptr->sdu_length);
}

void Xcp_MainFunction(void)
{
    Xcp_Channel *ch = &g_xcp_channel;

    if ((ch->runtime_ptr == NULL) || (ch->runtime_ptr->state == XCP_UNINIT))
    {
        return;
    }

#if (XCP_TIMESTAMP_TYPE != XCP_NO_TIME_STAMP)
    g_xcp_channel.runtime_ptr->timestamp++;
#endif

    Xcp_ProcessCommandQueue();

    Xcp_ContinueBlockUpload(ch);

#if (XCP_RESUME_MODE_ENABLED == STD_ON)

    Xcp_ResumeMainFunction();
#endif

#if (XCP_DAQ_ENABLED == STD_ON)
#if (XCP_STIM_TIMEOUT_ENABLED == STD_ON)

    Xcp_CheckStimTimeout(&g_xcp_channel);
#endif

    Xcp_ReportDtoOverload(&g_xcp_channel);

    if (g_xcp_channel.runtime_ptr->connection_state == XCP_CONNECTED)
    {
        uint16                      i;
        const Xcp_EventChannelType *event_cfg;
        uint32                      cycle_ms;

        for (i = 0U; i < XCP_MAX_EVENT_CHANNEL; i++)
        {
            event_cfg = &g_xcp_channel.config_ptr->xcp_event_channel[i];

            if (event_cfg->time_cycle == 0U)
            {
                continue;
            }

            switch (event_cfg->time_unit)
            {
            case XCP_TIMESTAMP_UNIT_1NS:
            case XCP_TIMESTAMP_UNIT_10NS:
            case XCP_TIMESTAMP_UNIT_100NS:

                cycle_ms = 1U;
                break;
            case XCP_TIMESTAMP_UNIT_1US:
                cycle_ms = (event_cfg->time_cycle + 999U) / 1000U;
                break;
            case XCP_TIMESTAMP_UNIT_10US:
                cycle_ms = (event_cfg->time_cycle * 10U + 999U) / 1000U;
                break;
            case XCP_TIMESTAMP_UNIT_100US:
                cycle_ms = (event_cfg->time_cycle * 100U + 999U) / 1000U;
                break;
            case XCP_TIMESTAMP_UNIT_1MS:
                cycle_ms = event_cfg->time_cycle;
                break;
            case XCP_TIMESTAMP_UNIT_10MS:
                cycle_ms = event_cfg->time_cycle * 10U;
                break;
            case XCP_TIMESTAMP_UNIT_100MS:
                cycle_ms = event_cfg->time_cycle * 100U;
                break;
            case XCP_TIMESTAMP_UNIT_1S:
                cycle_ms = event_cfg->time_cycle * 1000U;
                break;
            default:
                cycle_ms = event_cfg->time_cycle;
                break;
            }

            g_xcp_channel.runtime_ptr->event_channel_counter[i] += XCP_MAIN_FUNCTION_PERIOD_MS;

            if (g_xcp_channel.runtime_ptr->event_channel_counter[i] >= cycle_ms)
            {
                Xcp_Event(event_cfg->event_channel_number);

                g_xcp_channel.runtime_ptr->event_channel_counter[i] %= cycle_ms;
            }
        }
    }
#endif
}

void Xcp_Event(uint16 event_channel)
{
    if ((g_xcp_channel.runtime_ptr == NULL) || (g_xcp_channel.runtime_ptr->state != XCP_INIT))
    {
        return;
    }

    if (g_xcp_channel.runtime_ptr->connection_state != XCP_CONNECTED)
    {
        return;
    }

#if (XCP_DAQ_ENABLED == STD_ON)
    Xcp_ProcessDaq(&g_xcp_channel, event_channel);
#else
    (void)event_channel;
#endif
}

void Xcp_SendPositiveResponse(uint8 length)
{
    g_xcp_channel.storage_ptr->cto_length = length;
    (void)Xcp_SendResponse(g_xcp_channel.storage_ptr->cto_buffer, length);
}

void Xcp_SendErrorResponse(uint8 error_code)
{
    g_xcp_channel.storage_ptr->cto_buffer[0] = XCP_PID_ERR;
    g_xcp_channel.storage_ptr->cto_buffer[1] = error_code;
    g_xcp_channel.storage_ptr->cto_length    = 2U;
    (void)Xcp_SendResponse(g_xcp_channel.storage_ptr->cto_buffer, 2U);
}

Std_ReturnType Xcp_SendResponse(const uint8 *res_data, uint8 res_length)
{
    Xcp_RuntimeType *runtime = g_xcp_channel.runtime_ptr;
    Xcp_TxPipe      *tx      = &runtime->tx;
    Std_ReturnType   ret_val;

    if (runtime->tx_mode != XCP_TX_ON)
    {
        return E_NOT_OK;
    }

    XCP_DTO_ENTER_CRITICAL();

    if ((tx->cto_count == 0U) && (tx->state == XCP_TX_IDLE))
    {
        tx->state = XCP_TX_CTO;
        XCP_DTO_EXIT_CRITICAL();

        ret_val = Xcp_Tl_SendCto(res_data, res_length);

        if (ret_val != E_OK)
        {
            XCP_DTO_ENTER_CRITICAL();
            tx->state  = XCP_TX_IDLE;
            ret_val    = (Xcp_CtoStoreLocked(tx, res_data, res_length) != FALSE) ? E_OK : E_NOT_OK;
            XCP_DTO_EXIT_CRITICAL();
        }

        return ret_val;
    }

    if (Xcp_CtoStoreLocked(tx, res_data, res_length) == FALSE)
    {
        XCP_DTO_EXIT_CRITICAL();
        return E_NOT_OK;
    }

    XCP_DTO_EXIT_CRITICAL();
    return E_OK;
}

void Xcp_SendPendingCto(void)
{
    Xcp_RuntimeType *runtime = g_xcp_channel.runtime_ptr;
    Xcp_TxPipe      *tx      = &runtime->tx;
    uint8            pend_copy[XCP_MAX_CTO];
    uint8            pend_len;

    XCP_DTO_ENTER_CRITICAL();

    if ((tx->cto_count == 0U) || (tx->cto_inflight != FALSE)
        || (runtime->tx_mode != XCP_TX_ON) || (tx->state != XCP_TX_IDLE))
    {
        XCP_DTO_EXIT_CRITICAL();
        return;
    }

    pend_len = tx->cto_len[tx->cto_head];
    (void)memcpy(pend_copy, tx->cto_data[tx->cto_head], pend_len);
    tx->cto_inflight = TRUE;
    tx->state        = XCP_TX_CTO;

    XCP_DTO_EXIT_CRITICAL();

    if (Xcp_Tl_SendCto(pend_copy, pend_len) == E_OK)
    {
        XCP_DTO_ENTER_CRITICAL();
        tx->cto_head     = (uint8)((tx->cto_head + 1U) % XCP_CTO_QUEUE_DEPTH);
        tx->cto_count--;
        tx->cto_inflight = FALSE;
        XCP_DTO_EXIT_CRITICAL();
    }
    else
    {
        XCP_DTO_ENTER_CRITICAL();
        tx->cto_inflight = FALSE;
        tx->state        = XCP_TX_IDLE;
        XCP_DTO_EXIT_CRITICAL();
    }
}

void Xcp_SessionOpen(void)
{
    Xcp_RuntimeType *runtime = g_xcp_channel.runtime_ptr;

    runtime->connection_state = XCP_CONNECTED;
    /* Persist STORE_CAL/CLEAR_DAQ bits across sessions for master read-back */
    runtime->session_status &= (uint8)(XCP_SS_STORE_CAL_REQ | XCP_SS_CLEAR_DAQ_REQ);

#if (XCP_SEED_KEY_ENABLED == STD_ON)
    Xcp_InitProtection(&g_xcp_channel);
#endif

    Xcp_InitBlockTransfer(&g_xcp_channel);
}

void Xcp_SessionClose(void)
{
    Xcp_RuntimeType *runtime = g_xcp_channel.runtime_ptr;

#if (XCP_DAQ_ENABLED == STD_ON)
    Xcp_FreeDaqLists(&g_xcp_channel);
#endif

    runtime->tx.cto_head     = 0U;
    runtime->tx.cto_tail     = 0U;
    runtime->tx.cto_count    = 0U;
    runtime->tx.cto_claimed  = 0U;
    runtime->tx.cto_inflight = FALSE;
    Xcp_CancelBlockTransfer(&g_xcp_channel);
    runtime->connection_state = XCP_DISCONNECTED;

    runtime->mta            = 0U;
    runtime->mta_extension  = 0U;
    runtime->seed_requested = FALSE;
    runtime->seed_length    = 0U;
    runtime->seed_total     = 0U;

#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)
    runtime->pgm_started   = FALSE;
    runtime->pgm_processor = 0U;
    runtime->pgm_address   = 0U;
    runtime->pgm_size      = 0U;
#endif
}

uint32 Xcp_GetTimestamp(void)
{
#if (XCP_TIMESTAMP_TYPE != XCP_NO_TIME_STAMP)
#if (XCP_TIMESTAMP_SOURCE_OS == STD_ON)

    TickType os_elapsed = 0U;

    if (GetElapsedValue(XCP_OS_COUNTER, &g_xcp_channel.runtime_ptr->os_last_tick, &os_elapsed) == E_OK)
    {
        g_xcp_channel.runtime_ptr->os_timestamp_acc += os_elapsed;
    }

    return g_xcp_channel.runtime_ptr->os_timestamp_acc;
#else
    return g_xcp_channel.runtime_ptr->timestamp;
#endif
#else
    return 0U;
#endif
}

Std_ReturnType Xcp_SendEventPacket(uint8 event_code, const uint8 *event_info, uint8 info_length)
{
    uint8          ev_buffer[XCP_MAX_CTO];
    uint8          ev_length;
    Std_ReturnType ret_val = E_NOT_OK;

    if ((g_xcp_channel.runtime_ptr == NULL) ||
        (g_xcp_channel.runtime_ptr->state != XCP_INIT))
    {
        return E_NOT_OK;
    }

    if ((g_xcp_channel.runtime_ptr->connection_state != XCP_CONNECTED)
        && (event_code != XCP_EV_RESUME_MODE))
    {
        return E_NOT_OK;
    }

    if (g_xcp_channel.runtime_ptr->tx_mode != XCP_TX_ON)
    {
        return E_NOT_OK;
    }

    ev_buffer[0] = XCP_PID_EV;
    ev_buffer[1] = event_code;
    ev_length    = 2U;

    if ((event_info != NULL) && (info_length > 0U))
    {
        if ((ev_length + info_length) <= XCP_MAX_CTO)
        {
            (void)memcpy(&ev_buffer[ev_length], event_info, info_length);
            ev_length += info_length;
        }
    }

    ret_val = Xcp_SendResponse(ev_buffer, ev_length);
    return ret_val;
}

Std_ReturnType Xcp_SendServiceRequest(uint8 service_code, const uint8 *service_data, uint8 data_length)
{
    uint8          serv_buffer[XCP_MAX_CTO];
    uint8          serv_length;
    Std_ReturnType ret_val = E_NOT_OK;

    if ((g_xcp_channel.runtime_ptr == NULL) ||
        (g_xcp_channel.runtime_ptr->state != XCP_INIT))
    {
        return E_NOT_OK;
    }

    if (g_xcp_channel.runtime_ptr->connection_state != XCP_CONNECTED)
    {
        return E_NOT_OK;
    }

    if (g_xcp_channel.runtime_ptr->tx_mode != XCP_TX_ON)
    {
        return E_NOT_OK;
    }

    serv_buffer[0] = XCP_PID_SERV;
    serv_buffer[1] = service_code;
    serv_length    = 2U;

    if ((service_data != NULL) && (data_length > 0U))
    {
        if ((serv_length + data_length) <= XCP_MAX_CTO)
        {
            (void)memcpy(&serv_buffer[serv_length], service_data, data_length);
            serv_length += data_length;
        }
    }

    ret_val = Xcp_SendResponse(serv_buffer, serv_length);
    return ret_val;
}

Std_ReturnType Xcp_SendDaqOverloadEvent(uint16 daq_list_no)
{
    uint8 info[2];

    info[0] = (uint8)(daq_list_no & 0xFFU);
    info[1] = (uint8)((daq_list_no >> 8U) & 0xFFU);
    return Xcp_SendEventPacket(XCP_EV_DAQ_OVERLOAD, info, 2U);
}

Std_ReturnType Xcp_SendSessionTerminatedEvent(void)
{
    return Xcp_SendEventPacket(XCP_EV_SESSION_TERMINATED, NULL, 0U);
}

Std_ReturnType Xcp_SendSleepEvent(void)
{
    return Xcp_SendEventPacket(XCP_EV_SLEEP, NULL, 0U);
}

Std_ReturnType Xcp_SendWakeUpEvent(void)
{
    return Xcp_SendEventPacket(XCP_EV_WAKE_UP, NULL, 0U);
}
