                      
"""xcpgen 生成器测试: golden 输出断言 + 每条校验规则负例 + --check 语义。

用法: python Tests/test_xcpgen.py      全过返回 0
"""
import copy
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
XCPGEN = ROOT / "Tools" / "xcpgen" / "xcpgen.py"
BASE = ROOT / "Config" / "xcp_base.toml"

PASS = 0
FAIL = 0


def ok(cond, name, detail=""):
    global PASS, FAIL
    if cond:
        PASS += 1
    else:
        FAIL += 1
        print(f"FAIL {name} {detail}")


def run(*args, cfg=BASE, out=None, sets=()):
    cmd = [sys.executable, str(XCPGEN), "--config", str(cfg)]
    if out is not None:
        cmd += ["--out", str(out)]
    for s in sets:
        cmd += ["--set", s]
    return subprocess.run(cmd + list(args), cwd=ROOT,
                          capture_output=True, text=True, errors="replace")


def defines(path):
    d = {}
    for ln in path.read_text().splitlines():
        if ln.startswith("#define "):
            parts = ln[8:].split(None, 1)
            d[parts[0]] = parts[1] if len(parts) > 1 else ""
    return d


def cfg_file(tmp, mutate):
    """载入 base 配置, 应用 mutate(dict) 后以 JSON 落盘 (机器生成配置走 JSON)。"""
    cfg = load_base()
    mutate(cfg)
    p = tmp / f"cfg_{abs(hash(str(cfg)))}.json"
    p.write_text(json.dumps(cfg, ensure_ascii=False, indent=1), encoding="utf-8")
    return p


def load_base():
    if not hasattr(load_base, "_c"):
        spec = importlib.util.spec_from_file_location("xg", XCPGEN)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        load_base._c = mod.load_config(str(BASE))
    return copy.deepcopy(load_base._c)


def expect_err(name, needle, sets=(), mutate=None, cfg=None, out=None):
    c = cfg or BASE
    if mutate is not None:
        c = cfg_file(out, mutate)
    r = run(cfg=c, out=out / "o" if out else None, sets=sets)
    ok(r.returncode != 0 and needle in r.stderr, name,
       f"rc={r.returncode} err={r.stderr.strip()[:120]}")


def main():
    with tempfile.TemporaryDirectory(prefix="xcpgen_t_") as tdir:
        tmp = Path(tdir)

                                                          
        out = tmp / "can"
        r = run(cfg=BASE, out=out)
        ok(r.returncode == 0, "gen default rc", r.stderr[:200])
        h = defines(out / "Xcp_Cfg.h")
        ok(h.get("XCP_ON_CAN_ENABLED") == "STD_ON", "h on_can")
        ok(h.get("XCP_ON_FLEXRAY_ENABLED") == "STD_OFF", "h on_fr off")
        ok(h.get("XCP_LINK_TYPE") == "XCP_LINK_CAN", "h link")
        ok(h.get("XCP_MAX_CTO") == "(8U)", "h maxcto", h.get("XCP_MAX_CTO"))
        ok(h.get("XCP_MAX_DTO") == "(8U)", "h maxdto")
        ok(h.get("XCP_DAQ_CONFIG_TYPE") == "XCP_DAQ_DYNAMIC", "h daqtype")
        ok(h.get("XCP_TIMESTAMP_TYPE") == "XCP_FOUR_BYTE", "h tstype")
        ok(h.get("XCP_TIMESTAMP_UNIT") == "XCP_TIMESTAMP_UNIT_1MS", "h tsunit")
        ok(h.get("XCP_IDENTIFICATION_FIELD_TYPE") == "XCP_ABSOLUTE", "h idtype")
        ok(h.get("XCP_SW_MAJOR_VERSION") == "(1U)", "h swmajor")
        ok(h.get("XCP_MODULE_ID") is None, "h no module id dup")
                        
        ok(h.get("XCP_DAQ_DYNAMIC") == "(1U)", "h lit daq dyn")
        ok(h.get("XCP_RELATIVE_WORD_ALIGNED") == "(3U)", "h lit rwa")
        ok(h.get("XCP_TIMESTAMP_UNIT_1MS") == "(6U)", "h lit 1ms")
                           
        htxt = (out / "Xcp_Cfg.h").read_text()
        ok("#ifndef XCP_CHECK_READ_ACCESS" in htxt, "h hook ifndef")
        ok("#error" in htxt and "XCP_ON_CDD_ENABLED == STD_ON" in htxt, "h #error guards")
        c = (out / "Xcp_Cfg.c").read_text()
        ok("XCP_START_SEC_CONST_UNSPECIFIED" in c, "c memmap start")
        ok("xcp_event_channels[XCP_MAX_EVENT_CHANNEL]" in c, "c event table")
        ok(".xcp_rx_pdu_id = 0U" in c and ".xcp_tx_pdu_id = 1U" in c, "c pdu ids")
        ok(".xcp_segment_config = xcp_segment_config" in c, "c seg ref")
        ok(".xcp_flash_sector_config = xcp_flash_sector_config" in c, "c sector ref")
        ok("const Xcp_ConfigType xcp_config" in c, "c config obj")
        mm = (out / "Xcp_MemMap.h").read_text()
        ok("#ifdef XCP_START_SEC_CONST_UNSPECIFIED" in mm, "memmap guard")
        a2l = (out / "Xcp_IfData.a2l").read_text()
        ok("XCP_ON_CAN" in a2l and "MAX_CTO 8" in a2l, "a2l can")
        ok("XCP_ON_ETHERNET" not in a2l, "a2l no eth")

                                                          
        out = tmp / "eth"
        r = run(cfg=BASE, out=out, sets=("LinkType=ETHERNET",))
        ok(r.returncode == 0, "gen eth rc", r.stderr[:200])
        h = defines(out / "Xcp_Cfg.h")
        ok(h.get("XCP_ON_ETHERNET_ENABLED") == "STD_ON", "eth on")
        ok(h.get("XCP_ON_CAN_ENABLED") == "STD_OFF", "eth can off")
        ok(h.get("XCP_LINK_TYPE") == "XCP_LINK_ETHERNET", "eth link")
        ok("XCP_ON_ETHERNET" in (out / "Xcp_IfData.a2l").read_text(), "a2l eth")

                                                              
        out = tmp / "sta"
        r = run(cfg=BASE, out=out,
                sets=("XcpDaqConfigType=DAQ_STATIC", "XcpMinDaq=1"))
        ok(r.returncode == 0, "gen static rc", r.stderr[:200])
        ok(defines(out / "Xcp_Cfg.h").get("XCP_DAQ_CONFIG_TYPE") == "XCP_DAQ_STATIC",
           "static daq type")
        ok(".xcp_static_daq_list" in (out / "Xcp_Cfg.c").read_text(), "static list ref")

                                                           
        out = tmp / "trunc"
        r = run(cfg=BASE, out=out, sets=("XcpMaxEventChannel=2",))
        ok(r.returncode == 0 and "截断" in r.stderr, "truncate warn", r.stderr[:120])

                                                              
        out = tmp / "chk"
        r = run("--check", cfg=BASE, out=out)
        ok(r.returncode == 0 and "check OK" in r.stdout, "check ok", r.stdout[:80])
        ok(not (out / "Xcp_Cfg.h").exists(), "check writes nothing")

                                                       
        NEG = [
            ("link bogus", "不在", ("LinkType=PCI",)),
            ("unknown param", "未知 ECUC", ("XcpNoSuchParam=1",)),
            ("cto too big", "越界", ("XcpMaxCto=256",)),
            ("cto not int", "需整数", ("XcpMaxCto=abc",)),
            ("bool bad", "bool 需", ("XcpOnCddEnabled=foo",)),
            ("bad syntax", "需 Name=Value", ("badsyntax",)),
            ("zero links", "恰 1 条链路", ("XcpOnCanEnabled=0",)),
            ("two links", "恰 1 条链路", ("XcpOnEthernetEnabled=1",)),
            ("mindaq>maxdaq", "XcpMinDaq > XcpMaxDaq", ("XcpMinDaq=9",)),
            ("static w/o mindaq", "DAQ_STATIC 需", ("XcpDaqConfigType=DAQ_STATIC",)),
            ("resume w/o daq", "依赖 XcpDaqEnabled", ("XcpDaqEnabled=0",)),
            ("tsfixed w/ none", "TimestampFixed",
             ("XcpTimestampType=NO_TIME_STAMP", "XcpTimestampFixed=1")),
            ("ilv w/o block", "依赖 XcpBlockTransferEnabled",
             ("XcpInterleavedModeEnabled=1", "XcpBlockTransferEnabled=0")),
            ("enum bogus", "不在", ("XcpTimestampType=BOGUS",)),
        ]
        for name, needle, sets in NEG:
            expect_err("neg " + name, needle, sets=sets, out=tmp)

                                                         
        def drop_ec_row(c):
            c["event_channels"] = c["event_channels"][:3]

        def drop_ec_field(c):
            del c["event_channels"][0]["max_daq_list"]

        def bad_seg_pages(c):
            c["segments"][0]["num_pages"] = 0

        def bad_seg_len(c):
            c["segments"][0]["length"] = 0

        def bad_sec_len(c):
            c["flash_sectors"][0]["length"] = 0

        def drop_seg_row(c):
            c["segments"] = c["segments"][:1]

        def drop_sec_rows(c):
            c["flash_sectors"] = c["flash_sectors"][:1]

        def drop_static_rows(c):
            c["static_daq_lists"] = []
            c["general"]["XcpDaqConfigType"] = "DAQ_STATIC"
            c["general"]["XcpMinDaq"] = 1

        CFGNEG = [
            ("ec rows short", "行数(3) < XcpMaxEventChannel", drop_ec_row, ()),
            ("ec missing field", "缺 max_daq_list", drop_ec_field, ()),
            ("seg pages 0", "num_pages", bad_seg_pages, ()),
            ("seg len 0", "地址/长度非法", bad_seg_len, ()),
            ("sec len 0", "length >= 1", bad_sec_len, ()),
            ("seg rows short", "行数(1) < XcpMaxSegment", drop_seg_row, ()),
            ("sec rows short", "行数(1) < XcpMaxFlashSector", drop_sec_rows, ()),
            ("static rows 0", "static_daq_lists 行数(0)", drop_static_rows, ()),
        ]
        for name, needle, mut, sets in CFGNEG:
            expect_err("neg cfg " + name, needle, sets=sets, mutate=mut, out=tmp)

                              
        bad = tmp / "cfg_nodict.json"
        bad.write_text("[1, 2]\n", encoding="utf-8")
        r = run(cfg=bad, out=tmp / "nodict")
        ok(r.returncode != 0 and "missing CONFIG" in r.stderr, "neg no CONFIG",
           r.stderr[:100])

    print(f"xcpgen tests: {PASS} passed, {FAIL} failed")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
