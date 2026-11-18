# Architecture
> **中文**: [docs/architecture.zh-CN.md](architecture.zh-CN.md)

Single-channel XCP slave, layered for a clean line between protocol state,
transport, and integration.

```
   ┌──────────────────────────────────────────────────────────┐
   │                   Host application                       │
   │    Xcp_Event(n)  Xcp_SendEventPacket  Xcp_SetTransmissionMode │
   └──────────────────────────────────────────────────────────┘
                            │
   ┌──────────────────────────────────────────────────────────┐
   │                  Xcp.c (entry layer)                     │
   │   Init | MainFunction | EnqueueCommand | ProcessCommandQueue │
   │   HandleRxIndication | HandleTxConfirmation | TriggerTransmit │
   │   SendResponse | SendPendingCto | SessionOpen/Close       │
   └──────────────────────────────────────────────────────────┘
                            │
   ┌──────────────────────────────────────────────────────────┐
   │                  Xcp_Cmd.c (dispatch)                    │
   │   { cmd, min_len, resource, guard, fn } table            │
   │   only writer to Xcp_Tl_SendCto (CTO path)               │
   └──────────────────────────────────────────────────────────┘
                            │
   ┌─────────────┬───────────────┬──────────────┬─────────────┐
   │  Xcp_Std.c  │ Xcp_DaqCfg.c  │ Xcp_CalPage.c│ Xcp_FlashProg.c│
   │  STD/CAL    │ DAQ config    │ PAG commands │ PGM commands │
   │             │               │              │              │
   │  Xcp_DaqRun.c  Xcp_DaqStim.c  Xcp_SeedKey.c  Xcp_Resume.c  │
   │  DAQ run   │  STIM receive │  Seed & Key │  RESUME+NvM   │
   └─────────────┴───────────────┴──────────────┴─────────────┘
                            │
   ┌──────────────────────────────────────────────────────────┐
   │      XcpOnCan / XcpOnFr / XcpOnEth / XcpOnCdd            │
   │   TL shells + frame pack for FR/ETH                      │
   └──────────────────────────────────────────────────────────┘
                            │
   ┌──────────────────────────────────────────────────────────┐
   │              Host integration (your code)                │
   │   CanIf_Transmit | FrIf_Transmit | SoAd_IfTransmit        │
   │   callouts (seed/key, flash, checksum, ecu reset)         │
   └──────────────────────────────────────────────────────────┘
```

## State ownership

`Xcp_Channel` is the single object passed to internal functions:

```c
struct Xcp_Channel {
    const Xcp_ConfigType  *config_ptr;   /* generated const config    */
    Xcp_StorageType       *storage_ptr;  /* DTO/queue/segment buffers */
    Xcp_RuntimeType       *runtime_ptr;  /* session + protocol state  */
};
extern Xcp_Channel g_xcp_channel;
```

- **`g_xcp_channel`** is the only module-level singleton. Everything else hangs
  off `storage`/`runtime`/`config` so re-init is a `memset` + re-bind.
- **`Xcp_TxPipe`** (in `Xcp_RuntimeType`) — one TX slot shared between CTO and
  DTO. `state` ∈ {`IDLE`, `CTO`, `DTO`}, `parked` holds one deferred CTO, and
  `dto_latch` is the watchdog flag.
- **`Xcp_ActiveDaqIndices`/`Count`** — rebuild index lists so `Xcp_ProcessDaq`
  walks only `RUNNING` lists.
- **`Xcp_SegmentRuntime[]`** — per-segment `active_page_ecu`/`active_page_xcp`.

## TX flow

```
User code (DAQ event)             Command dispatch
        │                                 │
        ▼                                 ▼
  Xcp_DtoSendOrQueue              Xcp_SendResponse
        │                                 │
        ▼                                 ▼
  state=DTO ──send──► bus   OR   state=CTO ──send──► bus
        │                                 │
        └───────►  park CTO in tx.parked ◄───────┘
                            │
              TxConfirmation releases parked → DTO queue next
```

The dispatcher is the only CTO sender. `Xcp_SendPendingCto` is called by
`Xcp_ReportDtoOverload` + `TxConfirmation` so a deferred RES/ERR eventually
goes out even when a DTO won the bus.

## Memory addressing

`mta` (32-bit) + `mta_extension` (8-bit ext) → translated pointer:

```
ext >> 4 == 0      → raw addr (no translation)
ext >> 4 == s      → segment s-1's start + active_page_xcp × page_len + addr
```

Every access goes through `Xcp_MemPtrRead`/`Xcp_MemPtrWrite` — one place that
calls `XCP_CHECK_{READ,WRITE}_ACCESS_EX` and returns `NULL` on denial.
`Xcp_ExtValid` validates the ext field up front (`SET_MTA`, `WRITE_DAQ`,
`SHORT_DOWNLOAD`, `SHORT_UPLOAD`).

## Config → code

```
Config/xcp_base.toml  +  Config/profiles/*.toml (extends)
            │
            ▼
       Tools/xcpgen/xcpgen.py
            │
            ▼
   Gen/<profile>/Xcp_Cfg.h    — all XCP_* macros + #error invariants
   Gen/<profile>/Xcp_Cfg.c    — const tables (event/segment/sector/daq)
   Gen/<profile>/Xcp_MemMap.h — placeholder
   Gen/<profile>/Xcp_IfData.a2l — master-side description file
```

`xcpgen.py` is the **only** config source. `Xcp/Xcp_Config.h` is a shim that
`#include`s `Xcp_Cfg.h`. The ratchet (`Tests/ratchet.py`) regenerates every
profile into a temp dir and byte-compares with `Gen/<profile>/` to catch drift.

## Tests

```
Tests/run_all_tests.bat     One-shot: regen → ratchet → build+run 6 variants
                            → predef → xcpgen unit → 103-variant matrix
Tests/test_main.c           ~317 asserts/variant — every command + error path
Tests/test_matrix_func.c         Feature-adaptive smoke (varies with profile)
Tests/test_predef.c         Static/predefined DAQ coverage
Tests/build_matrix.py       Cross-platform variant runner
Tests/ratchet.py            Architecture ratchet (regression caps)
```

## What is NOT in this repo (by design)

- `AUTOSAR_SWS_XCP.pdf` / `AUTOSAR_SRS_XCP.pdf` — confidential ASAM docs, removed
  before publication.
- `.claude/`, `all.txt`, `avail.txt`, `cfg.txt`, `o.txt`, `used.txt` — session
  scratch, ignored.
- Generated `Tests/*.exe` — rebuilt on demand.
