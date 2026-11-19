# Changelog
> **中文**: [CHANGELOG.zh-CN.md](CHANGELOG.zh-CN.md)

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
