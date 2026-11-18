                      
"""XCP build matrix: for each switch combination, xcpgen generates Xcp_Cfg.{h,c}, then all
module sources are compiled and linked with -Werror.

Variants are expressed as ECUC parameters (--set); Tools/xcpgen/xcpgen.py generates each
variant's config. When a validation rule is violated the generator reports the error
directly (negative case); broken references / undefined symbols surface at the link step.
The product callout file Xcp_Callouts.c shares its name with the test stub and is only
checked with -fsyntax-only, not linked.
Usage: python Tests/build_matrix.py [-j N] [-k substring]    returns 0 when all pass
"""
import argparse
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GENCFG = "Config/xcp_base.toml"
XCPGEN = "Tools/xcpgen/xcpgen.py"

MODULE_SRC = [
    "Xcp/Xcp.c", "Xcp/Xcp_Cmd.c", "Xcp/Xcp_Std.c", "Xcp/Xcp_DaqCfg.c", "Xcp/Xcp_DaqRun.c", "Xcp/Xcp_DaqStim.c", "Xcp/Xcp_SeedKey.c",
    "Xcp/Xcp_CalPage.c", "Xcp/Xcp_FlashProg.c", "Xcp/Xcp_Resume.c",
    "Xcp/XcpOnCan.c", "Xcp/XcpOnFr.c", "Xcp/XcpOnEth.c", "Xcp/XcpOnCdd.c",
]
LINK_ONLY = ["Tests/test_stubs.c", "Tests/test_matrix.c"]
SYNTAX_ONLY = ["Xcp/Xcp_Callouts.c"]

# These variants run the full functional test instead of sanity: test_matrix is replaced by test_main / test_predef
FUNC_TESTS = {
    "can/default":      "Tests/test_matrix_func.c",
    "can/interleaved":  "Tests/test_matrix_func.c",
    "can/daq_static":   "Tests/test_matrix_func.c",
    "can/predef":       "Tests/test_matrix_func.c",
    "can/minimal":      "Tests/test_matrix_func.c",
    "can/daq_off":      "Tests/test_matrix_func.c",
    "can/seedkey_off":  "Tests/test_matrix_func.c",
    "can/calpage_off":  "Tests/test_matrix_func.c",
    "can/pgm_off":      "Tests/test_matrix_func.c",
    "can/resume_off":   "Tests/test_matrix_func.c",
    "fr/default":       "Tests/test_matrix_func.c",
    "eth/default":      "Tests/test_matrix_func.c",
    "cdd/default":      "Tests/test_matrix_func.c",
}

CFLAGS = [
    "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
                                             
    "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
    "-ITests/stubs", "-ITests", "-IXcp", "-include", "Tests/test_config.h",
]

LINKS = {"can": "CAN", "fr": "FLEXRAY", "eth": "ETHERNET", "cdd": "CDD"}

                                        
COMMON = [
    ("default", []),
    ("daq_off", ["XcpDaqEnabled=0", "XcpResumeModeEnabled=0"]),
    ("daq_static", ["XcpDaqConfigType=DAQ_STATIC", "XcpMinDaq=1"]),
    ("seedkey_off", ["XcpSeedKeyEnabled=0"]),
    ("calpage_off", ["XcpCalibrationPageEnabled=0"]),
    ("resume_off", ["XcpResumeModeEnabled=0"]),
    ("pgm_off", ["XcpFlashProgrammingEnabled=0"]),
    ("interleaved", ["XcpInterleavedModeEnabled=1"]),
    ("block_off", ["XcpBlockTransferEnabled=0"]),
    ("suppress_tx_off", ["XcpSuppressTxSupport=0"]),
    ("det_off", ["XcpDevErrorDetect=0"]),
    ("version_api_off", ["XcpVersionInfoApi=0"]),
    ("prescaler_off", ["XcpPrescalerSupported=0"]),
    ("stim_timeout_off", ["XcpStimTimeoutEnabled=0"]),
    ("drop_stats_off", ["XcpEnableDtoDropStats=0"]),
    ("ts_none", ["XcpTimestampType=NO_TIME_STAMP"]),
    ("ts_1byte", ["XcpTimestampType=ONE_BYTE"]),
    ("ts_2byte_fixed", ["XcpTimestampType=TWO_BYTE", "XcpTimestampFixed=1"]),
    ("ts_counter", ["XcpTimestampSourceOs=0"]),
    ("id_rel_byte", ["XcpIdentificationFieldType=RELATIVE_BYTE"]),
    ("id_rel_word", ["XcpIdentificationFieldType=RELATIVE_WORD", "XcpOdtEntriesCount=255"]),
    ("id_rel_word_aligned", ["XcpIdentificationFieldType=RELATIVE_WORD_ALIGNED"]),
    ("one_segment_sector", ["XcpMaxSegment=1", "XcpMaxFlashSector=1"]),
    ("minimal", ["XcpDaqEnabled=0", "XcpResumeModeEnabled=0", "XcpSeedKeyEnabled=0",
                  "XcpCalibrationPageEnabled=0", "XcpFlashProgrammingEnabled=0",
                  "XcpBlockTransferEnabled=0", "XcpSuppressTxSupport=0",
                  "XcpDevErrorDetect=0", "XcpVersionInfoApi=0"]),
    ("predef", ["XcpMinDaq=2"]),
]

PER_LINK = {
    "eth": [
        ("eth_unsegmented", ["XcpEthSegmentedRx=0"]),
        ("eth_cto_dto_255", ["XcpMaxCto=255", "XcpMaxDto=255"]),
    ],
    "fr": [
        ("fr_single_frame", ["XcpFrConcatenatedFrames=0"]),
    ],
}

def variants(name_filter):
    for link in LINKS:
        for name, sets in COMMON + PER_LINK.get(link, []):
            vid = f"{link}/{name}"
            if name_filter and name_filter not in vid:
                continue
            yield vid, [f"LinkType={LINKS[link]}", *sets]

def build(vid, sets, workdir):
    gendir = workdir / vid.replace("/", "_")
    gen = subprocess.run(
        [sys.executable, XCPGEN, "--config", GENCFG, "--out", str(gendir),
         *sum((["--set", s] for s in sets), [])],
        cwd=ROOT, capture_output=True, text=True, errors="replace")
    if gen.returncode != 0:
        return vid, sets, "xcpgen: " + (gen.stderr or gen.stdout).strip()
    exe = gendir / (vid.replace("/", "_") + ".exe")
    cflags = [CFLAGS[0], f"-I{gendir}", *CFLAGS[1:]]
    test_src = FUNC_TESTS.get(vid)
    link = ["Tests/test_stubs.c", test_src] if test_src else LINK_ONLY
    steps = [
        ["gcc", *cflags, "-o", str(exe), *MODULE_SRC, str(gendir / "Xcp_Cfg.c"), *link],
        ["gcc", *cflags, "-fsyntax-only", *SYNTAX_ONLY],
    ]
    for cmd in steps:
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
        if r.returncode != 0:
            return vid, sets, (r.stderr or r.stdout).strip()
    r = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True, errors="replace")
    if r.returncode != 0:
        return vid, sets, f"run rc={r.returncode}: " + (r.stderr or r.stdout).strip()
    return vid, sets, None

def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("-k", "--filter", default="")
    args = ap.parse_args()

    todo = list(variants(args.filter))
    if not todo:
        print(f"no variant matches '{args.filter}'")
        return 2

    with tempfile.TemporaryDirectory(prefix="xcp_matrix_") as tmp, \
            ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda v: build(v[0], v[1], Path(tmp)), todo))

    failed = [r for r in results if r[2]]
    for vid, sets, err in failed:
        print(f"FAIL {vid}: " + " ".join(sets))
        for line in err.splitlines()[:12]:
            print("     " + line)
    print(f"matrix: {len(results) - len(failed)}/{len(results)} variants OK")
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
