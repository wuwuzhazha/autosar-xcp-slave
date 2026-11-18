/**
 * @file Xcp.h
 * @brief Public API of the XCP slave module (ASAM XCP Part 2 v1.1, AUTOSAR CP R4.4.0).
 *
 * SPDX-License-Identifier: MIT
 *
 * Single-channel slave: one protocol instance, one active session, one of
 * CAN / FlexRay / Ethernet / CDD transports selected by `Xcp_LinkType` at
 * config-generation time.
 *
 * Usage:
 *   1. Generate a configuration: `python Tools/xcpgen/xcpgen.py --profile <name> --out Gen/<name>`.
 *   2. Call `Xcp_Init(&xcp_config)` at startup.
 *   3. Feed received frames to `Xcp_HandleRxIndication` (or via `Xcp_<Lo>RxIndication`).
 *   4. Call `Xcp_MainFunction()` every `XCP_MAIN_FUNCTION_PERIOD_MS`.
 *   5. Call `Xcp_HandleTxConfirmation` after each lower-layer TX confirmation.
 */
#ifndef XCP_H
#define XCP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "StandardTypes.h"
#include "ComStack_Types.h"
#include "Xcp_Config.h"
#include "Xcp_Types.h"

/**
 * @brief Initialize the XCP slave.
 * @param Xcp_ConfigPtr generated config (Gen/<profile>/Xcp_Cfg.c); NULL uses
 *        the default `xcp_config` symbol from the generated code.
 * @note  Resets storage, runtime, DAQ lists, segments, seed/key, RESUME state.
 *        Safe to call again to reset the module.
 */
void Xcp_Init(const Xcp_ConfigType *Xcp_ConfigPtr);

#if (XCP_VERSION_INFO_API == STD_ON)

/**
 * @brief Return module version info.
 * @param versioninfo out — receives AUTOSAR `Std_VersionInfoType`.
 * @note  DET: `XCP_E_PARAM_POINTER` if `versioninfo == NULL`.
 */
void Xcp_GetVersionInfo(Std_VersionInfoType *versioninfo);
#endif

#if (XCP_SUPPRESS_TX_SUPPORT == STD_ON)

/**
 * @brief Enable / disable TX on this channel.
 * @param channel ignored (single-channel module; pass 0)
 * @param mode    `XCP_TX_ON` to emit, `XCP_TX_OFF` to suppress all TX
 *                (pending DTOs/CTOs are held, not dropped).
 */
void Xcp_SetTransmissionMode(NetworkHandleType channel, Xcp_TransmissionModeType mode);
#endif

/**
 * @brief Periodic service — call every `XCP_MAIN_FUNCTION_PERIOD_MS`.
 *        Drives command-queue dispatch, RESUME pending work, STIM timeouts,
 *        DTO overload reporting, and the per-event-channel scheduler.
 */
void Xcp_MainFunction(void);

/**
 * @brief Manually trigger an event channel's DAQ sampling.
 * @param event_channel `event_channel_number` from `xcp_event_channel[]` —
 *        DAQ lists whose `event_channel` matches will emit DTOs.
 */
void Xcp_Event(uint16 event_channel);

/**
 * @brief Emit an EV_<code> packet on the CTO channel.
 * @param event_code    `XCP_EV_*` constant (`Xcp_Types.h`)
 * @param event_info    optional payload (may be NULL when `info_length == 0`)
 * @param info_length   bytes in `event_info` (≤ `XCP_MAX_CTO - 2`)
 * @return `E_OK` queued/sent, `E_NOT_OK` if uninitialized, not connected,
 *         or `tx_mode == XCP_TX_OFF`.
 */
Std_ReturnType Xcp_SendEventPacket(uint8 event_code, const uint8 *event_info, uint8 info_length);

/**
 * @brief Emit a SERV_<code> packet (service request to the master).
 * @param service_code  `XCP_SERV_*` constant
 * @param service_data  optional payload
 * @param data_length   bytes in `service_data`
 */
Std_ReturnType Xcp_SendServiceRequest(uint8 service_code, const uint8 *service_data, uint8 data_length);

/**
 * @brief Emit `EV_DAQ_OVERLOAD` with the list that overflowed.
 * @param daq_list_no   list index; `0xFFFF` reports a generic overload.
 */
Std_ReturnType Xcp_SendDaqOverloadEvent(uint16 daq_list_no);

/**
 * @brief Emit `EV_SESSION_TERMINATED` before the transport closes.
 *        Useful when you detect the connection drop locally — afterwards call
 *        `Xcp_SessionClose` (or `Xcp_SoAdConnectionClosed` on Ethernet).
 */
Std_ReturnType Xcp_SendSessionTerminatedEvent(void);

/** @brief Emit `EV_SLEEP`. */
Std_ReturnType Xcp_SendSleepEvent(void);

/** @brief Emit `EV_WAKE_UP`. */
Std_ReturnType Xcp_SendWakeUpEvent(void);

#if (XCP_ON_ETHERNET_ENABLED == STD_ON)

/**
 * @brief Called by the SoAd integration when the XCP socket is closed by
 *        the remote peer. Stops all DAQs and marks the session DISCONNECTED.
 * @return `TRUE` if a session was open and is now closed, `FALSE` otherwise.
 */
boolean Xcp_SoAdConnectionClosed(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
