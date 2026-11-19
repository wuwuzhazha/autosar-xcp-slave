"""Shared harness: build the slave bridge DLL and connect pyxcp to it.

The slave runtime is compiled as a shared library and bridged to a python-can
``virtual`` bus, so pyxcp exchanges real XCPonCAN frames with the C code.
CPU-bound transport work stays in this process; no CAN hardware is required.
"""

from __future__ import annotations

import ctypes
import logging
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

try:
    import can
except ImportError:  # pragma: no cover - callers report it
    can = None

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
BUILD = HERE / ".build"

MASTER_ID = 0x700  # master -> slave: CMD, STIM
SLAVE_ID = 0x701   # slave -> master: RES, ERR, EV, DAQ

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
]


def _run(cmd: list[str]) -> None:
    subprocess.run(cmd, cwd=ROOT, check=True)


def build_bridge(name: str = "default", profile: str | None = None, sets: tuple[str, ...] = ()) -> Path:
    """Compile the slave + bridge. ``profile``/``sets`` select a generated config."""
    BUILD.mkdir(parents=True, exist_ok=True)

    if profile is None and not sets:
        cfg = ROOT / "Gen" / "can"
    else:
        cfg = BUILD / name / "cfg"
        cfg.mkdir(parents=True, exist_ok=True)
        args = [sys.executable, "Tools/xcpgen/xcpgen.py", "--out", str(cfg)]
        if profile is not None:
            args += ["--profile", profile]
        else:
            args += ["--config", "Config/xcp_base.toml"]
        for item in sets:
            args += ["--set", item]
        _run(args)

    suffix = ".dll" if os.name == "nt" else ".so"
    out = BUILD / name / ("xcp_slave_bridge" + suffix)
    out.parent.mkdir(parents=True, exist_ok=True)

    flags = [
        "-shared", "-std=c99", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter",
        "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
        "-ITests/stubs", "-ITests", "-IXcp", f"-I{cfg}",
        "-include", "Tests/test_config.h",
    ]
    flags.append("-static-libgcc" if os.name == "nt" else "-fPIC")
    _run(["gcc", *flags, *SOURCES, str(cfg / "Xcp_Cfg.c"), "-o", str(out)])
    return out


def load_bridge(path: Path) -> ctypes.CDLL:
    lib = ctypes.CDLL(str(path))
    u8p = ctypes.POINTER(ctypes.c_ubyte)

    lib.bridge_init.argtypes = []
    lib.bridge_tick.argtypes = [ctypes.c_uint32]
    lib.bridge_rx.argtypes = [u8p, ctypes.c_uint32]
    lib.bridge_tx_pop.argtypes = [u8p, ctypes.POINTER(ctypes.c_uint32)]
    lib.bridge_tx_pop.restype = ctypes.c_uint32
    lib.bridge_tx_confirm.argtypes = []
    lib.bridge_event.argtypes = [ctypes.c_uint16]
    lib.bridge_os_ticks.restype = ctypes.c_uint32
    lib.bridge_stim_frame_count.restype = ctypes.c_uint32
    lib.bridge_stim_frame_reset.argtypes = []
    lib.bridge_dto_drop_count.restype = ctypes.c_uint32
    lib.bridge_dto_drop_reset.argtypes = []
    lib.bridge_ram_addr.restype = ctypes.c_uint32
    lib.bridge_ram_read.argtypes = [u8p, ctypes.c_uint32]
    lib.bridge_ram_write.argtypes = [u8p, ctypes.c_uint32]
    lib.bridge_flash_read.argtypes = [ctypes.c_uint32, u8p, ctypes.c_uint32]
    lib.bridge_flash_read.restype = ctypes.c_uint32
    lib.bridge_cal_read.argtypes = [ctypes.c_uint8, u8p, ctypes.c_uint32]
    lib.bridge_cal_read.restype = ctypes.c_uint32
    lib.bridge_cal_write.argtypes = [ctypes.c_uint8, u8p, ctypes.c_uint32]
    lib.bridge_cal_rom_write.argtypes = [ctypes.c_uint8, u8p, ctypes.c_uint32]
    return lib


def make_app(channel: str, fd: bool = False, timeout: float = 2.0):
    from pyxcp.config import create_application_from_config

    app = create_application_from_config(
        {
            "Transport": {
                "CAN": {
                    "interface": "virtual",
                    "channel": channel,
                    "can_id_master": MASTER_ID,
                    "can_id_slave": SLAVE_ID,
                    "daq_identifier": [],
                    "fd": fd,
                    "max_dlc_required": False,
                }
            },
            "General": {"timeout": timeout, "disable_error_handling": True},
        },
        log_level=logging.WARNING,
    )
    app.transport.layer = "CAN"
    return app


def open_buses(channel: str):
    master = can.Bus(interface="virtual", channel=channel)
    rx = can.Bus(interface="virtual", channel=channel)
    tx = can.Bus(interface="virtual", channel=channel)
    tap = can.Bus(interface="virtual", channel=channel)
    rx.set_filters([{"can_id": MASTER_ID, "can_mask": 0x7FF, "extended": False}])
    tap.set_filters([{"can_id": SLAVE_ID, "can_mask": 0x7FF, "extended": False}])
    return master, rx, tx, tap


def close_buses(*buses) -> None:
    for bus in buses:
        try:
            bus.shutdown()
        except Exception:
            pass


def drain(bus) -> int:
    count = 0
    while bus.recv(timeout=0) is not None:
        count += 1
    return count


class SlaveLink:
    """Threaded frame pump between the virtual bus and the slave DLL."""

    def __init__(self, lib: ctypes.CDLL, rx_bus, tx_bus, fd: bool = False, tick_ms: int = 1) -> None:
        self.lib = lib
        self.rx_bus = rx_bus
        self.tx_bus = tx_bus
        self.fd = fd
        self.tick_ms = tick_ms
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._threads = [
            threading.Thread(target=self._rx_loop, daemon=True),
            threading.Thread(target=self._tx_loop, daemon=True),
            threading.Thread(target=self._tick_loop, daemon=True),
        ]

    def start(self) -> None:
        with self._lock:
            self.lib.bridge_init()
        for thread in self._threads:
            thread.start()

    def stop(self) -> None:
        self._stop.set()
        for thread in self._threads:
            thread.join(timeout=2.0)

    def event(self, channel: int) -> None:
        with self._lock:
            self.lib.bridge_event(channel)

    def os_ticks(self) -> int:
        with self._lock:
            return int(self.lib.bridge_os_ticks())

    def stim_frame_count(self) -> int:
        with self._lock:
            return int(self.lib.bridge_stim_frame_count())

    def reset_stim_frame_count(self) -> None:
        with self._lock:
            self.lib.bridge_stim_frame_reset()

    def dto_drop_count(self) -> int:
        with self._lock:
            return int(self.lib.bridge_dto_drop_count())

    def reset_dto_drop_count(self) -> None:
        with self._lock:
            self.lib.bridge_dto_drop_reset()

    def ram_addr(self) -> int:
        return int(self.lib.bridge_ram_addr())

    def ram_read(self, length: int) -> bytes:
        buf = (ctypes.c_ubyte * length)()
        self.lib.bridge_ram_read(buf, length)
        return bytes(buf)

    def ram_write(self, data: bytes) -> None:
        buf = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
        self.lib.bridge_ram_write(buf, len(data))

    def flash_read(self, address: int, length: int) -> bytes:
        buf = (ctypes.c_ubyte * length)()
        if self.lib.bridge_flash_read(address, buf, length) != length:
            raise ValueError(f"flash read outside test window: 0x{address:08x}+{length}")
        return bytes(buf)

    def cal_read(self, page: int, length: int) -> bytes:
        buf = (ctypes.c_ubyte * length)()
        if self.lib.bridge_cal_read(page, buf, length) != length:
            raise ValueError("cal read out of range")
        return bytes(buf)

    def cal_write(self, page: int, data: bytes) -> None:
        buf = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
        self.lib.bridge_cal_write(page, buf, len(data))

    def cal_rom_write(self, page: int, data: bytes) -> None:
        buf = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
        self.lib.bridge_cal_rom_write(page, buf, len(data))

    def _rx_loop(self) -> None:
        u8p = ctypes.POINTER(ctypes.c_ubyte)
        while not self._stop.is_set():
            msg = self.rx_bus.recv(timeout=0.05)
            if msg is None or msg.arbitration_id != MASTER_ID:
                continue
            data = (ctypes.c_ubyte * len(msg.data)).from_buffer_copy(msg.data)
            with self._lock:
                self.lib.bridge_rx(data, len(msg.data))

    def _tx_loop(self) -> None:
        buf = (ctypes.c_ubyte * 64)()
        length = ctypes.c_uint32()
        while not self._stop.is_set():
            frames = []
            with self._lock:
                while True:
                    n = self.lib.bridge_tx_pop(buf, ctypes.byref(length))
                    if n == 0:
                        break
                    frames.append(bytes(buf[: length.value]))
            for frame in frames:
                self.tx_bus.send(
                    can.Message(
                        arbitration_id=SLAVE_ID,
                        is_extended_id=False,
                        is_fd=self.fd,
                        data=frame,
                    )
                )
                with self._lock:
                    self.lib.bridge_tx_confirm()
            if not frames:
                time.sleep(0.0002)

    def _tick_loop(self) -> None:
        period = self.tick_ms / 1000.0
        while not self._stop.is_set():
            time.sleep(period)
            with self._lock:
                self.lib.bridge_tick(self.tick_ms)


def unlock(xcp, resource: int) -> bool:
    seed = xcp.getSeed(0, resource)
    key = bytes(value ^ 0xFF for value in seed.seed)
    return bool(xcp.unlock(len(key), key))


def raw_errors() -> None:
    """Make pyxcp raise XcpResponseError instead of running its retry handler."""
    from pyxcp.master.errorhandler import disable_error_handling

    disable_error_handling(True)


def expect_error(fn, *args, **kwargs):
    from pyxcp.types import XcpResponseError

    try:
        fn(*args, **kwargs)
    except XcpResponseError as exc:
        return exc.get_error_code()
    return None
