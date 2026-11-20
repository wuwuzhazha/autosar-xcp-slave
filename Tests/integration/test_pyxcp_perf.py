#!/usr/bin/env python3
"""Performance envelope of the slave, measured through pyxcp.

Two builds are exercised: the default classic-CAN profile (8-byte CTO/DTO)
and a CAN-FD profile (64-byte CTO/DTO, 15 ODT entries). Both transfer the
same application byte count for UPLOAD/DOWNLOAD. Every measurement uses the
real pyxcp transport over a python-can ``virtual`` bus; the numbers therefore
include pyxcp and the Python frame pump, not just the C core.

The virtual bus is a local memory queue: it does not model arbitration or
CAN/CAN-FD bit timing. The FD advantage measured here is the payload-per-
frame gain (fewer frames for the same bytes), not the FD data-phase bit rate.
DAQ throughput is reported both as sampled entry bytes and as wire bytes
(PID + padding included), and delivery is compared with the simulated OS
ticks that actually scheduled the events.

Report: Tests/integration/.build/perf.json plus a Markdown table on stdout.
Full run takes roughly half a minute. ``XCP_PERF_SCALE`` scales the transfer
sizes and observation windows (e.g. 0.5 for a quicker CI pass).

Run: python Tests/integration/test_pyxcp_perf.py
"""

from __future__ import annotations

import json
import os
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import xcp_bridge as h

if h.can is None:
    print("SKIP: install python-can first")
    sys.exit(2)

try:
    from pyxcp.master import Master
except ImportError as exc:
    print(f"SKIP: install pyxcp first ({exc})")
    sys.exit(2)

SCALE = max(0.1, float(os.environ.get("XCP_PERF_SCALE", "1.0")))
RESULTS: dict[str, dict] = {}


def percentile(sorted_values: list[float], fraction: float) -> float:
    if not sorted_values:
        return 0.0
    index = min(len(sorted_values) - 1, int(round(fraction * (len(sorted_values) - 1))))
    return sorted_values[index]


def summarize(samples_ms: list[float]) -> dict:
    ordered = sorted(samples_ms)
    return {
        "samples": len(ordered),
        "mean_ms": round(sum(ordered) / len(ordered), 3),
        "p50_ms": round(percentile(ordered, 0.50), 3),
        "p95_ms": round(percentile(ordered, 0.95), 3),
        "p99_ms": round(percentile(ordered, 0.99), 3),
        "max_ms": round(ordered[-1], 3),
        "per_s": round(1000.0 * len(ordered) / sum(ordered), 1),
    }


def latency(xcp, fn, count: int) -> dict:
    samples = []
    for _ in range(count):
        t0 = time.perf_counter_ns()
        fn()
        samples.append((time.perf_counter_ns() - t0) / 1e6)
    return summarize(samples)


def upload_throughput(xcp, link, addr: int, total: int, chunk: int) -> dict:
    link.ram_write(bytes((i * 17 + 3) & 0xFF for i in range(total)))
    xcp.setMta(addr)
    sent = 0
    t0 = time.perf_counter()
    while sent < total:
        size = min(chunk, total - sent)
        data = xcp.upload(size)
        if len(data) != size:
            raise AssertionError(f"UPLOAD returned {len(data)} of {size} bytes")
        sent += size
    elapsed = time.perf_counter() - t0
    return {
        "bytes": sent,
        "seconds": round(elapsed, 3),
        "bytes_per_s": round(sent / elapsed, 1),
        "frames": -(-sent // chunk),
        "frames_per_s": round((-(-sent // chunk)) / elapsed, 1),
    }


def block_upload_throughput(
    xcp, link, addr: int, total: int, request: int, frame_payload: int
) -> dict:
    pattern = bytes((i * 17 + 3) & 0xFF for i in range(total))
    link.ram_write(pattern)
    xcp.setMta(addr)
    sent = 0
    requests = 0
    frames = 0
    t0 = time.perf_counter()
    while sent < total:
        size = min(request, total - sent)
        data = xcp.upload(size)
        if data != pattern[sent : sent + size]:
            raise AssertionError(f"UPLOAD block returned wrong data at {sent}")
        sent += size
        requests += 1
        frames += -(-size // frame_payload)
    elapsed = time.perf_counter() - t0
    return {
        "bytes": sent,
        "seconds": round(elapsed, 3),
        "bytes_per_s": round(sent / elapsed, 1),
        "requests": requests,
        "requests_per_s": round(requests / elapsed, 1),
        "frames": frames,
        "frames_per_s": round(frames / elapsed, 1),
        "integrity": True,
    }


def download_throughput(xcp, link, addr: int, total: int, chunk: int) -> dict:
    payload = bytes([0xA5]) * chunk
    xcp.setMta(addr)
    sent = 0
    frames = 0
    t0 = time.perf_counter()
    while sent < total:
        size = min(chunk, total - sent)
        xcp.download(payload[:size])
        sent += size
        frames += 1
    elapsed = time.perf_counter() - t0
    ok = link.ram_read(total) == (payload * (total // chunk + 1))[:total]
    return {
        "bytes": sent,
        "seconds": round(elapsed, 3),
        "bytes_per_s": round(sent / elapsed, 1),
        "frames": frames,
        "frames_per_s": round(frames / elapsed, 1),
        "integrity": ok,
    }


def configure_max_daq(xcp, link, addr: int, entries: int, event: int) -> tuple[int, list[int]]:
    lists, odts = 8, 4
    values = lists * odts * entries
    image = bytearray(4 * values)
    for index in range(values):
        struct.pack_into("<I", image, 4 * index, index)

    xcp.freeDaq()
    xcp.allocDaq(lists)
    for daq_list in range(lists):
        xcp.allocOdt(daq_list, odts)
        for odt in range(odts):
            xcp.allocOdtEntry(daq_list, odt, entries)
            xcp.setDaqPtr(daq_list, odt, 0)
            for entry in range(entries):
                index = (daq_list * odts + odt) * entries + entry
                xcp.writeDaq(0xFF, 4, 0, addr + 4 * index)
        xcp.setDaqListMode(0, daq_list, event, 1, 0)
    link.ram_write(bytes(image))
    return values, [0, 4, 8, 12]


def collect_dtos(bus, seconds: float) -> list[bytes]:
    frames = []
    end = time.perf_counter() + seconds
    while time.perf_counter() < end:
        frame = bus.recv(timeout=0.02)
        if (
            frame is not None
            and frame.arbitration_id == h.SLAVE_ID
            and frame.data
            and frame.data[0] < 0xFC
        ):
            frames.append(bytes(frame.data))
    return frames


def start_daq(xcp) -> None:
    for daq_list in range(8):
        xcp.startStopDaqList(2, daq_list)
    xcp.startStopSynch(1)


def stop_daq(xcp) -> None:
    xcp.startStopSynch(2)


def check_dto_frames(frames: list[bytes], entries: int) -> tuple[int, int, int]:
    """Return (unique PIDs, sample mismatches, wire bytes)."""
    seen = set()
    mismatches = 0
    wire_bytes = 0
    for frame in frames:
        pid = frame[0]
        seen.add(pid)
        wire_bytes += len(frame) - 1
        base = pid * entries
        payload = frame[1 : 1 + 4 * entries]
        if len(payload) != 4 * entries:
            mismatches += 1
            continue
        for entry in range(entries):
            offset = 4 * entry
            if payload[offset : offset + 4] != struct.pack("<I", base + entry):
                mismatches += 1
                break
    return len(seen), mismatches, wire_bytes


def daq_benchmark(xcp, link, tap_bus, addr: int, entries: int, event: int, seconds: float) -> dict:
    values, _ = configure_max_daq(xcp, link, addr, entries, event)
    dtos_per_event = 32
    cycle_ticks = 10 if event == 0 else 1

    h.drain(tap_bus)
    link.reset_dto_drop_count()
    start_daq(xcp)
    ticks_before = link.os_ticks()
    frames = collect_dtos(tap_bus, seconds)
    ticks_after = link.os_ticks()
    drops = link.dto_drop_count()
    stop_daq(xcp)
    h.drain(tap_bus)

    unique, mismatches, wire_bytes = check_dto_frames(frames, entries)
    simulated_ms = ticks_after - ticks_before
    simulated_s = simulated_ms / 1000.0
    scheduled_events = simulated_ms // cycle_ticks
    expected_frames = scheduled_events * dtos_per_event
    sample_bytes = len(frames) * entries * 4
    return {
        "entries_per_odt": entries,
        "event_channel": event,
        "values": values,
        "frames": len(frames),
        "frames_per_s": round(len(frames) / seconds, 1),
        # Configured entry bytes are the useful DAQ payload; wire bytes also
        # include the PID and the DTO padding to XCP_MAX_DTO.
        "sample_bytes": sample_bytes,
        "sample_bytes_per_s": round(sample_bytes / seconds, 1),
        "sample_bytes_per_simulated_s": round(sample_bytes / simulated_s, 1) if simulated_s else 0.0,
        "wire_bytes": wire_bytes,
        "wire_bytes_per_s": round(wire_bytes / seconds, 1),
        "wire_bytes_per_simulated_s": round(wire_bytes / simulated_s, 1) if simulated_s else 0.0,
        "unique_pids": unique,
        "mismatches": mismatches,
        "simulated_ms": simulated_ms,
        "scheduled_events": scheduled_events,
        "expected_frames": expected_frames,
        "events_observed": round(len(frames) / dtos_per_event, 1),
        "delivery_ratio": round(len(frames) / max(expected_frames, 1), 3),
        "dto_drops": drops,
    }


def stim_throughput(xcp, link, addr: int, entries: int, frames_target: int) -> dict:
    lists, odts = 8, 4
    values = lists * odts * entries
    xcp.stim.activate(True)
    # pyxcp 0.30.5 hands the STIM payload to the frame sender as a list;
    # block_request expects bytes.
    xcp.stim.set_frame_sender(
        lambda cmd, payload: xcp.transport.block_request(cmd, bytes(payload))
    )

    xcp.freeDaq()
    xcp.allocDaq(lists)
    for daq_list in range(lists):
        xcp.allocOdt(daq_list, odts)
        data = bytearray()
        for odt in range(odts):
            xcp.allocOdtEntry(daq_list, odt, entries)
            xcp.setDaqPtr(daq_list, odt, 0)
            for entry in range(entries):
                index = (daq_list * odts + odt) * entries + entry
                xcp.writeDaq(0xFF, 4, 0, addr + 4 * index)
                data.extend(struct.pack("<I", index))
        xcp.setDaqListMode(0x02, daq_list, 0, 1, 0)
        xcp.stim.setData(daq_list, bytes(data))
    for daq_list in range(lists):
        xcp.startStopDaqList(1, daq_list)

    expected = b"".join(struct.pack("<I", index) for index in range(values))
    link.ram_write(bytes(4 * values))
    link.reset_stim_frame_count()

    dtos_per_event = lists * odts
    rounds = -(-frames_target // dtos_per_event)
    frames = rounds * dtos_per_event
    t0 = time.perf_counter()
    for _ in range(rounds):
        xcp.stim.triggerEvent(0)

    deadline = time.perf_counter() + 10.0
    while time.perf_counter() < deadline:
        if (
            link.stim_frame_count() >= frames
            and link.ram_read(4 * values) == expected
        ):
            break
        time.sleep(0.002)
    elapsed = time.perf_counter() - t0

    received = link.stim_frame_count()
    integrity = link.ram_read(4 * values) == expected
    for daq_list in range(lists):
        xcp.startStopDaqList(0, daq_list)
    xcp.freeDaq()
    payload_bytes = frames * entries * 4
    return {
        "entries_per_odt": entries,
        "frames": frames,
        "frames_received": received,
        "delivery_ratio": round(received / frames, 3),
        "frames_per_s": round(frames / elapsed, 1),
        "payload_bytes": payload_bytes,
        "payload_bytes_per_s": round(payload_bytes / elapsed, 1),
        "seconds": round(elapsed, 3),
        "integrity": integrity,
    }


def flash_throughput(xcp, link, chunk: int, total: int) -> dict:
    payload = bytes((i * 7 + 1) & 0xFF for i in range(total))
    base = 0x08000000
    prepare_block = 32  # XCP_MAX_PGM_BLOCK_SIZE in the generated profile
    xcp.programStart()
    xcp.setMta(base)
    xcp.programClear(0, 0x4000)

    sent = 0
    t0 = time.perf_counter()
    while sent < total:
        block = min(prepare_block, total - sent)
        xcp.programPrepare(block)
        xcp.setMta(base + sent)
        inner = 0
        while inner < block:
            size = min(chunk, block - inner)
            xcp.program(payload[sent + inner : sent + inner + size], size, last=True)
            inner += size
        sent += block
    elapsed = time.perf_counter() - t0
    xcp.programVerify(0, 0, 0)
    integrity = link.flash_read(0x08000000, total) == payload
    xcp.programReset()
    return {
        "bytes": total,
        "seconds": round(elapsed, 3),
        "bytes_per_s": round(total / elapsed, 1),
        "integrity": integrity,
    }


def run_variant(name: str, profile_sets: tuple[str, ...], fd: bool, payload_size: int) -> dict:
    print(f"== {name} ==")
    lib = h.load_bridge(h.build_bridge(name, sets=profile_sets))
    channel = f"xcp-perf-{name}-{id(object())}"
    buses = h.open_buses(channel)
    master_bus, rx_bus, tx_bus, tap_bus = buses
    link = h.SlaveLink(lib, rx_bus, tx_bus, fd=fd)
    link.start()

    cto_chunk = 63 if fd else 7
    dto_chunk = 62 if fd else 6
    entries = 15 if fd else 1
    transfer = int(payload_size * SCALE) & ~0xFF
    daq_window = 2.0 * SCALE
    load_window = 1.0 * SCALE

    try:
        with Master("can", h.make_app(channel, fd=fd, timeout=5.0), transport_layer_interface=master_bus) as xcp:
            xcp.connect()
            for resource in (0x01, 0x04, 0x08, 0x10):
                h.unlock(xcp, resource)
            addr = link.ram_addr()

            result = {}
            result["cto_latency"] = latency(xcp, lambda: xcp.getStatus(), int(300 * SCALE))
            result["short_upload_latency"] = latency(
                xcp, lambda: xcp.shortUpload(4, addr), int(300 * SCALE)
            )
            result["upload"] = upload_throughput(xcp, link, addr, transfer, cto_chunk)
            result["upload_block"] = block_upload_throughput(
                xcp, link, addr, transfer, 252, cto_chunk
            )
            result["download"] = download_throughput(xcp, link, addr, transfer, dto_chunk)
            result["daq_sustained"] = daq_benchmark(xcp, link, tap_bus, addr, entries, 0, daq_window)
            result["daq_peak"] = daq_benchmark(xcp, link, tap_bus, addr, entries, 1, 1.0 * SCALE)
            result["stim"] = stim_throughput(
                xcp, link, addr, entries, max(6400, int(12800 * SCALE))
            )

            # CTO latency while a sustained DAQ list set is running.
            configure_max_daq(xcp, link, addr, entries, 0)
            start_daq(xcp)
            result["cto_latency_under_daq"] = latency(xcp, lambda: xcp.getStatus(), int(200 * SCALE))
            stop_daq(xcp)
            xcp.freeDaq()

            result["flash"] = flash_throughput(xcp, link, dto_chunk, int(4096 * SCALE))
            xcp.connect()
            xcp.disconnect()
            return result
    finally:
        link.stop()
        h.close_buses(*buses)


def check(condition: bool, message: str) -> bool:
    status = "PASS" if condition else "FAIL"
    print(f"  {status}  {message}")
    return condition


def main() -> int:
    start = time.time()
    classic = run_variant(
        "perf_can", ("XcpDtoQueueDepth=128",), fd=False, payload_size=8192
    )
    fd = run_variant(
        "perf_fd",
        (
            "XcpMaxCto=64",
            "XcpMaxDto=64",
            "XcpOdtEntriesCount=15",
            "XcpDtoQueueDepth=128",
        ),
        fd=True,
        payload_size=8192,
    )
    RESULTS["classic_can"] = classic
    RESULTS["can_fd"] = fd
    RESULTS["meta"] = {
        "scale": SCALE,
        "duration_s": round(time.time() - start, 2),
        "python": sys.version.split()[0],
        "platform": sys.platform,
    }

    out = h.BUILD / "perf.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(RESULTS, indent=2), encoding="utf-8")

    print("\n| benchmark | classic CAN | CAN-FD |")
    print("|---|---:|---:|")
    rows = [
        ("CTO latency p50 (ms)", classic["cto_latency"]["p50_ms"], fd["cto_latency"]["p50_ms"]),
        ("CTO latency p99 (ms)", classic["cto_latency"]["p99_ms"], fd["cto_latency"]["p99_ms"]),
        ("CTO rate (cmd/s)", classic["cto_latency"]["per_s"], fd["cto_latency"]["per_s"]),
        ("UPLOAD plain (B/s)", classic["upload"]["bytes_per_s"], fd["upload"]["bytes_per_s"]),
        ("UPLOAD block (B/s)", classic["upload_block"]["bytes_per_s"], fd["upload_block"]["bytes_per_s"]),
        ("DOWNLOAD (B/s)", classic["download"]["bytes_per_s"], fd["download"]["bytes_per_s"]),
        ("DAQ event0 (frames/s)", classic["daq_sustained"]["frames_per_s"], fd["daq_sustained"]["frames_per_s"]),
        ("DAQ event0 sample (B/s)", classic["daq_sustained"]["sample_bytes_per_s"], fd["daq_sustained"]["sample_bytes_per_s"]),
        ("DAQ event0 sample (B/sim-s)", classic["daq_sustained"]["sample_bytes_per_simulated_s"], fd["daq_sustained"]["sample_bytes_per_simulated_s"]),
        ("DAQ event0 wire (B/s)", classic["daq_sustained"]["wire_bytes_per_s"], fd["daq_sustained"]["wire_bytes_per_s"]),
        ("DAQ event0 scheduled delivery", classic["daq_sustained"]["delivery_ratio"], fd["daq_sustained"]["delivery_ratio"]),
        ("DAQ event1 (frames/s)", classic["daq_peak"]["frames_per_s"], fd["daq_peak"]["frames_per_s"]),
        ("STIM (frames/s)", classic["stim"]["frames_per_s"], fd["stim"]["frames_per_s"]),
        ("STIM (B/s)", classic["stim"]["payload_bytes_per_s"], fd["stim"]["payload_bytes_per_s"]),
        ("CTO p99 under DAQ (ms)", classic["cto_latency_under_daq"]["p99_ms"], fd["cto_latency_under_daq"]["p99_ms"]),
        ("Flash programming (B/s)", classic["flash"]["bytes_per_s"], fd["flash"]["bytes_per_s"]),
    ]
    for label, a, b in rows:
        print(f"| {label} | {a} | {b} |")
    print(
        "\nThe virtual bus does not model CAN/CAN-FD bit timing. The FD gain above "
        "comes from carrying more entry bytes per frame (and fewer frames for the "
        "same transfer), not from the FD data-phase bit rate."
    )
    print(f"JSON report: {out}")

    ok = True
    for label, variant in (("classic", classic), ("fd", fd)):
        ok &= check(variant["cto_latency"]["p99_ms"] < 500.0, f"{label}: CTO p99 below 500 ms")
        ok &= check(variant["upload"]["bytes_per_s"] > 1000.0, f"{label}: UPLOAD above 1 kB/s")
        ok &= check(
            variant["upload_block"]["integrity"],
            f"{label}: block UPLOAD contents match",
        )
        ok &= check(
            variant["upload_block"]["bytes_per_s"] > 1000.0,
            f"{label}: block UPLOAD above 1 kB/s",
        )
        ok &= check(
            variant["upload_block"]["requests"] < variant["upload"]["frames"],
            f"{label}: block UPLOAD uses fewer requests than plain frames",
        )
        ok &= check(variant["download"]["bytes_per_s"] > 1000.0, f"{label}: DOWNLOAD above 1 kB/s")
        ok &= check(variant["download"]["integrity"], f"{label}: DOWNLOAD contents match")
        ok &= check(
            variant["daq_sustained"]["unique_pids"] == 32,
            f"{label}: all 32 ODT PIDs observed",
        )
        ok &= check(
            variant["daq_sustained"]["mismatches"] == 0,
            f"{label}: DAQ samples match RAM",
        )
        # Compared against the simulated ticks that actually fired, not the
        # wall-clock ideal; anything near 1.0 means the queue kept up.
        ok &= check(
            variant["daq_sustained"]["delivery_ratio"] > 0.5,
            f"{label}: sustained DAQ tracks the event scheduler",
        )
        ok &= check(variant["stim"]["integrity"], f"{label}: STIM contents match")
        ok &= check(
            variant["stim"]["delivery_ratio"] == 1.0,
            f"{label}: every STIM frame reached the slave",
        )
        ok &= check(variant["stim"]["frames_per_s"] > 100.0, f"{label}: STIM above 100 frames/s")
        ok &= check(variant["flash"]["integrity"], f"{label}: flash contents match")
        ok &= check(variant["flash"]["bytes_per_s"] > 100.0, f"{label}: flash above 100 B/s")

    print(f"================ pyxcp perf: {'PASS' if ok else 'FAIL'} ================")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
