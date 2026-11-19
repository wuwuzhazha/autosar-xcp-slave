#!/usr/bin/env python3
"""Quick end-to-end smoke test: pyxcp master <-> slave over a virtual CAN bus.

For the full command/feature matrix see ``test_pyxcp_functional.py``; for
throughput/latency numbers see ``test_pyxcp_perf.py``.

Run: python Tests/integration/test_pyxcp_master.py
"""

from __future__ import annotations

import sys
import time

import xcp_bridge as h

if h.can is None:
    print("SKIP: install python-can first")
    sys.exit(2)

try:
    from pyxcp.master import Master
except ImportError as exc:
    print(f"SKIP: install pyxcp first ({exc})")
    sys.exit(2)


def main() -> int:
    lib = h.load_bridge(h.build_bridge())
    channel = f"xcp-smoke-{id(object())}"
    master_bus, rx_bus, tx_bus, tap_bus = h.open_buses(channel)
    link = h.SlaveLink(lib, rx_bus, tx_bus)
    link.start()
    h.raw_errors()

    failures = 0

    def check(name: str, condition: bool, detail: str = "") -> None:
        nonlocal failures
        if condition:
            print(f"  PASS  {name}")
        else:
            failures += 1
            print(f"  FAIL  {name}  {detail}")

    try:
        with Master("can", h.make_app(channel), transport_layer_interface=master_bus) as xcp:
            resp = xcp.connect()
            check(
                "CONNECT reply: CAN, CTO/DTO 8",
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
            check("GET_ID + UPLOAD", ident_data == b"AUTOSAR XCP Slave", f"got {ident_data!r}")

            check("UNLOCK CAL", h.unlock(xcp, 0x01))
            check("UNLOCK DAQ", h.unlock(xcp, 0x04))

            addr = link.ram_addr()
            link.ram_write(b"\x01\x02\x03\x04")
            xcp.setMta(addr)
            check("SET_MTA + UPLOAD", xcp.upload(4) == b"\x01\x02\x03\x04")

            xcp.setMta(addr)
            xcp.download(b"\xde\xad\xbe\xef")
            check("SET_MTA + DOWNLOAD", link.ram_read(4) == b"\xde\xad\xbe\xef")
            check("SHORT_UPLOAD", xcp.shortUpload(4, addr) == b"\xde\xad\xbe\xef")

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
            check(
                "DAQ DTO carries the RAM value",
                dto is not None and dto[0] == first.firstPid and dto[1:5] == b"\x44\x33\x22\x11",
                f"dto={dto!r}",
            )
            xcp.startStopDaqList(0, 0)
            xcp.freeDaq()

            slave_id = xcp.transportLayerCmd(0xFF)
            check("TL_CMD GET_SLAVE_ID", slave_id[-3:] == b"XCP", f"got {slave_id!r}")

            xcp.disconnect()
    finally:
        link.stop()
        h.close_buses(master_bus, rx_bus, tx_bus, tap_bus)

    print(f"================ pyxcp smoke: {failures} failed ================")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
