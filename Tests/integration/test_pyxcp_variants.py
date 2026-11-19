#!/usr/bin/env python3
"""Feature-gated behaviour over the real pyxcp transport.

Each variant is a separately generated profile compiled into its own bridge.
The checks assert both sides of every switch: disabled commands report
ERR_CMD_UNKNOWN and the remaining services stay usable.

Run: python Tests/integration/test_pyxcp_variants.py
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import xcp_bridge as h

if h.can is None:
    print("SKIP: install python-can first")
    sys.exit(2)

try:
    from pyxcp.master import Master
    from pyxcp.types import XcpResponseError
except ImportError as exc:
    print(f"SKIP: install pyxcp first ({exc})")
    sys.exit(2)


class Suite:
    def __init__(self, name: str) -> None:
        self.name = name
        self.passed = 0
        self.failed = 0

    def check(self, label: str, condition: bool, detail: str = "") -> None:
        if condition:
            self.passed += 1
            print(f"  PASS  {label}")
        else:
            self.failed += 1
            print(f"  FAIL  {label}  {detail}")

    def error(self, label: str, fn, *args, code: str, **kwargs) -> None:
        try:
            result = fn(*args, **kwargs)
        except XcpResponseError as exc:
            got = str(exc.get_error_code())
            self.check(label, got == code, f"got {got}")
            return
        self.check(label, False, f"expected {code}, got {result!r}")


def run_variant(
    name: str,
    profile: str | None,
    sets: tuple[str, ...],
    body,
    fd: bool = False,
) -> tuple[int, int]:
    print(f"== {name} ==")
    lib = h.load_bridge(h.build_bridge(name, profile=profile, sets=sets))
    channel = f"xcp-var-{name}-{id(object())}"
    master_bus, rx_bus, tx_bus, tap_bus = h.open_buses(channel)
    link = h.SlaveLink(lib, rx_bus, tx_bus, fd=fd)
    link.start()
    suite = Suite(name)
    try:
        with Master("can", h.make_app(channel, fd=fd, timeout=3.0), transport_layer_interface=master_bus) as xcp:
            xcp.connect()
            body(suite, xcp, link, link.ram_addr())
    finally:
        link.stop()
        h.close_buses(master_bus, rx_bus, tx_bus, tap_bus)
    return suite.passed, suite.failed


def unlock_all(xcp) -> None:
    for resource in (0x01, 0x04, 0x08, 0x10):
        h.unlock(xcp, resource)


def minimal(suite: Suite, xcp, link, addr: int) -> None:
    status = xcp.getStatus()
    suite.check(
        "GET_STATUS reports no protected resources",
        status.resourceProtectionStatus.calpag is False
        and status.resourceProtectionStatus.daq is False
        and status.resourceProtectionStatus.stim is False
        and status.resourceProtectionStatus.pgm is False,
        f"{status}",
    )
    suite.error("GET_SEED disabled -> ERR_CMD_UNKNOWN", xcp.getSeed, 0, 1, code="ERR_CMD_UNKNOWN")
    suite.error("UNLOCK disabled -> ERR_CMD_UNKNOWN", xcp.unlock, 4, b"\0\0\0\0", code="ERR_CMD_UNKNOWN")
    suite.error("DAQ processor info disabled -> ERR_CMD_UNKNOWN", xcp.getDaqProcessorInfo, code="ERR_CMD_UNKNOWN")
    suite.error("FREE_DAQ disabled -> ERR_CMD_UNKNOWN", xcp.freeDaq, code="ERR_CMD_UNKNOWN")
    suite.error("PAG processor info disabled -> ERR_CMD_UNKNOWN", xcp.getPagProcessorInfo, code="ERR_CMD_UNKNOWN")
    suite.error("SET_CAL_PAGE disabled -> ERR_CMD_UNKNOWN", xcp.setCalPage, 2, 0, 1, code="ERR_CMD_UNKNOWN")
    suite.error("PGM processor info disabled -> ERR_CMD_UNKNOWN", xcp.getPgmProcessorInfo, code="ERR_CMD_UNKNOWN")
    suite.error("PROGRAM_START disabled -> ERR_CMD_UNKNOWN", xcp.programStart, code="ERR_CMD_UNKNOWN")
    suite.error(
        "DOWNLOAD_NEXT disabled with block transfer -> ERR_CMD_UNKNOWN",
        xcp.downloadNext, b"\x00", 1, True, code="ERR_CMD_UNKNOWN",
    )
    suite.error(
        "address extension disabled -> ERR_SEGMENT_NOT_VALID",
        xcp.setMta, addr, 0x10, code="ERR_SEGMENT_NOT_VALID",
    )

    xcp.setMta(addr)
    xcp.download(b"\xDE\xAD\xBE\xEF")
    suite.check(
        "plain DOWNLOAD still works",
        link.ram_read(8)[:4] == b"\xDE\xAD\xBE\xEF",
    )
    xcp.setMta(addr + 2)
    suite.check("plain UPLOAD still works", xcp.upload(2) == b"\xBE\xEF")
    ident = xcp.getId(0)
    suite.check(
        "GET_ID + UPLOAD identify the slave",
        ident.length == 17 and xcp.fetch(ident.length) == b"AUTOSAR XCP Slave",
    )
    suite.check("SYNCH still answers", xcp.synch() == b"\x00")


def seedkey_off(suite: Suite, xcp, link, addr: int) -> None:
    status = xcp.getStatus()
    suite.check(
        "resources unprotected without seed/key",
        status.resourceProtectionStatus.calpag is False
        and status.resourceProtectionStatus.daq is False
        and status.resourceProtectionStatus.stim is False
        and status.resourceProtectionStatus.pgm is False,
        f"{status}",
    )
    suite.error("GET_SEED disabled -> ERR_CMD_UNKNOWN", xcp.getSeed, 0, 1, code="ERR_CMD_UNKNOWN")
    suite.error("UNLOCK disabled -> ERR_CMD_UNKNOWN", xcp.unlock, 4, b"\0\0\0\0", code="ERR_CMD_UNKNOWN")

    suite.check("CAL page access without unlock", xcp.getPagProcessorInfo().maxSegments == 2)
    xcp.setCalPage(2, 0, 1)
    suite.check("SET/GET_CAL_PAGE without unlock", xcp.getCalPage(2, 0) == 1)
    xcp.setCalPage(2, 0, 0)

    xcp.freeDaq()
    xcp.allocDaq(1)
    xcp.allocOdt(0, 1)
    xcp.allocOdtEntry(0, 0, 1)
    xcp.setDaqPtr(0, 0, 0)
    xcp.writeDaq(0xFF, 4, 0, addr)
    suite.check("DAQ configuration without unlock", xcp.getDaqListInfo(0).maxOdt == 4)
    xcp.freeDaq()

    suite.check("PGM info without unlock", xcp.getPgmProcessorInfo().maxSector == 4)


def static_daq(suite: Suite, xcp, link, addr: int) -> None:
    unlock_all(xcp)
    proc = xcp.getDaqProcessorInfo()
    suite.check(
        "GET_DAQ_PROCESSOR_INFO reports STATIC with one list",
        str(proc.daqProperties.daqConfigType) == "STATIC" and proc.minDaq == 1,
        f"{proc}",
    )
    suite.error("ALLOC_DAQ rejected in STATIC mode -> ERR_DAQ_CONFIG", xcp.allocDaq, 1, code="ERR_DAQ_CONFIG")
    suite.error("ALLOC_ODT rejected in STATIC mode -> ERR_DAQ_CONFIG", xcp.allocOdt, 0, 1, code="ERR_DAQ_CONFIG")
    info = xcp.getDaqListInfo(0)
    suite.check(
        "predefined static list is visible without allocation",
        info.daqListProperties.predefined is True
        and info.daqListProperties.daq is True
        and info.maxOdt == 1,
        f"{info}",
    )
    xcp.startStopDaqList(1, 0)
    running = xcp.getDaqListMode(0).currentMode.running is True
    xcp.startStopDaqList(0, 0)
    stopped = xcp.getDaqListMode(0).currentMode.running is False
    suite.check("static list starts and stops", running and stopped)


def predef_daq(suite: Suite, xcp, link, addr: int) -> None:
    unlock_all(xcp)
    proc = xcp.getDaqProcessorInfo()
    suite.check(
        "GET_DAQ_PROCESSOR_INFO reports two predefined lists",
        proc.minDaq == 2 and str(proc.daqProperties.daqConfigType) == "DYNAMIC",
        f"{proc}",
    )
    suite.check(
        "predefined list flags are set",
        xcp.getDaqListInfo(0).daqListProperties.predefined is True
        and xcp.getDaqListInfo(1).daqListProperties.predefined is True,
    )
    suite.error(
        "ALLOC_DAQ beyond the pool -> ERR_MEMORY_OVERFLOW",
        xcp.allocDaq, 7, code="ERR_MEMORY_OVERFLOW",
    )
    xcp.allocDaq(4)
    xcp.setDaqPtr(1, 0, 0)
    suite.error(
        "write-protected predefined list -> ERR_WRITE_PROTECTED",
        xcp.writeDaq, 0xFF, 4, 0, addr, code="ERR_WRITE_PROTECTED",
    )
    xcp.startStopDaqList(1, 0)
    running = xcp.getDaqListMode(0).currentMode.running is True
    xcp.startStopDaqList(0, 0)
    suite.check("predefined list starts and stops", running)
    xcp.freeDaq()


def interleaved(suite: Suite, xcp, link, addr: int) -> None:
    unlock_all(xcp)
    comm = xcp.getCommModeInfo()
    suite.check(
        "GET_COMM_MODE_INFO advertises interleaved mode",
        comm.commModeOptional.interleavedMode is True,
        f"{comm}",
    )
    xcp.setMta(addr)
    xcp.download(b"\x01\x02\x03\x04")
    xcp.setMta(addr)
    suite.check("command channel still works", xcp.upload(4) == b"\x01\x02\x03\x04")
    suite.check("status still works", xcp.getStatus().sessionConfiguration == 0)


def fd_limits(suite: Suite, xcp, link, addr: int) -> None:
    unlock_all(xcp)
    suite.check(
        "CONNECT negotiates 64-byte CTO and DTO",
        xcp.slaveProperties.maxCto == 64 and xcp.slaveProperties.maxDto == 64,
        f"maxCto={xcp.slaveProperties.maxCto} maxDto={xcp.slaveProperties.maxDto}",
    )

    # Payload-carrying SHORT_DOWNLOAD only fits when the CTO exceeds 8 bytes.
    xcp.shortDownload(addr + 36, 0, b"\x11\x22\x33\x44")
    suite.check(
        "SHORT_DOWNLOAD carries a payload on CAN-FD",
        link.ram_read(64)[36:40] == b"\x11\x22\x33\x44",
        f"got {link.ram_read(64)[36:40]!r}",
    )

    link.ram_write(bytes(range(128)))
    suite.check(
        "SHORT_UPLOAD returns 60 bytes in one CTO",
        xcp.shortUpload(60, addr) == bytes(range(60)),
    )

    payload = bytes(range(62))
    xcp.setMta(addr + 64)
    xcp.download(payload)
    suite.check(
        "DOWNLOAD accepts a 62-byte payload",
        link.ram_read(128)[64:126] == payload,
    )
    xcp.setMta(addr + 64)
    suite.check("UPLOAD returns 62 bytes", xcp.upload(62) == payload)

    xcp.setMta(addr)
    suite.check(
        "BUILD_CHECKSUM covers 60 bytes",
        xcp.buildChecksum(60).checksum == sum(range(60)),
    )


VARIANTS = [
    (
        "minimal",
        None,
        (
            "XcpDaqEnabled=0",
            "XcpResumeModeEnabled=0",
            "XcpSeedKeyEnabled=0",
            "XcpCalibrationPageEnabled=0",
            "XcpFlashProgrammingEnabled=0",
            "XcpBlockTransferEnabled=0",
            "XcpSuppressTxSupport=0",
            "XcpDevErrorDetect=0",
            "XcpVersionInfoApi=0",
        ),
        minimal,
        False,
    ),
    ("seedkey_off", None, ("XcpSeedKeyEnabled=0",), seedkey_off, False),
    ("daq_static", "static", (), static_daq, False),
    ("daq_predef", "predef", (), predef_daq, False),
    ("interleaved", None, ("XcpInterleavedModeEnabled=1",), interleaved, False),
    (
        "can_fd",
        None,
        ("XcpMaxCto=64", "XcpMaxDto=64", "XcpOdtEntriesCount=15"),
        fd_limits,
        True,
    ),
]


def main() -> int:
    total_passed = 0
    total_failed = 0
    for name, profile, sets, body, fd in VARIANTS:
        passed, failed = run_variant(name, profile, sets, body, fd=fd)
        total_passed += passed
        total_failed += failed
    print(f"================ pyxcp variants: {total_passed} passed, {total_failed} failed ================")
    return 1 if total_failed else 0


if __name__ == "__main__":
    sys.exit(main())
