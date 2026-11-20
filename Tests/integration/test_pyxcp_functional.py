#!/usr/bin/env python3
"""Comprehensive functional test: every slave service reachable through pyxcp.

The full-featured CAN profile is used (CAL, DAQ, STIM, PGM, RESUME, block
transfer, absolute PID). pyxcp drives the real XCPonCAN path via the bridge;
raw transport requests cover services pyxcp does not wrap.

Run: python Tests/integration/test_pyxcp_functional.py
"""

from __future__ import annotations

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
    import pyxcp.types as t
    from pyxcp.types import XcpResponseError
except ImportError as exc:
    print(f"SKIP: install pyxcp first ({exc})")
    sys.exit(2)

RES_CAL = 0x01
RES_DAQ = 0x04
RES_STIM = 0x08
RES_PGM = 0x10


class Suite:
    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0

    def check(self, name: str, condition: bool, detail: str = "") -> None:
        if condition:
            self.passed += 1
            print(f"  PASS  {name}")
        else:
            self.failed += 1
            print(f"  FAIL  {name}  {detail}")

    def error(self, name: str, fn, *args, code: str | None = None, **kwargs) -> None:
        try:
            result = fn(*args, **kwargs)
        except XcpResponseError as exc:
            got = str(exc.get_error_code())
            self.check(name, code is None or got == code, f"got {got}")
            return
        self.check(name, False, f"expected {code or 'error'}, got {result!r}")


def u32(data: bytes, offset: int = 0) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def main() -> int:
    suite = Suite()
    lib = h.load_bridge(h.build_bridge())
    channel = f"xcp-func-{id(object())}"
    master_bus, rx_bus, tx_bus, tap_bus = h.open_buses(channel)
    link = h.SlaveLink(lib, rx_bus, tx_bus)
    link.start()

    try:
        with Master("can", h.make_app(channel, timeout=3.0), transport_layer_interface=master_bus) as xcp:
            xcp.connect()
            addr = link.ram_addr()
            suite.check(
                "CONNECT advertises slave block mode",
                xcp.slaveProperties.slaveBlockMode is True,
            )

            # ---------------- A. connection / protection ----------------
            print("-- A. connection, status, identity")
            comm = xcp.getCommModeInfo()
            suite.check(
                "GET_COMM_MODE_INFO: single-command CAN, driver v1.0",
                comm.maxBs == 0
                and comm.minSt == 0
                and comm.queueSize == 0
                and comm.commModeOptional.interleavedMode is False
                and comm.xcpDriverVersionNumber == 0x10,
                f"{comm}",
            )

            status = xcp.getStatus()
            suite.check(
                "GET_STATUS: every resource protected before unlock",
                status.sessionConfiguration == 0
                and status.resourceProtectionStatus.calpag is True
                and status.resourceProtectionStatus.daq is True
                and status.resourceProtectionStatus.stim is True
                and status.resourceProtectionStatus.pgm is True,
                f"{status}",
            )

            locked_calls = [
                ("DOWNLOAD while CAL locked", xcp.download, (b"\x00",)),
                ("SHORT_DOWNLOAD while CAL locked", xcp.shortDownload, (addr + 4, 0, b"")),
                ("MODIFY_BITS while CAL locked", xcp.modifyBits, (0, 0xFFFF, 0)),
                ("SET_CAL_PAGE while CAL locked", xcp.setCalPage, (2, 0, 1)),
                ("COPY_CAL_PAGE while CAL locked", xcp.copyCalPage, (0, 0, 0, 1)),
                ("FREE_DAQ while DAQ locked", xcp.freeDaq, ()),
                ("ALLOC_DAQ while DAQ locked", xcp.allocDaq, (1,)),
                ("ALLOC_ODT while DAQ locked", xcp.allocOdt, (0, 1)),
                ("ALLOC_ODT_ENTRY while DAQ locked", xcp.allocOdtEntry, (0, 0, 1)),
                ("WRITE_DAQ while DAQ locked", xcp.writeDaq, (0xFF, 4, 0, addr)),
                ("SET_DAQ_LIST_MODE while DAQ locked", xcp.setDaqListMode, (0, 0, 0, 1, 0)),
                ("START_STOP_DAQ_LIST while DAQ locked", xcp.startStopDaqList, (1, 0)),
                ("START_STOP_SYNCH while DAQ locked", xcp.startStopSynch, (1,)),
                ("CLEAR_DAQ_LIST while DAQ locked", xcp.clearDaqList, (0,)),
                ("PROGRAM_START while PGM locked", xcp.programStart, ()),
                ("PROGRAM_PREPARE while PGM locked", xcp.programPrepare, (16,)),
                ("PROGRAM_CLEAR while PGM locked", xcp.programClear, (0, 0x100)),
                ("PROGRAM while PGM locked", xcp.program, (b"\x00", 1, True)),
                ("PROGRAM_VERIFY while PGM locked", xcp.programVerify, (0, 0, 0)),
                ("PROGRAM_RESET while PGM locked", xcp.programReset, (False,)),
            ]
            for label, fn, args in locked_calls:
                suite.error(f"{label} -> ERR_ACCESS_LOCKED", fn, *args, code="ERR_ACCESS_LOCKED")

            suite.error(
                "GET_SEED mode 2 -> ERR_MODE_NOT_VALID",
                xcp.getSeed, 2, RES_CAL, code="ERR_MODE_NOT_VALID",
            )
            suite.error(
                "GET_SEED invalid resource -> ERR_OUT_OF_RANGE",
                xcp.getSeed, 0, 0x02, code="ERR_OUT_OF_RANGE",
            )
            suite.error(
                "GET_SEED continuation without seed -> ERR_SEQUENCE",
                xcp.getSeed, 1, RES_CAL, code="ERR_SEQUENCE",
            )

            for resource, field in (
                (RES_CAL, "calpag"),
                (RES_DAQ, "daq"),
                (RES_STIM, "stim"),
                (RES_PGM, "pgm"),
            ):
                seed = xcp.getSeed(0, resource)
                suite.check(
                    f"GET_SEED pattern for resource 0x{resource:02X}",
                    seed.length == 4
                    and seed.seed[0] == resource
                    and seed.seed[1] == (resource ^ 0x5A)
                    and seed.seed[2] == (resource ^ 0xA5)
                    and seed.seed[3] == 0x42,
                    f"{seed}",
                )
                key = bytes(value ^ 0xFF for value in seed.seed)
                xcp.unlock(len(key), key)
                status = xcp.getStatus()
                suite.check(
                    f"UNLOCK clears the {field} protection bit",
                    getattr(status.resourceProtectionStatus, field) is False,
                    f"{status}",
                )

            status = xcp.getStatus()
            suite.check(
                "GET_STATUS: unprotected after unlock, session 0",
                status.sessionConfiguration == 0
                and status.resourceProtectionStatus.calpag is False
                and status.resourceProtectionStatus.daq is False
                and status.resourceProtectionStatus.stim is False
                and status.resourceProtectionStatus.pgm is False,
                f"{status}",
            )

            ident = xcp.getId(0)
            ident_data = xcp.fetch(ident.length)
            suite.check(
                "GET_ID mode 0 + chunked UPLOAD",
                ident.length == 17 and ident_data == b"AUTOSAR XCP Slave",
                f"len={ident.length} data={ident_data!r}",
            )
            suite.check(
                "GET_ID modes 1..9 return length 0",
                all(xcp.getId(mode).length == 0 for mode in range(1, 10)),
            )

            synch = xcp.synch()
            suite.check("SYNCH returns ERR_CMD_SYNCH", synch == b"\x00", f"{synch!r}")

            slave_id = xcp.transportLayerCmd(0xFF)
            suite.check("TL_CMD GET_SLAVE_ID -> 'XCP'", slave_id[-3:] == b"XCP", f"{slave_id!r}")
            suite.error(
                "TL_CMD unknown sub-command -> ERR_CMD_UNKNOWN",
                xcp.transportLayerCmd, 0x01, code="ERR_CMD_UNKNOWN",
            )

            # ---------------- B. memory access ----------------
            print("-- B. memory access")
            pattern = bytes(range(256))
            link.ram_write(pattern)

            suite.error(
                "DOWNLOAD_NEXT without an active block -> ERR_SEQUENCE",
                xcp.downloadNext, b"\x00", 1, True, code="ERR_SEQUENCE",
            )
            suite.error(
                "DOWNLOAD declared payload beyond CTO -> ERR_OUT_OF_RANGE",
                xcp.transport.request, t.Command.DOWNLOAD, 5, 1, 2,
                code="ERR_OUT_OF_RANGE",
            )
            suite.error(
                "short UPLOAD from denied address -> ERR_ACCESS_DENIED",
                xcp.shortUpload, 4, 0xDEAD0000, code="ERR_ACCESS_DENIED",
            )

            xcp.setMta(addr)
            suite.check("SET_MTA + UPLOAD 4B", xcp.upload(4) == pattern[:4])
            suite.check("UPLOAD advances MTA", xcp.upload(4) == pattern[4:8])
            suite.check(
                "UPLOAD zero elements -> empty RES",
                xcp.upload(0) == b"",
            )
            suite.check(
                "SHORT_UPLOAD at +16",
                xcp.shortUpload(4, addr + 16) == pattern[16:20],
            )
            xcp.setMta(addr)
            suite.check(
                "UPLOAD block mode streams 64 bytes",
                xcp.upload(64) == pattern[:64],
            )
            suite.check(
                "block UPLOAD advances MTA",
                xcp.upload(4) == pattern[64:68],
            )
            suite.error(
                "SHORT_UPLOAD 8B > MAX_CTO-1 -> ERR_OUT_OF_RANGE",
                xcp.shortUpload, 8, addr, code="ERR_OUT_OF_RANGE",
            )

            xcp.setMta(addr + 32)
            xcp.download(b"\xde\xad\xbe\xef")
            suite.check(
                "SET_MTA + DOWNLOAD writes RAM",
                link.ram_read(64)[32:36] == b"\xde\xad\xbe\xef",
            )

            # Classic CAN: the 8-byte header fills the whole CTO, so a
            # payload-carrying SHORT_DOWNLOAD cannot be transported; the
            # zero-payload form and the overflow diagnostic are testable here.
            xcp.shortDownload(addr + 36, 0, b"")
            suite.error(
                "SHORT_DOWNLOAD declared payload beyond CTO -> ERR_OUT_OF_RANGE",
                xcp.transport.request, t.Command.SHORT_DOWNLOAD,
                1, 0, 0, *struct.pack("<I", addr + 36), code="ERR_OUT_OF_RANGE",
            )

            # Block download: DOWNLOAD arms block mode, DOWNLOAD_NEXT continues it.
            xcp.setMta(addr + 40)
            xcp.download(b"\x01\x02\x03\x04")
            xcp.downloadNext(b"\xc0\xde", 2, last=True)
            suite.check(
                "DOWNLOAD + DOWNLOAD_NEXT block sequence",
                link.ram_read(64)[40:46] == b"\x01\x02\x03\x04\xc0\xde",
            )
            xcp.setMta(addr + 48)
            xcp.downloadMax(b"\x10\x20\x30\x40")
            suite.check(
                "DOWNLOAD_MAX writes and advances MTA",
                link.ram_read(64)[48:52] == b"\x10\x20\x30\x40"
                and xcp.upload(1) == pattern[52:53],
            )

            xcp.setMta(addr + 52)
            xcp.download(b"\xff\xff\xff\xff")
            xcp.setMta(addr + 52)
            xcp.modifyBits(15, 0xF0, 0x05)
            suite.check(
                # ((old & ~((~0xF0) << 15)) ^ (0x05 << 15)) = 0x807AFFFF
                "MODIFY_BITS applies the AND/XOR mask formula",
                u32(link.ram_read(64), 52) == 0x807AFFFF,
                f"got 0x{u32(link.ram_read(64), 52):08X}",
            )
            suite.error(
                "MODIFY_BITS shift 17 -> ERR_CMD_SYNTAX",
                xcp.modifyBits, 17, 0xFFFF, 0xFFFF, code="ERR_CMD_SYNTAX",
            )

            xcp.setMta(addr)
            checksum = xcp.buildChecksum(16)
            suite.check(
                "BUILD_CHECKSUM over 16 bytes",
                checksum.checksum == sum(pattern[:16]),
                f"got {checksum.checksum}",
            )

            # Address extension high nibble selects the segment; 3 > max 2.
            suite.error(
                "SET_MTA invalid extension -> ERR_SEGMENT_NOT_VALID",
                xcp.setMta, addr, 0x31, code="ERR_SEGMENT_NOT_VALID",
            )
            xcp.setMta(0xDEAD0000)
            suite.error(
                "UPLOAD from denied address -> ERR_ACCESS_DENIED",
                xcp.upload, 4, code="ERR_ACCESS_DENIED",
            )

            # ---------------- C. calibration pages ----------------
            print("-- C. calibration pages")
            pag = xcp.getPagProcessorInfo()
            suite.check("GET_PAG_PROCESSOR_INFO: 2 segments", pag.maxSegments == 2, f"{pag}")

            seg0 = xcp.getSegmentInfo(0, 0, 0, 0)
            seg0_len = xcp.getSegmentInfo(0, 0, 1, 0)
            seg0_map = xcp.getSegmentInfo(2, 0, 0, 0)
            suite.check(
                "GET_SEGMENT_INFO mode0 start/length",
                seg0.basicInfo == 0x20000000 and seg0_len.basicInfo == 0x1000,
                f"{seg0} {seg0_len}",
            )
            suite.check(
                "GET_SEGMENT_INFO mode2 mapping",
                seg0_map.mappingInfo == 0x20000000,
                f"{seg0_map}",
            )
            suite.error(
                "GET_SEGMENT_INFO out-of-range info -> ERR_OUT_OF_RANGE",
                xcp.getSegmentInfo, 0, 0, 2, 0, code="ERR_OUT_OF_RANGE",
            )

            page_ref = xcp.getPageInfo(0, 0)
            page_work = xcp.getPageInfo(0, 1)
            suite.check(
                "GET_PAGE_INFO: reference read-only, working read/write",
                page_ref.properties.xcp_read_access_with_ecu is True
                and page_ref.properties.xcp_write_access_with_ecu is False
                and page_work.properties.xcp_write_access_with_ecu is True,
                f"{page_ref} {page_work}",
            )

            suite.check("GET_CAL_PAGE mode1 page0", xcp.getCalPage(1, 0) == 0)
            xcp.setCalPage(2, 0, 1)
            suite.check("SET_CAL_PAGE then GET_CAL_PAGE page1", xcp.getCalPage(2, 0) == 1)
            xcp.setCalPage(2, 0, 0)
            suite.error(
                "SET_CAL_PAGE bad page -> ERR_PAGE_NOT_VALID",
                xcp.setCalPage, 0x82, 0, 9, code="ERR_PAGE_NOT_VALID",
            )
            suite.error(
                "SET_CAL_PAGE bad segment -> ERR_SEGMENT_NOT_VALID",
                xcp.setCalPage, 0x02, 9, 0, code="ERR_SEGMENT_NOT_VALID",
            )
            suite.error(
                "GET_PAGE_INFO bad page -> ERR_PAGE_NOT_VALID",
                xcp.getPageInfo, 0, 2, code="ERR_PAGE_NOT_VALID",
            )
            suite.error(
                "GET_CAL_PAGE mode 3 -> ERR_MODE_NOT_VALID",
                xcp.getCalPage, 3, 0, code="ERR_MODE_NOT_VALID",
            )
            suite.error(
                "SET_CAL_PAGE mode 0 -> ERR_MODE_NOT_VALID",
                xcp.setCalPage, 0, 0, 1, code="ERR_MODE_NOT_VALID",
            )

            # mode 0x83 applies the page to both access paths of every segment.
            xcp.setCalPage(0x83, 0, 1)
            suite.check(
                "SET_CAL_PAGE ALL sets ECU and XCP pages on both segments",
                xcp.getCalPage(1, 0) == 1
                and xcp.getCalPage(2, 0) == 1
                and xcp.getCalPage(1, 1) == 1
                and xcp.getCalPage(2, 1) == 1,
            )
            xcp.setCalPage(0x83, 0, 0)

            suite.error(
                "SET_SEGMENT_MODE freeze unsupported -> ERR_MODE_NOT_VALID",
                xcp.setSegmentMode, 1, 0, code="ERR_MODE_NOT_VALID",
            )
            suite.check("SET_SEGMENT_MODE normal mode", xcp.setSegmentMode(0, 0) == b"")
            suite.check("GET_SEGMENT_MODE returns 0", xcp.getSegmentMode(0) == 0)
            suite.error(
                "GET_SEGMENT_MODE bad segment -> ERR_SEGMENT_NOT_VALID",
                xcp.getSegmentMode, 9, code="ERR_SEGMENT_NOT_VALID",
            )

            cal_pattern = bytes([0x5A]) * 0x1000
            link.cal_rom_write(0, cal_pattern)
            xcp.copyCalPage(0, 0, 0, 1)
            suite.check(
                "COPY_CAL_PAGE copies ROM page0 to RAM page1",
                link.cal_read(1, 0x1000) == cal_pattern,
            )
            suite.error(
                "COPY_CAL_PAGE into reference page -> ERR_WRITE_PROTECTED",
                xcp.copyCalPage, 0, 0, 0, 0, code="ERR_WRITE_PROTECTED",
            )
            suite.error(
                "COPY_CAL_PAGE bad page -> ERR_PAGE_NOT_VALID",
                xcp.copyCalPage, 0, 0, 0, 9, code="ERR_PAGE_NOT_VALID",
            )
            suite.error(
                "COPY_CAL_PAGE bad segment -> ERR_SEGMENT_NOT_VALID",
                xcp.copyCalPage, 9, 0, 0, 1, code="ERR_SEGMENT_NOT_VALID",
            )

            # ---------------- D. DAQ ----------------
            print("-- D. DAQ")
            proc = xcp.getDaqProcessorInfo()
            suite.check(
                "GET_DAQ_PROCESSOR_INFO: dynamic, 8 lists, 4 events",
                str(proc.daqProperties.daqConfigType) == "DYNAMIC"
                and proc.maxDaq == 8
                and proc.maxEventChannel == 4
                and proc.minDaq == 0
                and proc.daqProperties.pidOffSupported is False,
                f"{proc}",
            )

            resolution = xcp.getDaqResolutionInfo()
            suite.check(
                "GET_DAQ_RESOLUTION_INFO: 4B entry, 4B/1ms timestamp",
                resolution.maxOdtEntrySizeDaq == 4
                and resolution.maxOdtEntrySizeStim == 4
                and int(resolution.timestampMode.size) == 4
                and resolution.timestampTicks == 1,
                f"{resolution}",
            )

            ev0 = xcp.getDaqEventInfo(0)
            ev2 = xcp.getDaqEventInfo(2)
            ev3 = xcp.getDaqEventInfo(3)
            suite.check(
                "GET_DAQ_EVENT_INFO: event0 DAQ+STIM 10ms, event2 DAQ only, event3 STIM only",
                ev0.daqEventProperties.daq is True
                and ev0.daqEventProperties.stim is True
                and ev0.eventChannelTimeCycle == 10
                and ev2.daqEventProperties.daq is True
                and ev2.daqEventProperties.stim is False
                and ev3.daqEventProperties.daq is False
                and ev3.daqEventProperties.stim is True,
                f"{ev0} {ev2} {ev3}",
            )

            xcp.freeDaq()
            xcp.allocDaq(8)
            for daq_list in range(8):
                xcp.allocOdt(daq_list, 4)
                xcp.allocOdtEntry(daq_list, 0, 7)
            info = xcp.getDaqListInfo(0)
            suite.check(
                "ALLOC_DAQ/ODT/ENTRY to maximum -> list info",
                info.maxOdt == 4 and info.maxOdtEntries == 7
                and info.daqListProperties.predefined is False,
                f"{info}",
            )
            suite.error(
                "ALLOC_DAQ beyond maximum -> ERR_MEMORY_OVERFLOW",
                xcp.allocDaq, 9, code="ERR_MEMORY_OVERFLOW",
            )

            xcp.setDaqPtr(0, 0, 0)
            xcp.writeDaq(0xFF, 4, 0, addr)
            # WRITE_DAQ advances the ODT entry pointer; point back at entry 0.
            xcp.setDaqPtr(0, 0, 0)
            entry = xcp.readDaq()
            suite.check(
                "SET_DAQ_PTR + WRITE_DAQ + READ_DAQ",
                entry.bitOffset == 0xFF
                and entry.sizeofDaqElement == 4
                and entry.address == addr,
                f"{entry}",
            )

            xcp.setDaqListMode(0, 0, 0, 1, 0)
            mode = xcp.getDaqListMode(0)
            suite.check(
                "SET/GET_DAQ_LIST_MODE: DAQ, event0, prescaler1",
                mode.currentEventChannel == 0
                and mode.currentPrescaler == 1
                and str(mode.currentMode.direction) == "DAQ",
                f"{mode}",
            )
            suite.error(
                "SET_DAQ_LIST_MODE PID_OFF -> ERR_MODE_NOT_VALID",
                xcp.setDaqListMode, 0x20, 0, 0, 1, 0, code="ERR_MODE_NOT_VALID",
            )
            suite.error(
                "DAQ list on STIM-only event -> ERR_DAQ_CONFIG",
                xcp.setDaqListMode, 0x00, 0, 3, 1, 0, code="ERR_DAQ_CONFIG",
            )

            # One live list with a known value.
            xcp.freeDaq()
            xcp.allocDaq(1)
            xcp.allocOdt(0, 1)
            xcp.allocOdtEntry(0, 0, 1)
            xcp.setDaqPtr(0, 0, 0)
            xcp.writeDaq(0xFF, 4, 0, addr)
            xcp.setDaqListMode(0, 0, 0, 1, 0)
            link.ram_write(bytes([0x44, 0x33, 0x22, 0x11]))
            h.drain(tap_bus)
            first = xcp.startStopDaqList(1, 0)
            dto = None
            deadline = time.time() + 1.0
            while time.time() < deadline and dto is None:
                frame = tap_bus.recv(timeout=0.05)
                if frame is not None and len(frame.data) >= 5 and frame.data[0] < 0xFC:
                    dto = bytes(frame.data)
            suite.check(
                "START_DAQ_LIST emits DTO with sampled RAM value",
                dto is not None
                and dto[0] == first.firstPid
                and dto[1:5] == b"\x44\x33\x22\x11",
                f"pid={first.firstPid} dto={dto!r}",
            )
            xcp.setDaqPtr(0, 0, 0)
            suite.error(
                "WRITE_DAQ while running -> ERR_DAQ_ACTIVE",
                xcp.writeDaq, 0xFF, 4, 0, addr, code="ERR_DAQ_ACTIVE",
            )
            xcp.startStopDaqList(0, 0)

            # Timestamped DAQ: PID + 4B timestamp + 1B value.
            xcp.clearDaqList(0)
            xcp.allocOdt(0, 1)
            xcp.allocOdtEntry(0, 0, 1)
            xcp.setDaqPtr(0, 0, 0)
            xcp.writeDaq(0xFF, 1, 0, addr + 200)
            xcp.setDaqListMode(0x10, 0, 0, 1, 0)
            timestamped_ram = bytearray(256)
            timestamped_ram[200] = 0x99
            link.ram_write(bytes(timestamped_ram))
            h.drain(tap_bus)
            xcp.startStopDaqList(1, 0)
            stamped = None
            deadline = time.time() + 1.0
            while time.time() < deadline and stamped is None:
                frame = tap_bus.recv(timeout=0.05)
                if frame is not None and len(frame.data) >= 6 and frame.data[0] < 0xFC:
                    stamped = bytes(frame.data)
            suite.check(
                "timestamped DAQ DTO carries 4B timestamp and value",
                stamped is not None and stamped[5] == 0x99,
                f"dto={stamped!r}",
            )
            clock = xcp.getDaqClock()
            suite.check("GET_DAQ_CLOCK returns a timestamp", clock.timestamp > 0, f"{clock}")
            xcp.startStopDaqList(0, 0)
            xcp.freeDaq()

            xcp.allocDaq(1)
            suite.error(
                "START_STOP_DAQ_LIST mode 3 -> ERR_MODE_NOT_VALID",
                xcp.startStopDaqList, 3, 0, code="ERR_MODE_NOT_VALID",
            )
            xcp.freeDaq()
            suite.error(
                "CLEAR_DAQ_LIST out of range -> ERR_OUT_OF_RANGE",
                xcp.clearDaqList, 9, code="ERR_OUT_OF_RANGE",
            )
            suite.error(
                "SET_DAQ_PTR out of range -> ERR_OUT_OF_RANGE",
                xcp.setDaqPtr, 9, 0, 0, code="ERR_OUT_OF_RANGE",
            )

            xcp.freeDaq()
            xcp.allocDaq(1)
            xcp.allocOdt(0, 1)
            xcp.allocOdtEntry(0, 0, 1)
            xcp.setDaqPtr(0, 0, 0)
            xcp.writeDaq(0xFF, 4, 0, addr)
            xcp.setDaqListMode(0x80, 0, 0, 1, 0)
            mode = xcp.getDaqListMode(0)
            suite.check(
                "SET/GET_DAQ_LIST_MODE carries the resume bit",
                mode.currentMode.resume is True,
                f"{mode}",
            )
            xcp.freeDaq()

            # Two lists started and stopped through START_STOP_SYNCH.
            xcp.allocDaq(2)
            for daq_list in range(2):
                xcp.allocOdt(daq_list, 1)
                xcp.allocOdtEntry(daq_list, 0, 1)
                xcp.setDaqPtr(daq_list, 0, 0)
                xcp.writeDaq(0xFF, 4, 0, addr + 4 * daq_list)
                xcp.setDaqListMode(0, daq_list, 1, 1, 0)
            sync_ram = bytearray(16)
            struct.pack_into("<II", sync_ram, 0, 0x11223344, 0x55667788)
            link.ram_write(bytes(sync_ram))
            h.drain(tap_bus)

            xcp.startStopDaqList(2, 0)
            xcp.startStopDaqList(2, 1)
            xcp.startStopSynch(1)
            seen = {}
            deadline = time.time() + 1.0
            while time.time() < deadline and len(seen) < 2:
                frame = tap_bus.recv(timeout=0.05)
                if frame is not None and len(frame.data) >= 5 and frame.data[0] < 0xFC:
                    seen[frame.data[0]] = bytes(frame.data[1:5])
            suite.check(
                "START_STOP_SYNCH starts both selected lists",
                seen.get(0) == b"\x44\x33\x22\x11" and seen.get(1) == b"\x88\x77\x66\x55",
                f"{seen}",
            )
            block_pattern = bytes((i * 3 + 1) & 0xFF for i in range(64))
            link.ram_write(block_pattern)
            xcp.setMta(addr)
            suite.check(
                "block UPLOAD while DAQ is running",
                xcp.upload(64) == block_pattern,
            )
            # START_STOP_SYNCH mode 1 consumes the selection; re-select before
            # stopping through the selected-lists path.
            xcp.startStopDaqList(2, 0)
            xcp.startStopDaqList(2, 1)
            xcp.startStopSynch(2)
            time.sleep(0.05)
            h.drain(tap_bus)
            stopped = xcp.getDaqListMode(0).currentMode.running is False
            stopped = stopped and xcp.getDaqListMode(1).currentMode.running is False
            suite.check("START_STOP_SYNCH stops both lists", stopped)
            xcp.freeDaq()

            # ---------------- E. STIM ----------------
            print("-- E. STIM")
            xcp.freeDaq()
            xcp.allocDaq(1)
            xcp.allocOdt(0, 1)
            xcp.allocOdtEntry(0, 0, 1)
            xcp.setDaqPtr(0, 0, 0)
            xcp.writeDaq(0xFF, 4, 0, addr + 220)
            xcp.setDaqListMode(0x02, 0, 0, 1, 0)
            info = xcp.getDaqListInfo(0)
            stim_mode = xcp.getDaqListMode(0)
            suite.check(
                # Dynamic lists advertise both directions; the current
                # direction is reported by GET_DAQ_LIST_MODE.
                "GET_DAQ_LIST_INFO / MODE report the STIM direction",
                info.daqListProperties.stim is True
                and info.daqListProperties.daq is True
                and info.maxOdt == 4
                and info.maxOdtEntries == 7
                and str(stim_mode.currentMode.direction) == "STIM",
                f"{info} {stim_mode}",
            )
            link.ram_write(bytes([0, 0, 0, 0]))
            xcp.startStopDaqList(1, 0)
            # Absolute PID 0 for list0/ODT0; send a raw STIM frame on the CMD ID.
            master_bus.send(
                h.can.Message(
                    arbitration_id=h.MASTER_ID,
                    is_extended_id=False,
                    data=bytes([0x00, 0x11, 0x22, 0x33, 0x44]),
                )
            )
            deadline = time.time() + 0.5
            while time.time() < deadline and link.ram_read(256)[220:224] != b"\x11\x22\x33\x44":
                time.sleep(0.005)
            suite.check(
                "STIM frame writes the configured entry",
                link.ram_read(256)[220:224] == b"\x11\x22\x33\x44",
                f"got {link.ram_read(256)[220:224]!r}",
            )
            xcp.startStopDaqList(0, 0)
            xcp.freeDaq()

            # ---------------- F. flash programming ----------------
            print("-- F. flash programming")
            pgm = xcp.getPgmProcessorInfo()
            suite.check(
                "GET_PGM_PROCESSOR_INFO: absolute mode, 4 sectors",
                pgm.pgmProperties.absoluteMode is True and pgm.maxSector == 4,
                f"{pgm}",
            )
            sector0 = xcp.getSectorInfo(0, 0)
            sector0_len = xcp.getSectorInfo(1, 0)
            suite.check(
                "GET_SECTOR_INFO mode0/1",
                sector0.sectorInfo == 0x08000000 and sector0_len.sectorInfo == 0x4000,
                f"{sector0} {sector0_len}",
            )
            suite.error(
                "GET_SECTOR_INFO bad sector -> ERR_OUT_OF_RANGE",
                xcp.getSectorInfo, 0, 9, code="ERR_OUT_OF_RANGE",
            )

            suite.error(
                "PROGRAM_PREPARE before PROGRAM_START -> ERR_SEQUENCE",
                xcp.programPrepare, 16, code="ERR_SEQUENCE",
            )
            suite.error(
                "PROGRAM_CLEAR before PROGRAM_START -> ERR_SEQUENCE",
                xcp.programClear, 0, 0x100, code="ERR_SEQUENCE",
            )
            suite.error(
                "PROGRAM_RESET before PROGRAM_START -> ERR_SEQUENCE",
                xcp.programReset, False, code="ERR_SEQUENCE",
            )

            xcp.programStart()
            suite.error(
                "PROGRAM_PREPARE over 32 bytes -> ERR_OUT_OF_RANGE",
                xcp.programPrepare, 33, code="ERR_OUT_OF_RANGE",
            )
            xcp.programPrepare(16)
            xcp.programFormat(0, 0, 1, 0)
            suite.check("PROGRAM_FORMAT accepts absolute programming", True)
            suite.error(
                "PROGRAM_FORMAT with compression -> ERR_OUT_OF_RANGE",
                xcp.programFormat, 1, 0, 1, 0, code="ERR_OUT_OF_RANGE",
            )
            suite.error(
                "PROGRAM_FORMAT with relative mode -> ERR_MODE_NOT_VALID",
                xcp.programFormat, 0, 0, 0, 0, code="ERR_MODE_NOT_VALID",
            )
            suite.error(
                "PROGRAM_FORMAT with bad access method -> ERR_MODE_NOT_VALID",
                xcp.programFormat, 0, 0, 1, 2, code="ERR_MODE_NOT_VALID",
            )
            xcp.setMta(0x08000000)
            xcp.programClear(0, 0x4000)
            suite.check(
                "PROGRAM_CLEAR erases sector 0",
                link.flash_read(0x08000000, 4) == b"\xff\xff\xff\xff",
            )
            # PROGRAM_CLEAR moves the MTA to the end of the cleared range.
            xcp.setMta(0x08000000)
            xcp.program(b"\x11\x22\x33\x44", 4, last=True)
            xcp.programNext(b"\x55\x66", 2, last=True)
            xcp.programMax(b"\x77\x88")
            suite.check(
                "PROGRAM / PROGRAM_NEXT / PROGRAM_MAX",
                link.flash_read(0x08000000, 8) == b"\x11\x22\x33\x44\x55\x66\x77\x88",
                f"got {link.flash_read(0x08000000, 8).hex()}",
            )
            xcp.programVerify(0, 0, 0)
            suite.check("PROGRAM_VERIFY succeeds", True)
            suite.error(
                "PROGRAM_VERIFY with a wrong value -> ERR_VERIFY",
                xcp.programVerify, 0, 0, 1, code="ERR_VERIFY",
            )
            suite.error(
                "PROGRAM_MAX without payload -> ERR_OUT_OF_RANGE",
                xcp.programMax, b"", code="ERR_OUT_OF_RANGE",
            )

            xcp.setMta(0x08020000)
            suite.error(
                "PROGRAM_CLEAR address hole -> ERR_GENERIC",
                xcp.programClear, 0, 0x1000, code="ERR_GENERIC",
            )
            xcp.programReset()

            # ---------------- G. resume / SET_REQUEST ----------------
            print("-- G. resume / SET_REQUEST")
            xcp.connect()
            for resource in (RES_CAL, RES_DAQ, RES_STIM, RES_PGM):
                h.unlock(xcp, resource)
            xcp.setRequest(0x01, 0)
            status = xcp.getStatus()
            suite.check(
                "SET_REQUEST STORE_CAL sets storeCalRequest",
                status.sessionStatus.storeCalRequest is True,
                f"{status}",
            )
            xcp.setRequest(0x04, 0)
            status = xcp.getStatus()
            suite.check(
                "SET_REQUEST STORE_DAQ_RESUME sets storeDaqRequest",
                status.sessionStatus.storeDaqRequest is True,
                f"{status}",
            )
            xcp.setRequest(0x08, 0)
            status = xcp.getStatus()
            suite.check(
                "SET_REQUEST CLEAR_DAQ_REQ sets clearDaqRequest",
                status.sessionStatus.clearDaqRequest is True,
                f"{status}",
            )
            xcp.setRequest(0xE0, 0)
            suite.check("SET_REQUEST reserved bits -> no-op RES", True)

            # ---------------- H. negative / disconnect paths ----------------
            print("-- H. negative paths")
            suite.error(
                "truncated CONNECT -> ERR_CMD_SYNTAX",
                xcp.transport.request, t.Command.CONNECT, code="ERR_CMD_SYNTAX",
            )
            suite.error(
                "truncated SET_MTA -> ERR_CMD_SYNTAX",
                xcp.transport.request, t.Command.SET_MTA, 0, 0, 0,
                code="ERR_CMD_SYNTAX",
            )
            suite.error(
                "truncated UPLOAD -> ERR_CMD_SYNTAX",
                xcp.transport.request, t.Command.UPLOAD, code="ERR_CMD_SYNTAX",
            )
            suite.error(
                "truncated GET_SEED -> ERR_CMD_SYNTAX",
                xcp.transport.request, t.Command.GET_SEED, 0,
                code="ERR_CMD_SYNTAX",
            )

            user = xcp.userCmd(0xAA, b"\x11\x22")
            suite.check(
                "USER_CMD echoes the payload",
                b"\x11\x22" in user,
                f"{user!r}",
            )
            xcp.disconnect()
            xcp.connect()
            suite.error(
                "CONNECT invalid mode -> ERR_MODE_NOT_VALID",
                xcp.connect, 0x77, code="ERR_MODE_NOT_VALID",
            )
            xcp.connect()
            seed = xcp.getSeed(0, RES_CAL)
            suite.check(
                "GET_SEED pattern for CAL",
                seed.length == 4
                and seed.seed[0] == RES_CAL
                and seed.seed[1] == (RES_CAL ^ 0x5A)
                and seed.seed[2] == (RES_CAL ^ 0xA5)
                and seed.seed[3] == 0x42,
                f"{seed}",
            )
            continuation = xcp.getSeed(1, RES_CAL)
            suite.check(
                "GET_SEED continuation after full seed -> length 0",
                continuation.length == 0,
                f"{continuation}",
            )
            suite.error(
                "UNLOCK wrong key -> ERR_ACCESS_LOCKED",
                xcp.unlock, 4, bytes([0, 0, 0, 0]), code="ERR_ACCESS_LOCKED",
            )
            # A failed unlock closes the session; reopen it before continuing.
            xcp.connect()
            suite.error(
                "GET_SEED continuation without pending seed -> ERR_SEQUENCE",
                xcp.getSeed, 1, RES_CAL, code="ERR_SEQUENCE",
            )
            h.unlock(xcp, RES_CAL)
            xcp.disconnect()
    finally:
        link.stop()
        h.close_buses(master_bus, rx_bus, tx_bus, tap_bus)

    print(f"================ pyxcp functional: {suite.passed} passed, {suite.failed} failed ================")
    return 1 if suite.failed else 0


if __name__ == "__main__":
    sys.exit(main())
