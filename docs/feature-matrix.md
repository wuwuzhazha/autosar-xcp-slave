# XCP Feature Matrix
> **中文**: [docs/feature-matrix.zh-CN.md](feature-matrix.zh-CN.md)

Coverage against ASAM XCP Part 2 v1.1 commands. `✓` = implemented + covered by the
host test suite (`Tests/test_main.c`, `Tests/test_matrix_func.c`,
`Tests/test_predef.c`). End-to-end coverage over a real XCP transport (pyxcp) is
described in [docs/testing.md](testing.md).

## Standard (STD)

| Code | Command | Status |
|------|---------|--------|
| 0xFF | CONNECT | ✓ |
| 0xFE | DISCONNECT | ✓ |
| 0xFD | GET_STATUS | ✓ |
| 0xFC | SYNCH | ✓ |
| 0xFB | GET_COMM_MODE_INFO | ✓ |
| 0xFA | GET_ID (type 0) | ✓ |
| 0xF9 | SET_REQUEST | ✓ |
| 0xF8 | GET_SEED (mode 0 first / mode 1 continue) | ✓ |
| 0xF7 | UNLOCK | ✓ |
| 0xF6 | SET_MTA | ✓ |
| 0xF5 | UPLOAD | ✓ |
| 0xF4 | SHORT_UPLOAD | ✓ |
| 0xF3 | BUILD_CHECKSUM | ✓ |
| 0xF2 | TRANSPORT_LAYER_CMD (CAN GET_SLAVE_ID) | ✓ |
| 0xF1 | USER_CMD (callout) | ✓ |
| 0xF0 | DOWNLOAD | ✓ |
| 0xEF | DOWNLOAD_NEXT | ✓ |
| 0xEE | DOWNLOAD_MAX | ✓ |
| 0xED | SHORT_DOWNLOAD | ✓ |
| 0xEC | MODIFY_BITS | ✓ |

## Calibration pages (PAG)

| Code | Command | Status |
|------|---------|--------|
| 0xEB | SET_CAL_PAGE | ✓ |
| 0xEA | GET_CAL_PAGE | ✓ |
| 0xE9 | GET_PAG_PROCESSOR_INFO | ✓ |
| 0xE8 | GET_SEGMENT_INFO (mode 0 basic, mode 1 pages, mode 2 mapping) | ✓ |
| 0xE7 | GET_PAGE_INFO | ✓ |
| 0xE6 | SET_SEGMENT_MODE | ✓ |
| 0xE5 | GET_SEGMENT_MODE | ✓ |
| 0xE4 | COPY_CAL_PAGE (callout) | ✓ |

## DAQ / STIM

| Code | Command | Status |
|------|---------|--------|
| 0xE3 | CLEAR_DAQ_LIST | ✓ |
| 0xE2 | SET_DAQ_PTR | ✓ |
| 0xE1 | WRITE_DAQ | ✓ |
| 0xE0 | SET_DAQ_LIST_MODE | ✓ |
| 0xDF | GET_DAQ_LIST_MODE | ✓ |
| 0xDE | START_STOP_DAQ_LIST (mode 0/1/2 = stop/start/select + FIRST_PID) | ✓ |
| 0xDD | START_STOP_SYNCH (mode 0 stop-all, 1 start-selected, 2 stop-selected) | ✓ |
| 0xDC | GET_DAQ_CLOCK (timestamp per `XCP_TIMESTAMP_TYPE`) | ✓ |
| 0xDB | READ_DAQ | ✓ |
| 0xDA | GET_DAQ_PROCESSOR_INFO | ✓ |
| 0xD9 | GET_DAQ_RESOLUTION_INFO | ✓ |
| 0xD8 | GET_DAQ_LIST_INFO | ✓ |
| 0xD7 | GET_DAQ_EVENT_INFO (by `event_channel_number`) | ✓ |
| 0xD6 | FREE_DAQ | ✓ |
| 0xD5 | ALLOC_DAQ | ✓ |
| 0xD4 | ALLOC_ODT | ✓ |
| 0xD3 | ALLOC_ODT_ENTRY | ✓ |

## Flash programming (PGM)

| Code | Command | Status |
|------|---------|--------|
| 0xD2 | PROGRAM_START | ✓ |
| 0xD1 | PROGRAM_CLEAR | ✓ |
| 0xD0 | PROGRAM | ✓ |
| 0xCF | PROGRAM_RESET | ✓ |
| 0xCE | GET_PGM_PROCESSOR_INFO | ✓ |
| 0xCD | GET_SECTOR_INFO (mode 0 addr, mode 1 len, mode 2 name=0) | ✓ |
| 0xCC | PROGRAM_PREPARE (enforces declared size) | ✓ |
| 0xCB | PROGRAM_FORMAT | ✓ |
| 0xCA | PROGRAM_NEXT | ✓ |
| 0xC9 | PROGRAM_MAX | ✓ |
| 0xC8 | PROGRAM_VERIFY | ✓ |

## Events / service packets

| Code | Name | Emitted by |
|------|------|------------|
| 0x00 | EV_RESUME_MODE | `Xcp_RestoreDaqConfig` |
| 0x01 | EV_CLEAR_DAQ | `Xcp_ClearStoredDaqConfig` (planned) |
| 0x02 | EV_STORE_DAQ | `Xcp_NvMWriteRamBlockToNvmCallback` |
| 0x03 | EV_STORE_CAL | `Xcp_SendEventPacket` (integrator) |
| 0x05 | EV_CMD_PENDING | dispatcher when needed |
| 0x06 | EV_DAQ_OVERLOAD | `Xcp_ReportDtoOverload` |
| 0x07 | EV_SESSION_TERMINATED | `Xcp_SendSessionTerminatedEvent` |
| 0x08 | EV_TIME_SYNC | `Xcp_SendEventPacket` (integrator) |
| 0x09 | EV_STIM_TIMEOUT | `Xcp_CheckStimTimeout` |
| 0x0A | EV_SLEEP | `Xcp_SendSleepEvent` |
| 0x0B | EV_WAKE_UP | `Xcp_SendWakeUpEvent` |
| 0xFE/0xFF | EV_USER / EV_TRANSPORT | `Xcp_SendEventPacket` |

## Error codes emitted

`ERR_CMD_SYNCH`, `ERR_CMD_BUSY`, `ERR_DAQ_ACTIVE`, `ERR_PGM_ACTIVE`,
`ERR_CMD_UNKNOWN`, `ERR_CMD_SYNTAX`, `ERR_OUT_OF_RANGE`, `ERR_ACCESS_DENIED`,
`ERR_ACCESS_LOCKED`, `ERR_PAGE_NOT_VALID`, `ERR_MODE_NOT_VALID`,
`ERR_SEGMENT_NOT_VALID`, `ERR_SEQUENCE`, `ERR_DAQ_CONFIG`,
`ERR_MEMORY_OVERFLOW`, `ERR_GENERIC`, `ERR_VERIFY`,
`ERR_RESOURCE_TEMP_NOT_ACC`, `ERR_WRITE_PROTECTED`.

## Declared non-goals

- **Slave block upload** (multi-frame `UPLOAD` continuation) — not implemented;
  CONNECT advertises `SLAVE_BLOCK_MODE = 0`. `GET_COMM_MODE_INFO` advertises
  `MASTER_BLOCK_MODE = 1` (download path only).
- **Multi-channel** — single `Xcp_Channel` by design.
- **A2L generation at runtime** — `Tools/xcpgen` emits `Gen/<profile>/Xcp_IfData.a2l`
  at generation time (offline, not on the wire).
- **SWS_00716 performance parameters** — master-side concern.

## Transport layers

| Link | Implementation |
|---|---|
| CAN | `XcpOnCan.c` — CanIf_Transmit + GET_SLAVE_ID `0xFF` |
| FlexRay | `XcpOnFr.c` — `LEN(2)|CTR(1)|DATA|FILL` framing, `FrIf_ReconfigLPdu` at init |
| Ethernet | `XcpOnEth.c` — `LEN(2)|CTR(2)|DATA` framing, `Xcp_SoAdConnectionClosed` |
| CDD | `XcpOnCdd.c` — names via `XcpCdd*Name` config |
