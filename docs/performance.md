# Performance
> **中文**: [docs/performance.zh-CN.md](performance.zh-CN.md)

These numbers are a **reference envelope, not a silicon or bus benchmark**.
They characterize the complete path used by the integration tests: pyxcp, the
Python frame pump, ctypes, the bridge and the C slave. They are useful for
regression detection and for comparing workloads; they are not a guarantee
for a production ECU or a real CAN/CAN-FD network.

## Method

Two generated profiles are compiled and driven through pyxcp over a
`python-can` `virtual` bus:

| Build | CTO/DTO | ODT entries | DTO queue |
|-------|---------|-------------|-----------|
| classic CAN | 8 bytes | 1 × 4-byte entry | 128 |
| CAN-FD | 64 bytes | 15 × 4-byte entries | 128 |

UPLOAD and DOWNLOAD move the same 8 KiB of application data on both builds.
DAQ uses the maximum dynamic configuration: 8 lists × 4 ODTs, sampled on
event channel 0 (10 ms) for sustained load and event channel 1 (1 ms) for the
peak test. STIM uses the same 8 × 4 ODT geometry, driven through pyxcp's
`Stim.triggerEvent`; 12 800 frames are sent per build and the bridge counts
every frame that reaches the slave.

### Metrics

- **CTO latency** — one command/response round trip, including pyxcp and the
  bridge threads. p50/p95/p99 are over 300 commands.
- **Bulk throughput** — application bytes per second for UPLOAD/DOWNLOAD, with
  the largest payload that fits the CTO (7/63 bytes for UPLOAD, 6/62 for
  DOWNLOAD).
- **DAQ sample bytes** — configured entry bytes only (`entries × 4`).
- **DAQ wire bytes** — full DTO payload after the PID, including the slave's
  padding to `XCP_MAX_DTO`.
- **Simulated time** — delta of the slave's OS tick counter, i.e. the time
  that actually scheduled events. The Python tick loop can run slower than
  wall time, so sample throughput is reported both per wall second and per
  simulated second.
- **Delivery** — observed DTOs divided by DTOs scheduled from simulated ticks.
- **DTO drops** — the slave's own queue-overflow counter.

## Reference run

Environment: Windows 10 x64, CPython 3.13.13, pyxcp 0.30.5, python-can 4.6.1,
commit `890c7db`, `XCP_PERF_SCALE=1.0` (run time 21 s).

| Metric | Classic CAN | CAN-FD |
|--------|------------:|-------:|
| CTO latency p50 | 1.23 ms | 1.15 ms |
| CTO latency p99 | 2.47 ms | 2.08 ms |
| CTO rate | 755 cmd/s | 819 cmd/s |
| UPLOAD | 5.5 kB/s | 52.0 kB/s |
| DOWNLOAD | 4.7 kB/s | 49.1 kB/s |
| DAQ event0 frames | 2 353 /s | 2 237 /s |
| DAQ event0 sample, wall | 9.4 kB/s | 134.2 kB/s |
| DAQ event0 sample, simulated | 12.7 kB/s | 192.2 kB/s |
| DAQ event0 wire | 16.5 kB/s | 140.9 kB/s |
| DAQ event0 delivery / drops | 1.00 / 0 | 1.01 / 0 |
| DAQ event1 frames | 15 502 /s | 14 235 /s |
| DAQ event1 sample, simulated | 128.4 kB/s | 1 919.3 kB/s |
| DAQ event1 drops | 0 | 0 |
| STIM frames | 48 770 /s | 42 203 /s |
| STIM payload | 195 kB/s | 2 532 kB/s |
| Flash programming | 3.2 kB/s | 8.0 kB/s |

The DAQ sample numbers match the configuration exactly in simulated time:

```
classic: 32 DTO × 4 B  / 10 ms = 12.8 kB/s
CAN-FD:  32 DTO × 60 B / 10 ms = 192 kB/s
```

At event channel 1 (1 ms), a 32-DTO burst can exceed the 128-entry queue when
the host cannot drain it; earlier runs reported FD drops (for example 185),
while this run reports none. This is the expected queue-depth limit, not data
corruption (all delivered samples match RAM).

## Interpretation

The virtual bus is a local memory queue. It does not model arbitration, the
1 Mbit/s arbitration phase, the 2–5 Mbit/s FD data phase, CRC or transceiver
timing. Consequently:

- **Frame-bound workloads show no FD gain.** DAQ produces one DTO per ODT per
  event regardless of DTO size, and a CTO is one request/response round trip.
  Frames per second and commands per second are therefore essentially equal.
- **Byte-bound workloads gain the payload ratio.** UPLOAD/DOWNLOAD gain about
  9–10× (63/7 and 62/6 bytes), and DAQ sample throughput gains 15× because an
  FD ODT carries 15 entries instead of one.
- **Padding matters.** DTOs are padded to `XCP_MAX_DTO`, so wire bytes grow
  9× while useful sample bytes grow 15×. Reporting only whole frames as
  "payload" understates the FD advantage.
- **Real CAN-FD is slower than these ratios suggest.** The arbitration phase
  remains at 1 Mbit/s and FD CRC is longer; typical net throughput gains over
  classic CAN are in the 3–7× range, not 15×.
- **Flash is command-bound.** Each 32-byte prepare block still needs
  `PROGRAM_PREPARE`, `SET_MTA` and one or more `PROGRAM` round trips, so the
  measured gain is only about 2.8×.

### Why STIM is much faster than DOWNLOAD and DAQ

STIM is a one-way, unacknowledged injection path. pyxcp's `Stim.triggerEvent`
builds the frames and sends them without waiting for a response, and the slave
writes the payload directly in its receive path — no response frame, no DTO
scheduler and no transmit queue. DOWNLOAD waits for one RES per frame, and DAQ
is event-scheduled ECU transmit, so all three measure different bottlenecks.
The ~49 000 frames/s figure is the Python/pyxcp sender plus bridge capacity,
not a CAN bus capability: at 1 Mbit/s, an 8-byte classic frame occupies the
bus for roughly 130 µs, capping a real bus near 7–8 k frames/s; at 2 Mbit/s, a
64-byte CAN-FD frame takes roughly 300 µs, capping it near 3 k frames/s.
Do not compare STIM frames/s directly with DAQ or DOWNLOAD frames/s.

## Reproduce

```bash
python3 -m pip install -r Tests/integration/requirements.txt
python3 Tests/integration/test_pyxcp_perf.py

# Faster CI-sized run (halves transfer sizes and observation windows)
XCP_PERF_SCALE=0.5 python3 Tests/integration/test_pyxcp_perf.py
```

The JSON report is written to `Tests/integration/.build/perf.json`; CI uploads
it as an artifact. The script only asserts data integrity and lenient
capability floors (for example, at least 1 kB/s for bulk transfer), not the
reference values above, so slower CI hosts do not produce false failures.
