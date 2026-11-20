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
| Quality gates | `Tests/quality.sh` | clang, ASan/UBSan, coverage, ARM cross-compile, cppcheck |

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
- memory access: `SET_MTA`, `UPLOAD` (including slave block upload for
  multi-frame reads), `SHORT_UPLOAD`, `DOWNLOAD`, `DOWNLOAD_NEXT`,
  `DOWNLOAD_MAX`, `SHORT_DOWNLOAD`, `MODIFY_BITS`, `BUILD_CHECKSUM`,
  access-denied, out-of-range and truncated CTO frames
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
| `minimal` | DAQ, seed/key, CAL, PGM, block transfer and resume off | disabled commands return `ERR_CMD_UNKNOWN`; no `SLAVE_BLOCK_MODE`; plain memory, ID and SYNCH still work |
| `seedkey_off` | `XcpSeedKeyEnabled=0` | resources are unprotected; CAL/DAQ/PGM commands work without unlock |
| `daq_static` | `XcpDaqConfigType=DAQ_STATIC`, `XcpMinDaq=1` | static list visible without allocation; `ALLOC_DAQ`/`ALLOC_ODT` rejected |
| `daq_predef` | `XcpMinDaq=2` | predefined lists, write protection, pool overflow, start/stop |
| `interleaved` | `XcpInterleavedModeEnabled=1` | interleaved mode advertised and command channel still usable |
| `can_fd` | 64-byte CTO/DTO, 15 ODT entries | payload `SHORT_DOWNLOAD`, slave block upload, 60/62-byte transfers and checksum |

### What the virtual bus does not cover

pyxcp validates the master side of the wire protocol. The `python-can`
`virtual` bus does not model the ECU or the physical network, so these remain
outside the suite:

- **CAN controller and transceiver**: arbitration, bit timing, ACK, error
  frames, bus-off, CAN-FD phase switching and bus load
- **Target execution**: interrupt latency, preemption, memory barriers, stack
  depth, compiler optimisation and 32-bit alignment; the ARM gate is
  compile-only
- **Flash / NvM / lifecycle**: erase and program timing, write granularity,
  brownout, power-loss recovery, reset, watchdog and bootloader interaction
- **Commercial master quirks**: CANape/INCA optional commands, timing
  expectations and seed/key flows; pyxcp is one master implementation
- **Physical layer**: termination, cable length, ground offset, EMC,
  sleep/wake and supply voltage
- **FlexRay / Ethernet**: TDMA schedule, switch/TSN behaviour and socket
  buffering

Those require hardware-in-the-loop or a target emulator with peripheral
models; a green pyxcp run is protocol evidence, not ECU validation.

### Performance

The performance suite runs a classic-CAN build and a CAN-FD build and reports
latency, bulk throughput, maximum DAQ/STIM configurations, and flash
programming. The virtual bus does not model CAN bit timing; method, reference
numbers and interpretation are in [docs/performance.md](performance.md).

## Quality gates

`Tests/quality.sh` is the Linux quality-gate runner used by the `quality` CI
job. It builds the full host suite, not the pyxcp bridge, so coverage describes
the default CAN profile's C paths.

| Gate | Command | What it proves |
|------|---------|----------------|
| clang host + matrix | `Tests/quality.sh host`, `matrix` | C99 builds and all 103 variants compile without GCC-only assumptions |
| ASan/UBSan | `Tests/quality.sh sanitize` | the full host suite has no detected memory or undefined-behaviour errors |
| Coverage | `Tests/quality.sh coverage` | line ≥80% and functions ≥90% for `Xcp/`; XML/HTML under `Tests/integration/.build/coverage` |
| ARM cross-compile | `Tests/quality.sh arm` | Cortex-M0 and Cortex-M4 32-bit builds compile with `-Werror` |
| Static analysis | `Tests/quality.sh cppcheck` | cppcheck warning/performance/portability gate |
| MISRA report | `Tests/quality.sh misra` | advisory cppcheck MISRA findings under `Tests/integration/.build/quality/misra` |

`Tests/quality.sh all` runs every blocking gate plus the advisory MISRA report.
It requires `clang`, `gcc`, `gcovr`, `arm-none-eabi-gcc` and `cppcheck`; CI
installs them explicitly. The MISRA report is not a compliance claim: it uses
cppcheck's addon with configuration-dependent messages suppressed, and the
repository does not maintain a reviewed deviation list. Treat it as a source
of candidate findings, not as a pass/fail gate.

## CI

`.github/workflows/ci.yml` runs on Ubuntu and Windows:

- ratchet, profile regeneration, xcpgen tests
- host CAN build and the 103-variant matrix
- pyxcp smoke, functional and feature-variant suites
- pyxcp performance at `XCP_PERF_SCALE=0.5`; the JSON report is uploaded as a
  build artifact
- an Ubuntu `quality` job: clang host + variant matrix, ASan/UBSan, coverage
  gates, Cortex-M0/M4 cross-compile and cppcheck; the advisory MISRA report is
  uploaded as an artifact

Live status is the CI badge in the README. Pass counts and timings are
deliberately not pinned in documentation because they change with every
commit; consult the workflow run for the authoritative result.
