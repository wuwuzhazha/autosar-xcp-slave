#!/usr/bin/env python3
"""Cross-platform test runner — same sweep as run_all_tests.bat.

Usage:
    python Tests/run_all_tests.py            # full sweep
    python Tests/run_all_tests.py --quick    # ratchet + can variant only
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TESTS = ROOT / "Tests"
GEN = ROOT / "Tools" / "xcpgen" / "xcpgen.py"

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
    "-ITests/stubs", "-ITests", "-IXcp", "-include", "Tests/test_config.h",
]

PROFILES = ["can", "ilv", "fr", "eth", "rbyte", "rword", "rwal", "cdd", "static", "predef"]

VARIANTS = [
    ("can",    "test_main",   "xcp_test_can"),
    ("ilv",    "test_main",   "xcp_test_ilv"),
    ("fr",     "test_main",   "xcp_test_fr"),
    ("eth",    "test_main",   "xcp_test_eth"),
    ("rbyte",  "test_main",   "xcp_test_rbyte"),
    ("rword",  "test_main",   "xcp_test_rword"),
    ("rwal",   "test_main",   "xcp_test_rwal"),
    ("cdd",    "test_main",   "xcp_test_cdd"),
    ("static", "test_static", "xcp_test_static"),
    ("predef", "test_predef", "xcp_test_predef"),
]

def run(cmd, cwd=None):
    print("$", " ".join(str(c) for c in cmd))
    r = subprocess.run(cmd, cwd=cwd or ROOT, capture_output=False)
    return r.returncode

def banner(msg):
    print("\n" + "=" * 70)
    print(msg)
    print("=" * 70)

def main():
    quick = "--quick" in sys.argv

    banner("[R] Architecture ratchet (counts + Gen drift)")
    if run([sys.executable, "Tests/ratchet.py"]) != 0:
        return 1

    banner("[0] Regen 10 profiles")
    for p in PROFILES:
        if run([sys.executable, str(GEN), "--profile", p, "--out", f"Gen/{p}"]) != 0:
            return 1
    if run([sys.executable, str(GEN), "--config", "Config/xcp_base.toml", "--check"]) != 0:
        return 1

    if quick:
        banner("[quick] can variant only")
        VARIANTS_TO_RUN = [VARIANTS[0]]
    else:
        VARIANTS_TO_RUN = VARIANTS

    for idx, (profile, main_src, exe) in enumerate(VARIANTS_TO_RUN, 1):
        banner(f"[{idx}/{len(VARIANTS_TO_RUN)}] Build+Run: {profile} ({main_src})")
        out = TESTS / exe
        cmd = ["gcc"] + FLAGS + [f"-IGen/{profile}", "-o", str(out)] + CORE + TRANSPORTS \
              + [f"Gen/{profile}/Xcp_Cfg.c", f"Tests/{main_src}.c"]
        if run(cmd) != 0:
            print(f"BUILD FAILED: {profile}")
            return 1
        if run([str(out)]) != 0:
            print(f"RUN FAILED: {profile}")
            return 1

    banner("xcpgen unit tests")
    if run([sys.executable, "Tests/test_xcpgen.py"]) != 0:
        return 1

    if not quick:
        banner("matrix (103 variants)")
        if run([sys.executable, "Tests/build_matrix.py"]) != 0:
            return 1

    banner("ALL TESTS PASSED")
    return 0

if __name__ == "__main__":
    sys.exit(main())
