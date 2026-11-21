# Changelog
> **中文**: [CHANGELOG.zh-CN.md](CHANGELOG.zh-CN.md)

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Host regression checks for every fix below; each was confirmed to fail
  against the unfixed sources
- pyxcp `push()` end-to-end test covering a multi-block download

### Fixed
- Block download follows the declared length: byte 1 of `DOWNLOAD` is the
  total block length and byte 1 of `DOWNLOAD_NEXT` the remaining length.
  The slave stays silent on intermediate frames, answers only the frame
  that exhausts the block and rejects overruns with `ERR_SEQUENCE`. A
  plain `DOWNLOAD` and `DOWNLOAD_MAX` are single-frame transfers and do
  not open a block; a denied frame leaves the block budget untouched, and
  a longer declaration on a build without block transfer is rejected
  before anything is written
- `MODIFY_BITS` performs the 32-bit read-modify-write byte-wise, so an
  unaligned `mta` no longer faults on Cortex-M0/M0+ or `UNALIGN_TRP`
  builds; the discarded read-pointer check is folded into the single
  write-access check
- Every `tx.state` transition (`Xcp_SendResponse`, `Xcp_SendPendingCto`,
  `Xcp_HandleTxConfirmation`, `Xcp_DtoSendNext`) takes the DTO critical
  section, and `Xcp_ReportDtoOverload` transmits with the lock released —
  no lower-layer call runs inside the region any more
- `Xcp_Init` primes `os_last_tick`, so the first timestamp no longer
  carries the full uptime
- The event scheduler keeps `event_channel_counter` below the cycle
  instead of growing without bound when the cycle is shorter than the
  main-function period
- `Xcp_Init` resets the calibration-page selection (`Xcp_InitCalPage`
  was an empty stub)
- The shipped event-channel configuration reports ODT consistency only;
  DAQ/EVENT consistency was advertised without being implemented
- DAQ configuration is frozen while any list runs: `FREE_DAQ`,
  `CLEAR_DAQ_LIST` and `ALLOC_ODT`/`ALLOC_ODT_ENTRY` return
  `ERR_DAQ_ACTIVE`, and starting an unconfigured list returns
  `ERR_DAQ_CONFIG`, so a running list's absolute PID can no longer shift
  mid-stream; `SET_DAQ_LIST_MODE` on a live list still tunes the
  prescaler but rejects moving the list to another event channel,
  flipping its direction or toggling its timestamp bit, all of which
  would reshape the running stream
- Block mode is usable out of the box: `xcpgen` rejects
  `XcpBlockTransferEnabled=1` with `XcpMaxBs=0`, and the defaults are
  `MAX_BS=4` and `MIN_ST=1 ms` (one main-loop cycle per block frame)
- A DTO is transmitted at the length of its ODT (identification field,
  optional timestamp and entries), not padded to `XCP_MAX_DTO`. The queue
  stores that length and both drain paths use it. `XcpDtoQueueDepth` must
  be at least `XcpOdtCount`, because a busy pipe queues the whole burst

## [0.3.2] — 2026-11-20

### Added
- Host tests for `PROGRAM_*` argument, declared-size, callout-failure and
  sector-info error paths; `Xcp_FlashProg.c` file coverage is 95.4%

### Changed
- CI quality-tool installation retries apt downloads, bounds network and lock
  waits, and fails within 10 minutes instead of hanging

## [0.3.1] — 2026-11-20

### Added
- Block-UPLOAD performance benchmark: 252 elements per request, `upload_block`
  JSON results and a check that it uses fewer requests than plain UPLOAD
- pyxcp coverage for block UPLOAD while DAQ is running
- Advisory MISRA report from cppcheck in `Tests/quality.sh`; CI uploads it as
  an artifact. It is not a compliance gate.

### Changed
- GitHub Actions for checkout, setup-python and upload-artifact updated to
  current Node 24 releases
- Windows CI uses the MSYS2 UCRT64 toolchain instead of deprecated MINGW64

### Fixed
- Starting a block UPLOAD now cancels any block transfer left active by a
  previous download, instead of returning `ERR_SEQUENCE`

## [0.3.0] — 2026-11-20

### Added
- `Tests/quality.sh`: clang host + 103-variant matrix, ASan/UBSan, gcovr
  coverage gate (line ≥80%, functions ≥90%), Cortex-M0/M4 ARM cross-compile
  and cppcheck static analysis
- CI `quality` job runs the full gate set on Ubuntu; coverage XML/HTML is
  uploaded as an artifact
- Slave block upload: one `UPLOAD` with more than `MAX_CTO-1` elements streams
  multiple `RES` packets when block transfer is enabled; CONNECT advertises
  `SLAVE_BLOCK_MODE`

### Changed
- Ratchet ceilings tightened: `g_xcp_channel` 177 → 172 and
  `xcp_preprocessor` 171 → 167

## [0.2.0] — 2026-11-20

### Added
- Open-source readiness: MIT LICENSE, README, CONTRIBUTING, CODE_OF_CONDUCT,
  SECURITY, .github CI + templates, .editorconfig, .gitattributes,
  .clang-format, CHANGELOG
- `docs/` — architecture, porting guide, feature matrix (English)
- `examples/minimal/` — host-buildable bring-up example
- Portability hooks `XCP_MEM_PTR`/`XCP_MEM_ADDR`; host tests now register
  objects in a mapped 32-bit address space, independent of PIE, ASLR and the
  host pointer width
- pyxcp end-to-end harness in `Tests/integration/` — the slave is built as a
  shared library and exchanges real XCPonCAN frames over a `python-can`
  virtual bus
- Functional suite covering the full command surface, the pre-unlock
  protection matrix, seed/key for every resource, calibration pages, dynamic
  DAQ/STIM, flash sequencing and negative paths
- Feature-variant suite for minimal, seed-key-off, static, predefined,
  interleaved and 64-byte CAN-FD profiles
- Performance suite for CTO latency, bulk upload/download, maximum DAQ/STIM
  configurations and flash programming, with a JSON report
- `docs/testing.md` and `docs/performance.md`, with Chinese mirrors
- CI runs the pyxcp smoke, functional and variant suites on Ubuntu and
  Windows and uploads the performance report as an artifact
- `XCP_DTO_QUEUE_DEPTH` config knob (decoupled from `XCP_MAX_ODT`)
- `Xcp_ExtValid` for `SET_MTA` address-extension validation
- `Xcp_EventChannelIdx` — event_channel_number → config index lookup

### Fixed
- `SET_REQUEST`: `STORE_DAQ_NO_RESUME` (bit1) is now a legal combination
  (stores DAQ but skips RESUME); previously rejected as `ERR_OUT_OF_RANGE`
- `GET_SEED mode=1` — multi-frame continuation for seeds > `XCP_MAX_CTO-2`
- `DOWNLOAD_NEXT` returns `ERR_CMD_UNKNOWN` when
  `XCP_BLOCK_TRANSFER_ENABLED == STD_OFF`
- `BUILD_CHECKSUM` now translates `mta` via `Xcp_MemTranslate` before callout
- `START_STOP_SYNCH` mode semantics: 0=stop_all, 1=start_selected,
  2=stop_selected
- `GET_PAGE_INFO` byte 2 returns `ecu_page_properties` (was `segment_number`)
- `GET_SEGMENT_INFO` mode 2 returns `(ext, page_start)` for the requested page
- `START_STOP_DAQ_LIST` FIRST_PID returns the list number under relative PID
  modes
- `PROGRAM_PREPARE`-declared size is enforced on `PROGRAM`/`NEXT`/`MAX`
- `WRITE_DAQ` size validated against `XCP_ODT_ENTRY_SIZE_{DAQ,STIM}`
- `Xcp_Tl_SendDto` moved out of the SchM critical region
- `SessionClose` clears per-session runtime state (`mta`, `seed_*`, `pgm_*`)
- `FrIf_ReconfigLPdu` now passes `XCP_FR_MAX_FRAME` (header included)
- Static/extern TX + counter state moved into `Xcp_StorageType`/`Xcp_RuntimeType`
- `Xcp_ProcessStim` dead code removed (STIM is RX-push, not event-polled)
- Resource protection: `XCP_RESOURCE_DAQ`/`XCP_RESOURCE_STIM` now gate the
  DAQ-config and STIM-write paths
- `SessionOpen` preserves `STORE_CAL_REQ`/`CLEAR_DAQ_REQ` bits for master
  read-back
- `GET_DAQ_CLOCK` returns `ts_size`-aware length and `ERR_CMD_UNKNOWN` when
  `XCP_TIMESTAMP_TYPE == NO_TIME_STAMP`
- `PGM_PROPERTIES` no longer advertises `NON_SEQ_PGM_SUPPORTED` (not implemented)

### Removed
- `Xcp_ProcessStim` (dead code — STIM is RX-push)
- Write-only `block_sequence_counter`/`expected_block_sequence` fields
- Non-standard `TIMESTAMP_UNIT_{1,10,100}PS` values
- AUTOSAR confidential spec documents (`AUTOSAR_SWS_XCP.pdf`, `AUTOSAR_SRS_XCP.pdf`,
  `srs.txt`, `sws.txt`)
- `Docs/xcp-kernel.html` (internal architecture scratch)
- Scratch analysis files at repo root

## [0.1.0] — 2026-11-18

First public release. Full STD + CAL + PAG + DAQ + STIM + PGM + Seed&Key +
RESUME command coverage per ASAM XCP Part 2 v1.1 on CAN / FlexRay / Ethernet /
CDD transports.
