#!/usr/bin/env python3
"""Issue #28 reproduction: build and run the deterministic DTO-confirmation
simulation for scenarios S1..S13.

Usage:
    python Tests/issue28/run_issue28_sim.py            # build + all scenarios
    python Tests/issue28/run_issue28_sim.py S6 S7      # build + selected scenarios
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
HERE = ROOT / "Tests" / "issue28"
GEN = HERE / "build" / "gen"
EXE = HERE / "build" / "xcp_issue28_sim"

CORE = [
    "Xcp/Xcp.c", "Xcp/Xcp_Cmd.c", "Xcp/Xcp_Std.c",
    "Xcp/Xcp_DaqCfg.c", "Xcp/Xcp_DaqRun.c", "Xcp/Xcp_DaqStim.c",
    "Xcp/Xcp_SeedKey.c", "Xcp/Xcp_CalPage.c", "Xcp/Xcp_FlashProg.c",
    "Xcp/Xcp_Resume.c", "Tests/test_stubs.c",
]
TRANSPORTS = ["Xcp/XcpOnCan.c", "Xcp/XcpOnFr.c", "Xcp/XcpOnEth.c", "Xcp/XcpOnCdd.c"]
FLAGS = [
    "-std=c99", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter",
    "-Werror=implicit-function-declaration",
    "-ITests/stubs", "-Itests", "-IXcp", "-include", "Tests/test_config.h",
    "-DXCP_TEST_EXTERNAL_CANIF_TRANSMIT",
]
SCENARIOS = ["S1", "S2", "S3", "S4", "S5", "S6", "S7", "S8", "S9", "S6B", "S7B", "S10", "S11",
             "S12", "S13"]


def run(cmd):
    print("$", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run(cmd, cwd=ROOT).returncode


def main():
    picked = [a for a in sys.argv[1:] if a.upper() in SCENARIOS] or SCENARIOS
    picked = [a.upper() for a in picked]

    GEN.parent.mkdir(parents=True, exist_ok=True)
    if run([sys.executable, "Tools/xcpgen/xcpgen.py",
            "--config", "Tests/issue28/xcp_issue28.toml", "--out", str(GEN)]) != 0:
        return 1

    cmd = ["gcc"] + FLAGS + [f"-I{GEN}", "-o", str(EXE)] + CORE + TRANSPORTS \
          + [str(GEN / "Xcp_Cfg.c"), "Tests/issue28/test_issue28_sim.c"]
    if run(cmd) != 0:
        print("BUILD FAILED")
        return 1

    for sid in picked:
        if run([str(EXE), sid]) != 0:
            print(f"RUN FAILED: {sid}")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
