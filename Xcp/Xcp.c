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

void Xcp_EnqueueCommand(const uint8 *cmd_data, uint8 cmd_length)
{
    if ((cmd_data == NULL) || (cmd_length == 0U)
#if (XCP_MAX_CTO < 255U)
        || (cmd_length > XCP_MAX_CTO)
#endif
    )
    {
        return;
    }

#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)

    if (g_xcp_channel.runtime_ptr->cmd_queue_count >= XCP_CMD_QUEUE_SIZE)
    {
        g_xcp_channel.runtime_ptr->busy_pending = TRUE;
        return;
    }

    (void)memcpy(g_xcp_channel.runtime_ptr->cmd_queue[g_xcp_channel.runtime_ptr->cmd_queue_tail],
                 cmd_data, cmd_length);
    g_xcp_channel.runtime_ptr->cmd_queue_len[g_xcp_channel.runtime_ptr->cmd_queue_tail] = cmd_length;

    g_xcp_channel.runtime_ptr->cmd_queue_tail =
        (g_xcp_channel.runtime_ptr->cmd_queue_tail + 1U) % XCP_CMD_QUEUE_SIZE;
    g_xcp_channel.runtime_ptr->cmd_queue_count++;
    g_xcp_channel.runtime_ptr->cmd_pending = TRUE;

#else

    if (g_xcp_channel.runtime_ptr->cmd_pending == TRUE)
    {

        g_xcp_channel.runtime_ptr->busy_pending = TRUE;
        return;
    }

    (void)memcpy(g_xcp_channel.runtime_ptr->cmd_queue_buffer, cmd_data, cmd_length);
    g_xcp_channel.runtime_ptr->cmd_queue_length = cmd_length;
    g_xcp_channel.runtime_ptr->cmd_pending      = TRUE;
#endif
}

void Xcp_ProcessCommandQueue(void)
{
    uint8 *cmd_data;
    uint8  cmd_length;

    if (g_xcp_channel.runtime_ptr->busy_pending == TRUE)
    {
        if ((g_xcp_channel.runtime_ptr->tx.state != XCP_TX_CTO)
            && (g_xcp_channel.runtime_ptr->tx.parked == FALSE))
        {
            Xcp_SendErrorResponse(XCP_ERR_CMD_BUSY);

            g_xcp_channel.runtime_ptr->busy_pending = FALSE;
        }

        return;
    }

    {
        Xcp_Channel *ch = &g_xcp_channel;

        if ((ch->runtime_ptr->cmd_pending == TRUE)
            && (Xcp_IsBlockUploadActive(ch) != FALSE))
        {
            Xcp_CancelBlockTransfer(ch);
        }
    }

    if (g_xcp_channel.runtime_ptr->cmd_pending == TRUE)
    {
        if ((g_xcp_channel.runtime_ptr->tx.state != XCP_TX_CTO)
            && (g_xcp_channel.runtime_ptr->tx.parked == FALSE))
        {
#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON)
            if (g_xcp_channel.runtime_ptr->cmd_queue_count > 0U)
            {
                cmd_data   = g_xcp_channel.runtime_ptr->cmd_queue[g_xcp_channel.runtime_ptr->cmd_queue_head];
                cmd_length = g_xcp_channel.runtime_ptr->cmd_queue_len[g_xcp_channel.runtime_ptr->cmd_queue_head];

                Xcp_ProcessCommand(cmd_data, cmd_length);

                g_xcp_channel.runtime_ptr->cmd_queue_head =
                    (g_xcp_channel.runtime_ptr->cmd_queue_head + 1U) % XCP_CMD_QUEUE_SIZE;
                g_xcp_channel.runtime_ptr->cmd_queue_count--;

                if (g_xcp_channel.runtime_ptr->cmd_queue_count == 0U)
                {
                    g_xcp_channel.runtime_ptr->cmd_pending = FALSE;
                }
            }
#else
            cmd_data   = g_xcp_channel.runtime_ptr->cmd_queue_buffer;
            cmd_length = g_xcp_channel.runtime_ptr->cmd_queue_length;

            Xcp_ProcessCommand(cmd_data, cmd_length);

            g_xcp_channel.runtime_ptr->cmd_pending = FALSE;
#endif
        }
    }
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

    if (runtime->tx.state == XCP_TX_DTO)
    {
        runtime->tx.state = XCP_TX_IDLE;

        if (result == E_OK)
        {

            if (runtime->tx.parked != FALSE)
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

            if (runtime->tx.parked != FALSE)
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

    if (runtime->tx.state == XCP_TX_CTO)
    {
        runtime->tx.state = XCP_TX_IDLE;

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

        if (runtime->tx.parked != FALSE)
        {
            Xcp_SendPendingCto();
        }
        else
        {
#if (XCP_DAQ_ENABLED == STD_ON)
            Xcp_DtoSendNext(&g_xcp_channel);
#endif
        }

        return;
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
    Std_ReturnType   ret_val;

    if (runtime->tx_mode != XCP_TX_ON)
    {
        return E_NOT_OK;
    }

    if (runtime->tx.state == XCP_TX_IDLE)
    {
        runtime->tx.state = XCP_TX_CTO;
        ret_val = Xcp_Tl_SendCto(res_data, res_length);

        if (ret_val != E_OK)
        {
            runtime->tx.state = XCP_TX_IDLE;
        }

        return ret_val;
    }

    if (runtime->tx.parked != FALSE)
    {
        return E_NOT_OK;
    }

    XCP_DTO_ENTER_CRITICAL();
    (void)memcpy(runtime->tx.parked_data, res_data, res_length);
    runtime->tx.parked_len = res_length;
    runtime->tx.parked     = TRUE;
    XCP_DTO_EXIT_CRITICAL();
    return E_OK;
}

void Xcp_SendPendingCto(void)
{
    Xcp_RuntimeType *runtime = g_xcp_channel.runtime_ptr;

    if ((runtime->tx.parked == FALSE) || (runtime->tx_mode != XCP_TX_ON))
    {
        return;
    }

    if (runtime->tx.state == XCP_TX_IDLE)
    {

        uint8 pend_copy[XCP_MAX_CTO];
        uint8 pend_len = runtime->tx.parked_len;

        (void)memcpy(pend_copy, runtime->tx.parked_data, pend_len);
        runtime->tx.parked = FALSE;

        runtime->tx.state = XCP_TX_CTO;

        if (Xcp_Tl_SendCto(pend_copy, pend_len) != E_OK)
        {
            runtime->tx.state = XCP_TX_IDLE;

            (void)memcpy(runtime->tx.parked_data, pend_copy, pend_len);
            runtime->tx.parked_len = pend_len;
            runtime->tx.parked     = TRUE;
        }
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

    runtime->tx.parked     = FALSE;
    runtime->tx.parked_len = 0U;
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
