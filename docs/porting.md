# Porting / Integration Guide
> **中文**: [docs/porting.zh-CN.md](porting.zh-CN.md)

What you must supply to drop `Xcp/` into an ECU. Everything outside `Xcp/` is
replaceable — the core only touches the host through **callouts** and **transport
hooks**.

## Required callouts — `Xcp/Xcp_Callback.h`

| Hook | Purpose |
|---|---|
| `Xcp_GetSeed_Callout(resource, seed, *len)` | Return unlock seed for `resource` |
| `Xcp_VerifyKey_Callout(resource, key, len)` | Verify unlock key (return `TRUE`/`FALSE`) |
| `Xcp_CopyCalPage_Callout(src_seg, src_page, dst_seg, dst_page)` | Copy a cal page |
| `Xcp_BuildChecksum_Callout(addr, size, *csum, *csum_len)` | Checksum over memory block |
| `Xcp_FlashErase_Callout(addr, len)` | Erase flash |
| `Xcp_FlashProgram_Callout(addr, data, len)` | Program flash |
| `Xcp_FlashVerify_Callout(addr, len, expected)` | Verify |
| `Xcp_EcuReset_Callout()` | Schedule MCU reset — **must not return until response is sent** |
| `Xcp_GetSeed_Callout` / `Xcp_VerifyKey_Callout` | See Seed & Key section |

`Xcp_Callouts.c` ships stub implementations — replace them with your own.

## Transport send

Pick **one** at config time (`Xcp_LinkType`):

| Link | Function to call on TX |
|---|---|
| CAN | `CanIf_Transmit(pdu_id, &pdu)` |
| FlexRay | `FrIf_Transmit(pdu_id, &pdu)` |
| Ethernet | `SoAd_IfTransmit(pdu_id, &pdu)` |
| CDD | Your own name via `XcpCddTransmitName` config — the generated `Xcp_Cfg.h` emits `XCP_CDD_TRANSMIT_NAME` |

If you bring your own lower layer, implement `Xcp_Tl_SendCto` / `Xcp_Tl_SendDto`
instead and compile out the `XcpOn*.c` files you don't need.

## Memory access

`Xcp_MemTranslate` / `Xcp_MemPtrRead` / `Xcp_MemPtrWrite` resolve `mta`+`ext` to a
physical pointer. Your `XCP_CHECK_{READ,WRITE}_ACCESS[_EX]` macros decide if a range
is legal — by default the generated `Xcp_Cfg.h` allows everything; **production
builds must gate them** (RAM bounds, cal-window only, etc.).

`ext` semantics (single-`Xcp_MemTranslate`):
- `ext == 0` → `addr` is a raw pointer
- `ext >> 4 == s` (s = 1..XCP_MAX_SEGMENT) → `addr` is an **offset into segment
  `s-1`** at the currently active XCP page

## DAQ runtime

- `Xcp_MainFunction` drives event-channel scheduling: each `event_channel` has a
  `time_cycle` × `time_unit` tick converted to milliseconds and compared against
  `XCP_MAIN_FUNCTION_PERIOD_MS`.
- `Xcp_Event(n)` is the manual trigger — call it from your app when the event
  actually fires (e.g. "new cam sync"), or rely on the periodic scheduler.
- `Xcp_DtoSendOrQueue` is the **single** DTO egress — never call `Xcp_Tl_SendDto`
  yourself; always go through it so the queue + busy-watchdog + overload event work.

## Cal Page segments

`xcp_segment_config` rows in `Config/xcp_base.toml` map `ext`-address spaces.
`ext >> 4` picks the row; `active_page_xcp` is multiplied into the page offset.

## RESUME / NvM

If `XcpResumeModeEnabled`:
- `NvM_ReadBlock` / `NvM_WriteBlock` / `NvM_InvalidateNvBlock` must exist.
- Block id is `XCP_NVM_BLOCK_ID_RESUME` (default 10, configurable).
- Data layout is `Xcp_ResumeDataType` (magic, version, checksum, per-DAQ ODT entries).

## Threads / critical sections

`XCP_DTO_ENTER_CRITICAL` / `XCP_DTO_EXIT_CRITICAL` guard the DTO queue + TX state.
Default maps to `SchM_Enter_Xcp/SchM_Exit_Xcp(XCP_EXCLUSIVE_AREA_0)` — override via
`Xcp_Cfg.h` or define them to your own locking primitive.

`Xcp_Tl_SendCto`/`SendDto` are **outside** the critical region (calls may be slow).

## Session lifecycle

- `Xcp_Init(&xcp_config)` — call once at startup (or to reset).
- `Xcp_SessionOpen` / `Xcp_SessionClose` — called by `CONNECT`/`DISCONNECT`; you can
  also call them directly when the socket opens/closes (see `Xcp_SoAdConnectionClosed`
  for the Ethernet case).
- `Xcp_HandleRxIndication(pdu)` — feed every received frame here; PID ≥ 0xC0 is a
  command, PID < 0xC0 is a STIM DTO.
- `Xcp_HandleTxConfirmation(result)` — call when the lower layer confirms a TX.
- `Xcp_HandleTriggerTransmit(pdu)` — call when the lower layer pulls data.

## Configuration reference

All knobs live in `Tools/xcpgen/xcpgen.py`'s `SCHEMA` table (~54 ECUC + vendor
parameters). Read `Config/xcp_base.toml` for the canonical profile plus the
`Config/profiles/*.toml` overrides that show the override syntax.

Useful checks:
```bash
python Tools/xcpgen/xcpgen.py --config Config/xcp_base.toml --check     # validate
python Tools/xcpgen/xcpgen.py --config Config/xcp_base.toml --show      # effective
python Tools/xcpgen/xcpgen.py --diff Config/profiles/can.toml           # vs base
python Tools/xcpgen/xcpserve.py                                       # web UI
```

## MISRA / style

- C99, `-Wall -Wextra -Werror=implicit-function-declaration` clean
- No dynamic allocation; all state in `Xcp_StorageType` + `Xcp_RuntimeType` +
  `g_xcp_channel`
- `Xcp_Channel` carries `config_ptr`/`storage_ptr`/`runtime_ptr` — internal functions
  take it explicitly (no hidden globals beyond `g_xcp_channel` itself)

## Test suite

- `Tests/run_all_tests.bat` — Windows: regen + ratchet + 6 functional variants +
  predef + xcpgen unit + matrix
- `Tests/build_matrix.py` — cross-platform matrix runner (103 variants × compile +
  run)
- `Tests/ratchet.py --tighten --no-gen` — update ratchet baseline after a cleanup

## Gotchas

- **`Xcp_EcuReset_Callout` must not reset immediately.** `PROGRAM_RESET` sends its
  response *after* the callout returns — delay the reset (e.g. `NvM`/`EcuM` post-task).
- `Xcp_DtoSendOrQueue` keeps queue ≤ `XCP_DTO_QUEUE_DEPTH` — drops fire the
  `EV_DAQ_OVERLOAD` event with a cooldown.
- `GET_DAQ_CLOCK` answers with `timestamp` sized by `XCP_TIMESTAMP_TYPE` — verify
  your `Xcp_GetTimestamp` source (`OS` counter vs software tick) matches
  `XcpTimestampSourceOs`.
