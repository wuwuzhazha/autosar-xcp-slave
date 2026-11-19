#!/usr/bin/env python3
"""End-to-end XCP test: pyxcp master talks to this slave over a CAN transport.

The slave is built as a shared library and bridged to a python-can virtual bus.
This exercises the real CTO/DTO framing, not the internal C test harness.

Run: python Tests/integration/test_pyxcp_master.py
"""

from __future__ import annotations

import ctypes
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

try:
    import can
except ImportError:  # pragma: no cover - reported by main()
    can = None

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

MASTER_ID = 0x700  # master -> slave (CMD, STIM)
SLAVE_ID = 0x701   # slave -> master (RES, ERR, EV, DAQ)

SOURCES = [
    "Xcp/Xcp.c",
    "Xcp/Xcp_Cmd.c",
    "Xcp/Xcp_Std.c",
    "Xcp/Xcp_DaqCfg.c",
    "Xcp/Xcp_DaqRun.c",
    "Xcp/Xcp_DaqStim.c",
    "Xcp/Xcp_SeedKey.c",
    "Xcp/Xcp_CalPage.c",
    "Xcp/Xcp_FlashProg.c",
    "Xcp/Xcp_Resume.c",
    "Xcp/XcpOnCan.c",
    "Xcp/XcpOnFr.c",
    "Xcp/XcpOnEth.c",
    "Xcp/XcpOnCdd.c",
    "Tests/test_stubs.c",
    "Tests/integration/xcp_slave_bridge.c",
    "Gen/can/Xcp_Cfg.c",
]


def build_bridge() -> Path:
    suffix = ".dll" if os.name == "nt" else ".so"
    out = HERE / ("xcp_slave_bridge" + suffix)
    cflags = [
        "-shared", "-std=c99", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter",
        "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
        "-ITests/stubs", "-ITests", "-IXcp", "-IGen/can",
        "-include", "Tests/test_config.h",
    ]
    cflags.append("-static-libgcc" if os.name == "nt" else "-fPIC")
    subprocess.run(["gcc", *cflags, *SOURCES, "-o", str(out)], cwd=ROOT, check=True)
    return out


class SlaveLink:
    """Pumps CAN frames between a python-can virtual bus and the slave DLL."""

    def __init__(self, lib: ctypes.CDLL, bus) -> None:
        self.lib = lib
        self.bus = bus
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self.lib.bridge_init()
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=2.0)

    def ram_addr(self) -> int:
        return int(self.lib.bridge_ram_addr())

    def ram_write(self, data: bytes) -> None:
        buf = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
        self.lib.bridge_ram_write(buf, len(data))

    def ram_read(self, length: int) -> bytes:
        buf = (ctypes.c_ubyte * length)()
        self.lib.bridge_ram_read(buf, length)
        return bytes(buf)

    def _run(self) -> None:
        u8p = ctypes.POINTER(ctypes.c_ubyte)
        buf = (ctypes.c_ubyte * 64)()
        length = ctypes.c_uint32()

        while not self._stop.is_set():
            # 1 ms housekeeping tick for DAQ scheduling and pending TX.
            self.lib.bridge_tick(1)

            while True:
                n = self.lib.bridge_tx_pop(buf, ctypes.byref(length))
                if n == 0:
                    break
                frame = bytes(buf[: length.value])
                self.bus.send(
                    can.Message(arbitration_id=SLAVE_ID, is_extended_id=False, data=frame)
                )
                self.lib.bridge_tx_confirm()

            msg = self.bus.recv(timeout=0.001)
            if msg is not None and msg.arbitration_id == MASTER_ID:
                data = (ctypes.c_ubyte * len(msg.data)).from_buffer_copy(msg.data)
                self.lib.bridge_rx(data, len(msg.data))


def load_bridge(path: Path) -> ctypes.CDLL:
    lib = ctypes.CDLL(str(path))
    u8p = ctypes.POINTER(ctypes.c_ubyte)
    lib.bridge_init.argtypes = []
    lib.bridge_tick.argtypes = [ctypes.c_uint32]
    lib.bridge_rx.argtypes = [u8p, ctypes.c_uint32]
    lib.bridge_tx_pop.argtypes = [u8p, ctypes.POINTER(ctypes.c_uint32)]
    lib.bridge_tx_pop.restype = ctypes.c_uint32
    lib.bridge_tx_confirm.argtypes = []
    lib.bridge_ram_addr.restype = ctypes.c_uint32
    lib.bridge_ram_read.argtypes = [u8p, ctypes.c_uint32]
    lib.bridge_ram_write.argtypes = [u8p, ctypes.c_uint32]
    return lib


def make_config():
    from pyxcp.config import create_application_from_config

    app = create_application_from_config(
        {
            "Transport": {
                "CAN": {
                    "interface": "virtual",
                    "channel": "xcp-integration",
                    "can_id_master": MASTER_ID,
                    "can_id_slave": SLAVE_ID,
                    "daq_identifier": [],
                }
            },
            "General": {"timeout": 2.0},
        }
    )
    app.transport.layer = "CAN"
    return app


def main() -> int:
    try:
        from pyxcp.master import Master
    except ImportError as exc:
        print(f"SKIP: install pyxcp and python-can first ({exc})")
        return 2

    if can is None:
        print("SKIP: install python-can first")
        return 2

    bridge_path = build_bridge()
    lib = load_bridge(bridge_path)

    master_bus = can.Bus(interface="virtual", channel="xcp-integration")
    slave_bus = can.Bus(interface="virtual", channel="xcp-integration")
    tap_bus = can.Bus(interface="virtual", channel="xcp-integration")
    slave_bus.set_filters(
        [{"can_id": MASTER_ID, "can_mask": 0x7FF, "extended": False}]
    )
    tap_bus.set_filters(
        [{"can_id": SLAVE_ID, "can_mask": 0x7FF, "extended": False}]
    )

    link = SlaveLink(lib, slave_bus)
    link.start()

    failures = 0

    def check(name: str, condition: bool, detail: str = "") -> None:
        nonlocal failures
        if condition:
            print(f"  PASS  {name}")
        else:
            failures += 1
            print(f"  FAIL  {name}  {detail}")

    try:
        with Master("can", make_config(), transport_layer_interface=master_bus) as xcp:
            resp = xcp.connect()
            check(
                "CONNECT reply: protocol layer CAN, CTO/DTO 8",
                resp.maxCto == 8 and resp.maxDto == 8,
                f"maxCto={resp.maxCto} maxDto={resp.maxDto}",
            )

            status = xcp.getStatus()
            check(
                "GET_STATUS: session configuration 0, CAL locked",
                status.sessionConfiguration == 0
                and status.resourceProtectionStatus.calpag is True,
                f"status={status}",
            )

            ident = xcp.getId(0)
            ident_data = xcp.fetch(ident.length)
            check(
                "GET_ID + UPLOAD returns the ID string",
                ident_data == b"AUTOSAR XCP Slave",
                f"got {ident_data!r}",
            )

            seed = xcp.getSeed(0, 0x01)
            key = bytes(b ^ 0xFF for b in seed.seed)
            unlocked = xcp.unlock(len(key), key)
            check("GET_SEED + UNLOCK opens CAL", bool(unlocked), f"seed={seed.seed!r}")

            seed = xcp.getSeed(0, 0x04)
            key = bytes(b ^ 0xFF for b in seed.seed)
            check("GET_SEED + UNLOCK opens DAQ", bool(xcp.unlock(len(key), key)))

            addr = link.ram_addr()
            link.ram_write(b"\x01\x02\x03\x04")
            xcp.setMta(addr)
            up = xcp.upload(4)
            check("SET_MTA + UPLOAD reads bridge RAM", up == b"\x01\x02\x03\x04", f"got {up!r}")

            xcp.setMta(addr)
            xcp.download(b"\xde\xad\xbe\xef")
            check(
                "SET_MTA + DOWNLOAD writes bridge RAM",
                link.ram_read(4) == b"\xde\xad\xbe\xef",
                f"got {link.ram_read(4)!r}",
            )

            short = xcp.shortUpload(4, addr)
            check("SHORT_UPLOAD reads the same window", short == b"\xde\xad\xbe\xef", f"got {short!r}")

            # Dynamic DAQ: one list, one ODT, one 4-byte entry on event 0.
            # A tap bus observes the DTOs independently of pyxcp's listener.
            xcp.freeDaq()
            xcp.allocDaq(1)
            xcp.allocOdt(0, 1)
            xcp.allocOdtEntry(0, 0, 1)
            xcp.setDaqPtr(0, 0, 0)
            xcp.writeDaq(0xFF, 4, 0, addr)  # 0xFF = no bit offset (plain 4-byte entry)
            xcp.setDaqListMode(0, 0, 0, 1, 0)
            link.ram_write(bytes([0x44, 0x33, 0x22, 0x11]))
            while tap_bus.recv(timeout=0) is not None:
                pass
            first = xcp.startStopDaqList(1, 0)

            dto = None
            deadline = time.time() + 1.0
            while time.time() < deadline and dto is None:
                frame = tap_bus.recv(timeout=0.05)
                if frame is not None and len(frame.data) >= 5 and frame.data[0] < 0xFC:
                    dto = bytes(frame.data)

            check(
                "START_DAQ_LIST + event 0 emits a DTO with the RAM value",
                dto is not None
                and dto[0] == first.firstPid
                and dto[1:5] == b"\x44\x33\x22\x11",
                f"pid={first.firstPid} dto={dto!r}",
            )
            xcp.startStopDaqList(0, 0)
            xcp.freeDaq()

            slave_id = xcp.transportLayerCmd(0xFF)
            check("TL_CMD GET_SLAVE_ID returns 'XCP'", slave_id[-3:] == b"XCP", f"got {slave_id!r}")

            xcp.disconnect()
    finally:
        link.stop()
        try:
            master_bus.shutdown()
            slave_bus.shutdown()
            tap_bus.shutdown()
        except Exception:
            pass

    print(f"================ pyxcp integration: {failures} failed ================")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
