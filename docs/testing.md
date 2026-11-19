# Testing
> **中文**: [docs/testing.zh-CN.md](testing.zh-CN.md)

The suite is layered so that a failure points at one kind of defect: protocol
logic, configuration generation, feature gating, transport behaviour, or
throughput. CI is the source of truth for pass/fail; this document describes
what each layer covers and how to reproduce it.

## Layers

| Layer | Entry point | Purpose |
|------|-------------|---------|
| Host asserts | `Tests/test_main.c` | Every implemented command, error path and memory edge case, compiled per functional variant |
| Feature matrix | `Tests/build_matrix.py` | 103 transport/profile/feature combinations, compiled with `-Werror` |
| Predefined DAQ | `Tests/test_predef.c` | Static and predefined DAQ list configuration |
| Architecture ratchet | `Tests/ratchet.py` | Forbids regressions in global-symbol use, preprocessor complexity, generated-file drift |
| Generator tests | `Tests/test_xcpgen.py` | `xcpgen` validation, profile expansion and output determinism |
| pyxcp smoke | `Tests/integration/test_pyxcp_master.py` | Connect, identify, unlock, memory access, one DAQ list |
| pyxcp functional | `Tests/integration/test_pyxcp_functional.py` | Full command surface, protection matrix, negative paths |
| pyxcp variants | `Tests/integration/test_pyxcp_variants.py` | Disabled-feature behaviour, static/predefined DAQ, interleaved mode, CAN-FD limits |
| pyxcp performance | `Tests/integration/test_pyxcp_perf.py` | CTO latency, bulk transfer, DAQ/STIM throughput, flash |

## Host suite

```bash
# Linux/macOS
python3 Tests/build_matrix.py

# Windows
Tests\run_all_tests.bat

# Architecture and generator checks
python3 Tests/ratchet.py --no-gen
python3 Tests/test_xcpgen.py
```

Host tests do not treat raw host pointers as XCP addresses. Test objects are
registered in a mapped 32-bit address space (`Tests/test_stubs.c`), so PIE,
ASLR and the host pointer width do not leak into protocol addresses. The same
map is used by the pyxcp bridge.

## pyxcp end-to-end suite

The slave runtime is compiled as a shared library and bridged to a
`python-can` `virtual` bus. pyxcp exchanges real XCPonCAN frames with the C
code through the normal transport callbacks; no CAN hardware is required.

```bash
python3 -m pip install -r Tests/integration/requirements.txt

python3 Tests/integration/test_pyxcp_master.py      # smoke
python3 Tests/integration/test_pyxcp_functional.py  # command + error matrix
python3 Tests/integration/test_pyxcp_variants.py    # feature-gated profiles
python3 Tests/integration/test_pyxcp_perf.py        # performance envelope
```

### Functional coverage

`test_pyxcp_functional.py` uses the default CAN profile and asserts:

- connection, status, identity, `GET_COMM_MODE_INFO`, every `GET_ID` mode,
  `SYNCH`, `TL_CMD` and `USER_CMD`
- the pre-unlock protection matrix for CAL, DAQ, STIM and PGM, including
  `ERR_ACCESS_LOCKED`, then seed/key patterns and unlock bits for every
  resource
- memory access: `SET_MTA`, `UPLOAD`, `SHORT_UPLOAD`, `DOWNLOAD`,
  `DOWNLOAD_NEXT`, `DOWNLOAD_MAX`, `SHORT_DOWNLOAD`, `MODIFY_BITS`,
  `BUILD_CHECKSUM`, access-denied, out-of-range and truncated CTO frames
- calibration pages: processor/segment/page info, page access modes, segment
  mode, page copy and invalid page/segment paths
- dynamic DAQ: maximum allocation, event channels, prescaler, timestamp,
  resume, synchronous start/stop, `READ_DAQ` and live DTO values; STIM writes
- flash programming: prepare, format, clear, program, next, max, verify,
  reset, sequencing errors, size limits and verify failures
- resume requests, malformed command lengths and invalid connection modes

Classic CAN has an 8-byte CTO, so payload-carrying `SHORT_DOWNLOAD` cannot be
transported; the functional suite checks the zero-payload form and the
declared-length overflow diagnostic. The CAN-FD variant below covers payload
transfer.

### Feature variants

| Variant | Build | Asserts |
|---------|-------|---------|
| `minimal` | DAQ, seed/key, CAL, PGM, block transfer and resume off | disabled commands return `ERR_CMD_UNKNOWN`; plain memory, ID and SYNCH still work |
| `seedkey_off` | `XcpSeedKeyEnabled=0` | resources are unprotected; CAL/DAQ/PGM commands work without unlock |
| `daq_static` | `XcpDaqConfigType=DAQ_STATIC`, `XcpMinDaq=1` | static list visible without allocation; `ALLOC_DAQ`/`ALLOC_ODT` rejected |
| `daq_predef` | `XcpMinDaq=2` | predefined lists, write protection, pool overflow, start/stop |
| `interleaved` | `XcpInterleavedModeEnabled=1` | interleaved mode advertised and command channel still usable |
| `can_fd` | 64-byte CTO/DTO, 15 ODT entries | payload `SHORT_DOWNLOAD`, 60/62-byte transfers and checksum |

### Performance

The performance suite runs a classic-CAN build and a CAN-FD build and reports
latency, bulk throughput, maximum DAQ/STIM configurations, and flash
programming. The virtual bus does not model CAN bit timing; method, reference
numbers and interpretation are in [docs/performance.md](performance.md).

## CI

`.github/workflows/ci.yml` runs on Ubuntu and Windows:

- ratchet, profile regeneration, xcpgen tests
- host CAN build and the 103-variant matrix
- pyxcp smoke, functional and feature-variant suites
- pyxcp performance at `XCP_PERF_SCALE=0.5`; the JSON report is uploaded as a
  build artifact

Live status is the CI badge in the README. Pass counts and timings are
deliberately not pinned in documentation because they change with every
commit; consult the workflow run for the authoritative result.
