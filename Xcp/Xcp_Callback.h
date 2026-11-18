/**
 * @file Xcp_Callback.h
 * @brief Integrator callback / callout contracts for the XCP slave.
 *
 * SPDX-License-Identifier: MIT
 *
 * Two groups of symbols live here:
 *   1. `Xcp_<Lo>*` — Rx/Tx/TriggerTransmit hooks that the lower-layer
 *      (CanIf, FrIf, SoAd, or your CDD) calls into the module.
 *   2. `Xcp_*_Callout` — hooks **you** implement. `Xcp/Xcp_Callouts.c`
 *      ships stubs; replace them with your own.
 *
 * Contracts below use:
 *   `E_OK`   — callout accepted the request; module proceeds normally
 *   `E_NOT_OK` — callout refused/failed; module maps to the appropriate
 *              protocol error (`ERR_GENERIC`, `ERR_VERIFY`, `ERR_CMD_UNKNOWN`
 *              per command)
 *   `NULL`   — pointer parameters are never NULL unless documented
 *              (callouts must not NULL-check unless stated)
 */
#ifndef XCP_CALLBACK_H
#define XCP_CALLBACK_H

#include "Xcp_Config.h"
#include "ComStack_Types.h"
#include "StandardTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * Lower-layer up-calls — one block per enabled transport.
 * The lower layer calls these when a frame arrives / is confirmed /
 * when the bus driver wants the next PDU to send (TriggerTransmit).
 * ------------------------------------------------------------------ */

#if (XCP_ON_CAN_ENABLED == STD_ON)

/**
 * @brief Called by CanIf when an XCP frame arrives on `rx_pdu_id`.
 * @param pdu_info_ptr frame; `sdu_data_ptr[0]` is the XCP PID
 *        (< 0xC8 = STIM DTO, ≥ 0xC8 = CTO command).
 */
extern void Xcp_CanIfRxIndication(PduIdType rx_pdu_id,
                                   const PduInfoType* pdu_info_ptr);

/** @brief Called by CanIf when a previous `CanIf_Transmit` completed. */
extern void Xcp_CanIfTxConfirmation(PduIdType tx_pdu_id,
                                     Std_ReturnType result);

/** @brief Called by CanIf when the bus pulls the next PDU (e.g. CAN FD
 *        trigger-transmit style). Fill `pdu_info_ptr` from the buffered
 *        last CTO; return `E_NOT_OK` to cancel. */
extern Std_ReturnType Xcp_CanIfTriggerTransmit(PduIdType tx_pdu_id,
                                                 PduInfoType* pdu_info_ptr);
#endif

#if (XCP_ON_ETHERNET_ENABLED == STD_ON)

extern void Xcp_SoAdIfRxIndication(PduIdType rx_pdu_id,
                                    const PduInfoType* pdu_info_ptr);

extern void Xcp_SoAdIfTxConfirmation(PduIdType tx_pdu_id,
                                      Std_ReturnType result);

extern Std_ReturnType Xcp_SoAdIfTriggerTransmit(PduIdType tx_pdu_id,
                                                PduInfoType* pdu_info_ptr);

/** @brief Pack one XCP message into an `LEN(2)|CTR(2)|DATA` Ethernet frame.
 *        `frame_capacity` is the output buffer size; returns bytes used
 *        (0 on error). Provided for integrators that want to compose
 *        multi-message frames or test against the wire format. */
extern uint16 Xcp_EthPackMessage(const uint8 *msg, uint8 msg_len, uint8 *frame, uint16 frame_capacity);
#endif

#if (XCP_ON_FLEXRAY_ENABLED == STD_ON)

extern void Xcp_FrIfRxIndication(PduIdType rx_pdu_id,
                                  const PduInfoType* pdu_info_ptr);

extern void Xcp_FrIfTxConfirmation(PduIdType tx_pdu_id,
                                    Std_ReturnType result);

extern Std_ReturnType Xcp_FrIfTriggerTransmit(PduIdType tx_pdu_id,
                                                PduInfoType* pdu_info_ptr);
#endif

#if (XCP_ON_CDD_ENABLED == STD_ON)

/**
 * @brief Called by your CDD when an XCP frame arrives.
 *        Function names are generated via `XcpCddRxIndicationName`,
 *        `XcpCddTxConfirmationName`, `XcpCddTriggerTransmitName` config —
 *        emitted into `Xcp_Cfg.h` as `XCP_CDD_*_NAME` macros.
 */
extern void XCP_CDD_RX_INDICATION_NAME(PduIdType rx_pdu_id,
                                       const PduInfoType* pdu_info_ptr);

extern void XCP_CDD_TX_CONFIRMATION_NAME(PduIdType tx_pdu_id,
                                         Std_ReturnType result);

extern Std_ReturnType XCP_CDD_TRIGGER_TRANSMIT_NAME(PduIdType tx_pdu_id,
                                                    PduInfoType* pdu_info_ptr);
#endif

/* ------------------------------------------------------------------
 * Integration callouts — implement in your own code.
 * Stubs in Xcp/Xcp_Callouts.c are provided so the module builds and
 * links; replace them before shipping.
 * ------------------------------------------------------------------ */

/**
 * @brief Compute a checksum over `[addr, addr+len)` in your address space.
 * @param addr        absolute (translated) address
 * @param len         bytes
 * @param checksum    out — the computed value (u8/u16/u32 by `checksumLen`)
 * @param checksumLen out — 1, 2, or 4 (anything else → `ERR_OUT_OF_RANGE`)
 * @return `E_OK` → response carries `checksum_type = checksumLen`
 */
extern Std_ReturnType Xcp_BuildChecksum_Callout(uint32 addr, uint32 len, uint32* checksum, uint8* checksumLen);

/**
 * @brief USER_CMD handler — the raw command bytes.
 * @return `E_OK` → you respond yourself (e.g. via `Xcp_SendResponse`);
 *         `E_NOT_OK` → slave emits `ERR_CMD_UNKNOWN`.
 */
extern Std_ReturnType Xcp_UserCommand_Callout(const uint8* cmd_data, uint8 cmd_length);

#if (XCP_SEED_KEY_ENABLED == STD_ON)

/**
 * @brief Produce the unlock seed for `resource`.
 * @param resource    `XCP_RESOURCE_*` constant
 * @param seed        out buffer, `XCP_MAX_KEY_LENGTH` bytes
 * @param seed_length out — actual bytes produced (≤ `XCP_MAX_KEY_LENGTH`;
 *                    if > `XCP_MAX_CTO - 2`, `GET_SEED mode=1` continues it)
 */
extern void Xcp_GetSeed_Callout(uint8 resource, uint8 *seed, uint8 *seed_length);

/**
 * @brief Verify an UNLOCK key against the resource's expected algorithm.
 * @return `TRUE` → protection bit for `resource` is cleared;
 *         `FALSE` → `ERR_ACCESS_LOCKED` + session disconnect (ASAM semantics).
 */
extern boolean Xcp_VerifyKey_Callout(uint8 resource, const uint8 *key, uint8 key_length);
#endif

#if (XCP_CALIBRATION_PAGE_ENABLED == STD_ON)

/**
 * @brief Copy page `src_page` of segment `src_segment` into `dst_page` of
 *        `dst_segment`.
 * @return `E_OK` on success → `RES`; `E_NOT_OK` → `ERR_GENERIC`.
 */
extern Std_ReturnType Xcp_CopyCalPage_Callout(uint8 src_segment, uint8 src_page, uint8 dst_segment, uint8 dst_page);
#endif

#if (XCP_FLASH_PROGRAMMING_ENABLED == STD_ON)

/** @brief Erase `[address, address+length)` — sector range pre-validated. */
extern Std_ReturnType Xcp_FlashErase_Callout(uint32 address, uint32 length);

/** @brief Program `length` bytes at `address` — range pre-validated. */
extern Std_ReturnType Xcp_FlashProgram_Callout(uint32 address, const uint8 *data, uint32 length);

/** @brief Verify `[address, address+length)` against `expected_value`. */
extern Std_ReturnType Xcp_FlashVerify_Callout(uint32 address, uint32 length, uint32 expected_value);

/**
 * @brief Schedule a CPU reset.
 * @warning Called **before** the `PROGRAM_RESET` response is sent. Do not
 *          reset immediately — queue the reset for after the transmit
 *          completes (e.g. via `EcuM`/`Os` post-task).
 */
extern void Xcp_EcuReset_Callout(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
