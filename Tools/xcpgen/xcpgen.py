#!/usr/bin/env python3
"""XCP configuration generator: ECUC schema -> Xcp_Cfg.h / Xcp_Cfg.c / Xcp_MemMap.h / Xcp_IfData.a2l

The schema covers the 12 containers / 54 AUTOSAR parameters of AUTOSAR XCP R4.4.0
plus this project's vendor parameters.
Configuration is TOML (or JSON) and supports extends-based inheritance:
  # Config/profiles/ilv.toml
  extends = "../xcp_base.toml"
  [general]
  XcpInterleavedModeEnabled = true

Usage:
  python Tools/xcpgen/xcpgen.py --profile ilv --out Gen/ilv     generate from a profile
  python Tools/xcpgen/xcpgen.py --config X.toml --out Gen/x     generate from a file
  python Tools/xcpgen/xcpgen.py --profile ilv --show            print the effective config
  python Tools/xcpgen/xcpgen.py --profile ilv --diff base       diff against another config
  python Tools/xcpgen/xcpgen.py --list-profiles                 list available profiles
  python Tools/xcpgen/xcpgen.py --config X.toml --check         validate only
  ... --set ECUC=v         override a single item (repeatable, for combinatorial tests)
The special --set key LinkType=CAN|FLEXRAY|ETHERNET|CDD expands to the four XcpOn*Enabled flags.
On validation failure => exit code 1, no files written.
"""
import argparse
import json
import re
import sys
import tomllib
from pathlib import Path

# ---------------------------------------------------------------------------
# ECUC parameter schema table P.
# Each entry: (EcucName, kind, default, lo, hi_or_enum, C_macro, cls)
#   kind: bool | int | enum | str | hex | func | schm | ident
#   cls : PC (pre-compile) | VENDOR (project-specific extension)
# ---------------------------------------------------------------------------

# Parameters whose C macro name cannot be derived as XCP_ + CamelCase.
MACRO_ALIASES = {
    "XcpOdtCount": "XCP_MAX_ODT",
    "XcpOdtEntriesCount": "XCP_MAX_ODT_ENTRIES",
    "XcpMaxEventChannel": "XCP_MAX_EVENT_CHANNEL",
}

# Prefix prepended to enum literals when emitted as C tokens.
ENUM_PREFIX = {
    "XcpDaqConfigType": "XCP_",
    "XcpTimestampType": "XCP_",
    "XcpTimestampUnit": "XCP_",
    "XcpIdentificationFieldType": "XCP_",
}

P = [
    # ---- AUTOSAR standard parameters ----
    ("XcpOnCanEnabled", "bool", True, 0, 1, "XCP_ON_CAN_ENABLED", "PC"),
    ("XcpOnFlexRayEnabled", "bool", False, 0, 1, "XCP_ON_FLEXRAY_ENABLED", "PC"),
    ("XcpOnEthernetEnabled", "bool", False, 0, 1, "XCP_ON_ETHERNET_ENABLED", "PC"),
    ("XcpOnCddEnabled", "bool", False, 0, 1, "XCP_ON_CDD_ENABLED", "PC"),
    ("XcpMaxCto", "int", 8, 8, 255, "XCP_MAX_CTO", "PC"),
    ("XcpMaxDto", "int", 8, 8, 255, "XCP_MAX_DTO", "PC"),
    ("XcpMaxBs", "int", 4, 0, 255, "XCP_MAX_BS", "PC"),
    ("XcpMinSt", "int", 10, 0, 255, "XCP_MIN_ST", "PC"),
    ("XcpDaqConfigType", "enum", "DAQ_DYNAMIC", None, ("DAQ_DYNAMIC", "DAQ_STATIC"),
     "XCP_DAQ_CONFIG_TYPE", "PC"),
    ("XcpMinDaq", "int", 0, 0, 255, "XCP_MIN_DAQ", "PC"),
    ("XcpMaxDaq", "int", 8, 1, 65535, "XCP_MAX_DAQ", "PC"),
    ("XcpOdtCount", "int", 4, 0, 252, "XCP_MAX_ODT", "PC"),
    ("XcpOdtEntriesCount", "int", 7, 0, 255, "XCP_MAX_ODT_ENTRIES", "PC"),
    ("XcpOdtEntrySizeDaq", "int", 4, 0, 255, "XCP_ODT_ENTRY_SIZE_DAQ", "PC"),
    ("XcpOdtEntrySizeStim", "int", 4, 0, 255, "XCP_ODT_ENTRY_SIZE_STIM", "PC"),
    ("XcpMaxEventChannel", "int", 4, 1, 65535, "XCP_MAX_EVENT_CHANNEL", "PC"),
    ("XcpPrescalerSupported", "bool", True, 0, 1, "XCP_PRESCALER_SUPPORTED", "PC"),
    ("XcpSuppressTxSupport", "bool", True, 0, 1, "XCP_SUPPRESS_TX_SUPPORT", "PC"),
    ("XcpTimestampType", "enum", "FOUR_BYTE", None,
     ("NO_TIME_STAMP", "ONE_BYTE", "TWO_BYTE", "FOUR_BYTE"), "XCP_TIMESTAMP_TYPE", "PC"),
    ("XcpTimestampUnit", "enum", "TIMESTAMP_UNIT_1MS", None,
     ("TIMESTAMP_UNIT_1NS", "TIMESTAMP_UNIT_10NS", "TIMESTAMP_UNIT_100NS",
      "TIMESTAMP_UNIT_1US", "TIMESTAMP_UNIT_10US", "TIMESTAMP_UNIT_100US",
      "TIMESTAMP_UNIT_1MS", "TIMESTAMP_UNIT_10MS", "TIMESTAMP_UNIT_100MS",
      "TIMESTAMP_UNIT_1S"), "XCP_TIMESTAMP_UNIT", "PC"),
    ("XcpTimestampTicks", "int", 1, 0, 65535, "XCP_TIMESTAMP_TICKS", "PC"),
    ("XcpTimestampFixed", "bool", False, 0, 1, "XCP_TIMESTAMP_FIXED", "PC"),
    ("XcpTimestampSourceOs", "bool", True, 0, 1, "XCP_TIMESTAMP_SOURCE_OS", "VENDOR"),
    ("XcpOsCounter", "int", 0, 0, 255, "XCP_OS_COUNTER", "VENDOR"),
    ("XcpMainFunctionPeriod", "int", 1, 1, 65535, "XCP_MAIN_FUNCTION_PERIOD_MS", "PC"),
    ("XcpDevErrorDetect", "bool", True, 0, 1, "XCP_DEV_ERROR_DETECT", "PC"),
    ("XcpVersionInfoApi", "bool", True, 0, 1, "XCP_VERSION_INFO_API", "PC"),
    ("XcpIdentificationFieldType", "enum", "ABSOLUTE", None,
     ("ABSOLUTE", "RELATIVE_BYTE", "RELATIVE_WORD", "RELATIVE_WORD_ALIGNED"),
     "XCP_IDENTIFICATION_FIELD_TYPE", "PC"),
    ("XcpIdText", "str", "AUTOSAR XCP Slave", None, None, "XCP_ID_TEXT", "PC"),
    # ---- Vendor parameters (feature switches, performance tuning) ----
    ("XcpDaqEnabled", "bool", True, 0, 1, "XCP_DAQ_ENABLED", "VENDOR"),
    ("XcpSeedKeyEnabled", "bool", True, 0, 1, "XCP_SEED_KEY_ENABLED", "VENDOR"),
    ("XcpCalibrationPageEnabled", "bool", True, 0, 1, "XCP_CALIBRATION_PAGE_ENABLED", "VENDOR"),
    ("XcpResumeModeEnabled", "bool", True, 0, 1, "XCP_RESUME_MODE_ENABLED", "VENDOR"),
    ("XcpFlashProgrammingEnabled", "bool", True, 0, 1, "XCP_FLASH_PROGRAMMING_ENABLED", "VENDOR"),
    ("XcpBlockTransferEnabled", "bool", True, 0, 1, "XCP_BLOCK_TRANSFER_ENABLED", "VENDOR"),
    ("XcpInterleavedModeEnabled", "bool", False, 0, 1, "XCP_INTERLEAVED_MODE_ENABLED", "VENDOR"),
    ("XcpCmdQueueSize", "int", 4, 1, 255, "XCP_CMD_QUEUE_SIZE", "VENDOR"),
    ("XcpDtoQueueDepth", "int", 4, 1, 255, "XCP_DTO_QUEUE_DEPTH", "VENDOR"),
    ("XcpTxRetryMax", "int", 3, 0, 255, "XCP_TX_RETRY_MAX", "VENDOR"),
    ("XcpStimTimeoutEnabled", "bool", True, 0, 1, "XCP_STIM_TIMEOUT_ENABLED", "VENDOR"),
    ("XcpStimTimeoutMs", "int", 100, 1, 65535, "XCP_STIM_TIMEOUT_MS", "VENDOR"),
    ("XcpDtoBusyTimeoutCycles", "int", 200, 1, 65535, "XCP_DTO_BUSY_TIMEOUT_CYCLES", "VENDOR"),
    ("XcpEnableDtoDropStats", "bool", True, 0, 1, "XCP_ENABLE_DTO_DROP_STATS", "VENDOR"),
    ("XcpNvmBlockIdResume", "hex", "10", 0, 65535, "XCP_NVM_BLOCK_ID_RESUME", "VENDOR"),
    ("XcpMaxKeyLength", "int", 8, 1, 255, "XCP_MAX_KEY_LENGTH", "VENDOR"),
    ("XcpSeedLength", "int", 4, 0, 255, "XCP_SEED_LENGTH", "VENDOR"),
    ("XcpKeyLength", "int", 4, 0, 255, "XCP_KEY_LENGTH", "VENDOR"),
    ("XcpMaxSegment", "int", 2, 1, 255, "XCP_MAX_SEGMENT", "VENDOR"),
    ("XcpMaxFlashSector", "int", 4, 1, 255, "XCP_MAX_FLASH_SECTOR", "VENDOR"),
    ("XcpMaxPgmBlockSize", "int", 32, 1, 65535, "XCP_MAX_PGM_BLOCK_SIZE", "VENDOR"),
    ("XcpEthSegmentedRx", "bool", True, 0, 1, "XCP_ETH_SEGMENTED_RX", "VENDOR"),
    ("XcpFrConcatenatedFrames", "bool", True, 0, 1, "XCP_FR_CONCATENATED_FRAMES", "VENDOR"),
    ("XcpCheckReadAccess", "func", "(0U)", None, None, "XCP_CHECK_READ_ACCESS", "VENDOR"),
    ("XcpCheckWriteAccess", "func", "(0U)", None, None, "XCP_CHECK_WRITE_ACCESS", "VENDOR"),
    ("XcpDtoEnterCritical", "schm", "SchM_Enter_Xcp(XCP_EXCLUSIVE_AREA_0)", None, None,
     "XCP_DTO_ENTER_CRITICAL", "VENDOR"),
    ("XcpDtoExitCritical", "schm", "SchM_Exit_Xcp(XCP_EXCLUSIVE_AREA_0)", None, None,
     "XCP_DTO_EXIT_CRITICAL", "VENDOR"),
    ("XcpSwMajorVersion", "int", 1, 0, 255, "XCP_SW_MAJOR_VERSION", "VENDOR"),
    ("XcpSwMinorVersion", "int", 0, 0, 255, "XCP_SW_MINOR_VERSION", "VENDOR"),
    ("XcpSwPatchVersion", "int", 0, 0, 255, "XCP_SW_PATCH_VERSION", "VENDOR"),
    # ---- CDD transport layer function names ----
    ("XcpCddTransmitName", "ident", "Xcp_CddTransmit", None, None,
     "XCP_CDD_TRANSMIT_NAME", "VENDOR"),
    ("XcpCddRxIndicationName", "ident", "Xcp_CddRxIndication", None, None,
     "XCP_CDD_RX_INDICATION_NAME", "VENDOR"),
    ("XcpCddTxConfirmationName", "ident", "Xcp_CddTxConfirmation", None, None,
     "XCP_CDD_TX_CONFIRMATION_NAME", "VENDOR"),
    ("XcpCddTriggerTransmitName", "ident", "Xcp_CddTriggerTransmit", None, None,
     "XCP_CDD_TRIGGER_TRANSMIT_NAME", "VENDOR"),
]
# Top-level config tables validated against row-count parameters.
TABLE_KEYS = ("event_channels", "static_daq_lists", "segments", "flash_sectors")

SCHEMA = {p[0]: p for p in P}

BOOL_TOK = {"1": True, "0": False, "true": True, "false": False,
            "on": True, "off": False, "STD_ON": True, "STD_OFF": False}

LINKS = ("XcpOnCanEnabled", "XcpOnFlexRayEnabled", "XcpOnEthernetEnabled", "XcpOnCddEnabled")
LINK_NAMES = ("CAN", "FLEXRAY", "ETHERNET", "CDD")

class SchemaError(Exception):
    pass

def _merge(dst, src):
    for k, v in src.items():
        if isinstance(v, dict) and isinstance(dst.get(k), dict):
            _merge(dst[k], v)
        else:
            dst[k] = v
    return dst

def _read_cfg_file(path):
    suf = path.suffix.lower()
    if suf == ".toml":
        with open(path, "rb") as f:
            return tomllib.load(f)
    if suf == ".json":
        return json.loads(path.read_text(encoding="utf-8"))
    raise SchemaError(f"{path}: config must be .toml or .json")

def load_config(path, _seen=None):
    path = Path(path).resolve()
    seen = _seen or []
    if path in seen:
        raise SchemaError("extends cycle: " + " -> ".join(str(p) for p in seen + [path]))
    if not path.exists():
        raise SchemaError(f"{path}: config does not exist")
    raw = _read_cfg_file(path)
    if not isinstance(raw, dict):
        raise SchemaError(f"{path}: config root must be a mapping (missing CONFIG)")
    parent = raw.pop("extends", None)
    cfg = {}
    if parent:
        for b in (parent if isinstance(parent, list) else [parent]):
            cfg = _merge(cfg, load_config(path.parent / b, seen + [path]))
    cfg = _merge(cfg, raw)
    if not isinstance(cfg.get("general", {}), dict):
        raise SchemaError(f"{path}: [general] must be a table")
    return cfg

def _toml_scalar(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return hex(v) if abs(v) >= 0x1000 else str(v)
    if isinstance(v, float):
        return str(v)
    return json.dumps(str(v), ensure_ascii=False)

def dump_toml(cfg, prefix=()):
    lines, tables = [], []
    for k, v in cfg.items():
        if isinstance(v, dict) or (isinstance(v, list) and v
                                  and all(isinstance(x, dict) for x in v)):
            tables.append((k, v))
        elif isinstance(v, list):
            lines.append(f"{k} = [" + ", ".join(_toml_scalar(x) for x in v) + "]")
        else:
            lines.append(f"{k} = {_toml_scalar(v)}")
    for k, v in tables:
        path = prefix + (k,)
        lines.append("")
        if isinstance(v, list):
            for row in v:
                lines.append("[[" + ".".join(path) + "]]")
                lines.extend(dump_toml(row, path).splitlines())
        else:
            lines.append("[" + ".".join(path) + "]")
            lines.extend(dump_toml(v, path).splitlines())
    return "\n".join(lines) + "\n"

def flatten(cfg, prefix=""):
    out = {}
    for k, v in cfg.items():
        key = f"{prefix}{k}"
        if isinstance(v, dict):
            out.update(flatten(v, key + "."))
        elif isinstance(v, list) and v and all(isinstance(x, dict) for x in v):
            for i, row in enumerate(v):
                out.update(flatten(row, f"{key}[{i}]."))
        else:
            out[key] = v
    return out

def profiles_dir():
    return Path(__file__).resolve().parents[2] / "Config" / "profiles"

def resolve_profile(name):
    p = profiles_dir() / f"{name}.toml"
    if not p.exists():
        have = ", ".join(sorted(x.stem for x in profiles_dir().glob("*.toml")))
        raise SchemaError(f"unknown profile {name!r}; available: {have or '(none)'}")
    return p

def coerce(name, raw, p):
    kind = p[1]
    if kind == "bool":
        if raw in BOOL_TOK:
            return BOOL_TOK[raw]
        raise SchemaError(f"{name}: bool expects 0/1/true/false/STD_ON/STD_OFF, got {raw!r}")
    if kind == "int":
        try:
            return int(raw, 0)
        except ValueError:
            raise SchemaError(f"{name}: int expects an integer, got {raw!r}")
    return raw  # str/hex/func/schm/ident pass through unchanged

def check_range(name, v, p):
    lo, hi = p[3], p[4]
    if v < lo or (hi is not None and v > hi):
        raise SchemaError(f"{name}: {v} out of range [{lo},{hi}]")

def apply_link(vals, name):
    up = str(name).upper()
    if up not in LINK_NAMES:
        raise SchemaError(f"LinkType: {name} not in {LINK_NAMES}")
    i = LINK_NAMES.index(up)
    for j, lk in enumerate(LINKS):
        vals[lk] = (j == i)

def normalize(cfg, sets):
    vals = {p[0]: p[2] for p in P}
    general = dict(cfg.get("general", {}))
    link = general.pop("LinkType", None)
    vals.update(general)
    if link is not None:
        apply_link(vals, link)

    unknown = sorted(set(vals) - set(SCHEMA))
    if unknown:
        raise SchemaError("unknown ECUC parameters: " + ", ".join(unknown))

    for kv in sets:
        m = re.fullmatch(r"([A-Za-z0-9_]+)=(.*)", kv)
        if not m:
            raise SchemaError(f"--set '{kv}': expects Name=Value")
        k, v = m.groups()
        if k == "LinkType":
            apply_link(vals, v)
            continue
        if k not in SCHEMA:
            raise SchemaError(f"--set '{k}': unknown ECUC parameter")
        vals[k] = coerce(k, v, SCHEMA[k])

    errs = []
    for k, p in SCHEMA.items():
        v = vals[k]
        kind, lo, hi = p[1], p[3], p[4]
        if kind == "bool" and not isinstance(v, bool):
            errs.append(f"{k}: bool expects True/False, got {v!r}")
        elif kind == "int":
            if not isinstance(v, int):
                errs.append(f"{k}: int expects an integer, got {v!r}")
            else:
                check_range(k, v, p)
        elif kind == "enum" and v not in hi:
            errs.append(f"{k}: {v} not in {hi}")
        elif kind == "ident" and not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", str(v)):
            errs.append(f"{k}: expects a C identifier, got {v!r}")
    if errs:
        raise SchemaError("\n".join(errs))

    n_links = sum(1 for k in LINKS if vals[k])
    if n_links != 1:
        errs.append(f"XcpOn*Enabled: exactly 1 link expected, got {n_links}")
    if vals["XcpMinDaq"] > vals["XcpMaxDaq"]:
        errs.append(f"XcpMinDaq > XcpMaxDaq")
    if vals["XcpDaqConfigType"] == "DAQ_STATIC" and vals["XcpMinDaq"] == 0:
        errs.append("DAQ_STATIC requires XcpMinDaq>0")
    if vals["XcpResumeModeEnabled"] and not vals["XcpDaqEnabled"]:
        errs.append("XcpResumeModeEnabled requires XcpDaqEnabled")
    if vals["XcpTimestampType"] == "NO_TIME_STAMP" and vals["XcpTimestampFixed"]:
        errs.append("XcpTimestampFixed requires a non-NO_TIME_STAMP type")
    if vals["XcpInterleavedModeEnabled"] and not vals["XcpBlockTransferEnabled"]:
        errs.append("XcpInterleavedModeEnabled requires XcpBlockTransferEnabled")
    if vals["XcpBlockTransferEnabled"] and vals["XcpMaxBs"] == 0:
        errs.append("XcpBlockTransferEnabled requires XcpMaxBs>0")
    if vals["XcpDaqEnabled"] and vals["XcpDtoQueueDepth"] < vals["XcpOdtCount"]:
        errs.append("XcpDtoQueueDepth must be >= XcpOdtCount")

    t = {k: cfg.get(k, []) for k in TABLE_KEYS}
    t["pdu"] = cfg.get("pdu", {"rx": 0, "tx": 1})
    t["fr"] = cfg.get("fr", {})

    # Row-count checks: table rows must match the declaring parameter.
    counts = {
        "event_channels": (vals["XcpMaxEventChannel"], "XcpMaxEventChannel", True),
        "static_daq_lists": (vals["XcpMinDaq"], "XcpMinDaq",
                             vals["XcpMinDaq"] > 0),
        "segments": (vals["XcpMaxSegment"], "XcpMaxSegment",
                     vals["XcpCalibrationPageEnabled"]),
        "flash_sectors": (vals["XcpMaxFlashSector"], "XcpMaxFlashSector",
                          vals["XcpFlashProgrammingEnabled"]),
    }
    for key, (need, pname, enabled) in counts.items():
        rows = t[key]
        if enabled and len(rows) < need:
            errs.append(f"{key} row count ({len(rows)}) < {pname} ({need})")
        elif enabled and len(rows) > need:
            print(f"warn: {key} {len(rows)} rows truncated to {need} ({pname})", file=sys.stderr)
            t[key] = rows[:need]
        elif not enabled and len(rows) > 0:
            t[key] = rows  # keep extra rows; the feature is off anyway
    for i, e in enumerate(t["event_channels"]):
        for f in ("event_channel_number", "event_channel_priority", "max_daq_list",
                  "time_cycle", "time_unit", "event_channel_type", "consistency"):
            if f not in e:
                errs.append(f"event_channels[{i}] missing {f}")
    for i, s in enumerate(t["segments"]):
        if s.get("num_pages", 0) < 1:
            errs.append(f"segments[{i}].num_pages >= 1")
        if s.get("start_address", 0) < 0 or s.get("length", 0) < 1:
            errs.append(f"segments[{i}] invalid address/length")
    for i, s in enumerate(t["flash_sectors"]):
        if s.get("length", 0) < 1:
            errs.append(f"flash_sectors[{i}].length >= 1")
    for i, s in enumerate(t["static_daq_lists"]):
        oc, ec2 = s.get("odt_count", 0), s.get("odt_entry_count", 0)
        ents = s.get("entries", [])
        if oc < 1 or ec2 < 1:
            errs.append(f"static_daq_lists[{i}]: odt_count/odt_entry_count >= 1")
        if len(ents) not in (0, oc * ec2):
            errs.append(f"static_daq_lists[{i}].entries count ({len(ents)}) != odt_count*odt_entry_count ({oc*ec2})")
        for j, en in enumerate(ents):
            if "address" not in en or "size" not in en:
                errs.append(f"static_daq_lists[{i}].entries[{j}] missing address/size")
    if errs:
        raise SchemaError("\n".join(errs))
    return vals, t

def mname(name):
    if name in MACRO_ALIASES:
        return MACRO_ALIASES[name]
    return "XCP_" + re.sub(r"(?<!^)(?=[A-Z])", "_", name)[4:].upper()

def mval(name, v):
    p = SCHEMA[name]
    kind = p[1]
    if kind == "bool":
        return "STD_ON" if v else "STD_OFF"
    if kind == "enum":
        return ENUM_PREFIX.get(name, "XCP_") + v
    if kind == "str":
        return f'"{v}"'
    if kind == "hex":
        return f"({v}U)" if str(v).lower().endswith("u") else f"({v}U)"
    if kind in ("func", "schm", "ident"):
        return v
    return f"({v}U)"

def emit_h(vals, src="--config"):
    L = ["#ifndef XCP_CFG_H", "#define XCP_CFG_H", "",
         "/* @generated by Tools/xcpgen/xcpgen.py — DO NOT EDIT",
         f" * AUTOSAR XCP R4.4.0 ECUC schema; source: {src} */", ""]
    L += ["/* Link-selection constants (used to derive XCP_LINK_TYPE) */",
          "#define XCP_LINK_CAN      (1U)",
          "#define XCP_LINK_FLEXRAY  (2U)",
          "#define XCP_LINK_ETHERNET (3U)",
          "#define XCP_LINK_CDD      (4U)", "",
          "/* enum literal constants: needed by #if and macro bodies before Xcp_Types.h */",
          "#define XCP_DAQ_STATIC  (0U)",
          "#define XCP_DAQ_DYNAMIC (1U)",
          "",
          "#define XCP_NO_TIME_STAMP (0U)",
          "#define XCP_ONE_BYTE      (1U)",
          "#define XCP_TWO_BYTE      (2U)",
          "#define XCP_FOUR_BYTE     (4U)",
          "",
          "#define XCP_TIMESTAMP_UNIT_1NS   (0U)",
          "#define XCP_TIMESTAMP_UNIT_10NS  (1U)",
          "#define XCP_TIMESTAMP_UNIT_100NS (2U)",
          "#define XCP_TIMESTAMP_UNIT_1US   (3U)",
          "#define XCP_TIMESTAMP_UNIT_10US  (4U)",
          "#define XCP_TIMESTAMP_UNIT_100US (5U)",
          "#define XCP_TIMESTAMP_UNIT_1MS   (6U)",
          "#define XCP_TIMESTAMP_UNIT_10MS  (7U)",
          "#define XCP_TIMESTAMP_UNIT_100MS (8U)",
          "#define XCP_TIMESTAMP_UNIT_1S    (9U)",
          "",
          "#define XCP_ABSOLUTE              (0U)",
          "#define XCP_RELATIVE_BYTE         (1U)",
          "#define XCP_RELATIVE_WORD         (2U)",
          "#define XCP_RELATIVE_WORD_ALIGNED (3U)", ""]
    schm_seen = False
    for name, kind, default, lo, hi, m, cls in P:
        tok = mval(name, vals[name])
        if kind == "schm":
            if not schm_seen:
                L += ["/* SchM critical-section defaults: SchM_Xcp.h comes from the RTE (-D/-include may override) */",
                      "#if !defined(XCP_DTO_ENTER_CRITICAL) || !defined(XCP_DTO_EXIT_CRITICAL)",
                      '#include "SchM_Xcp.h"', "#endif"]
                schm_seen = True
            L += [f"#ifndef {m}", f"#define {m}() {tok}", "#endif"]
        elif kind == "func":
            sig = "(addr, size)" if name.endswith("Access") else "()"
            L += [f"#ifndef {m}", f"#define {m}{sig} {tok}", "#endif"]
            if name.endswith("Access"):
                ex = m + "_EX"
                L += [f"#ifndef {ex}", f"#define {ex}(ext, addr, size) {m}(addr, size)", "#endif"]
        else:
            L.append(f"#define {m} {tok}")
    L += ["", "/* Derived */",
          "#define XCP_DAQ_COUNT (XCP_MAX_DAQ - XCP_MIN_DAQ)",
          "#define XCP_LINK_TYPE XCP_LINK_" + LINK_NAMES[[i for i, k in enumerate(LINKS)
                                                          if vals[k]][0]],
          "", "/* Compile-time hard constraints (the generator validates these first, but manual edits are caught too) */",
          "#if ((XCP_ON_CAN_ENABLED == STD_ON) + (XCP_ON_FLEXRAY_ENABLED == STD_ON) + (XCP_ON_ETHERNET_ENABLED == STD_ON) + (XCP_ON_CDD_ENABLED == STD_ON)) != 1",
          '#error "exactly one XCP transport link must be enabled"',
          "#endif",
          "#if (XCP_MAX_CTO < 8U) || (XCP_MAX_CTO > 255U)",
          '#error "XCP_MAX_CTO must be 8..255"',
          "#endif",
          "#if (XCP_MAX_DTO < 8U) || (XCP_MAX_DTO > 255U)",
          '#error "XCP_MAX_DTO must be 8..255 (uint8 frame length)"',
          "#endif",
          "#if (XCP_DAQ_CONFIG_TYPE == XCP_DAQ_STATIC) && (XCP_MIN_DAQ == 0U)",
          '#error "XCP_DAQ_CONFIG_TYPE=DAQ_STATIC requires XCP_MIN_DAQ > 0"',
          "#endif",
          "#if (XCP_MIN_DAQ > XCP_MAX_DAQ)",
          '#error "XCP_MIN_DAQ must be <= XCP_MAX_DAQ"',
          "#endif",
          "#if (XCP_RESUME_MODE_ENABLED == STD_ON) && (XCP_DAQ_ENABLED == STD_OFF)",
          '#error "XcpResumeModeEnabled requires XcpDaqEnabled"',
          "#endif",
          "#if (XCP_INTERLEAVED_MODE_ENABLED == STD_ON) && (XCP_BLOCK_TRANSFER_ENABLED == STD_OFF)",
          '#error "XcpInterleavedModeEnabled requires XcpBlockTransferEnabled"',
          "#endif",
          "#if (XCP_IDENTIFICATION_FIELD_TYPE == XCP_ABSOLUTE) \\",
          "    && ((XCP_MAX_DAQ * XCP_MAX_ODT) > 255U)",
          '#error "ABSOLUTE abs_pid is u8, XCP_MAX_DAQ*XCP_MAX_ODT must be <= 255"',
          "#endif",
          "", "#endif /* XCP_CFG_H */", ""]
    return "\n".join(L)

def tok(v):
    return v if isinstance(v, str) else f"{v}U"

def emit_c(vals, t, src="--config"):
    L = ['#include "Xcp.h"', '#include "Xcp_Internal.h"', "",
         "/* @generated by Tools/xcpgen/xcpgen.py — DO NOT EDIT",
         f" * source: {src} */", "",
         '#define XCP_START_SEC_CONST_UNSPECIFIED', '#include "Xcp_MemMap.h"', ""]
    ec = t["event_channels"]
    L.append("static const Xcp_EventChannelType xcp_event_channels[XCP_MAX_EVENT_CHANNEL] =")
    L.append("{")
    for e in ec:
        L.append("    { %sU, %sU, %sU, %sU, %s, %s, %s }," % (
            e["event_channel_number"], e["event_channel_priority"], e["max_daq_list"],
            e["time_cycle"], e["time_unit"], e["event_channel_type"], e["consistency"]))
    L += ["};", ""]
    if vals["XcpMinDaq"] > 0:
        for i, s in enumerate(t["static_daq_lists"]):
            ents = s.get("entries", [])
            if ents:
                L.append(f"static const Xcp_OdtEntryType xcp_static_daq_{i}_entries"
                         "[%sU] =" % (s["odt_count"] * s["odt_entry_count"]))
                L.append("{")
                for en in ents:
                    bo = en.get("bit_offset", "XCP_BIT_OFFSET_NONE")
                    botok = bo if isinstance(bo, str) else f"{bo}U"
                    L.append("    { 0x%08XUL, %sU, %sU, %s }," % (
                        en["address"], en.get("address_extension", 0), en["size"], botok))
                L += ["};", ""]
        L += ["const Xcp_StaticDaqListType Xcp_StaticDaqListConfig[XCP_MIN_DAQ] =", "{"]
        for i, s in enumerate(t["static_daq_lists"]):
            ep = f"xcp_static_daq_{i}_entries" if s.get("entries") else "NULL_PTR"
            L.append("    { %sU, %sU, %sU, %sU, %sU, %sU, %sU, %s }," % (
                s["daq_list_number"], s["event_channel_number"], s["direction"],
                s["odt_count"], s["odt_entry_count"],
                s.get("event_fixed", 0), s.get("write_protected", 0), ep))
        L += ["};", ""]
    if vals["XcpCalibrationPageEnabled"]:
        L += ["static const Xcp_SegmentConfigType xcp_segment_config[XCP_MAX_SEGMENT] =", "{"]
        for s in t["segments"]:
            L.append("    { 0x%08XUL, 0x%08XUL, %sU, %sU }," % (
                s["start_address"], s["length"], s["segment_number"], s["num_pages"]))
        L += ["};", ""]
    if vals["XcpFlashProgrammingEnabled"]:
        L += ["static const Xcp_FlashSectorType xcp_flash_sector_config[XCP_MAX_FLASH_SECTOR] =",
              "{"]
        for s in t["flash_sectors"]:
            L.append("    { 0x%08XUL, 0x%08XUL, %sU, %sU, %sU, %sU }," % (
                s["start_address"], s["length"], s["sector_number"],
                s["clear_sequence_number"], s["program_sequence_number"], s["program_method"]))
        L += ["};", ""]
    L += ['#define XCP_STOP_SEC_CONST_UNSPECIFIED', '#include "Xcp_MemMap.h"', "",
          "const Xcp_ConfigType xcp_config =", "{",
          "    .xcp_event_channel = xcp_event_channels,"]
    if vals["XcpMinDaq"] > 0:
        L.append("    .xcp_static_daq_list = Xcp_StaticDaqListConfig,")
    else:
        L.append("    .xcp_static_daq_list = NULL,")
    if vals["XcpCalibrationPageEnabled"]:
        L.append("    .xcp_segment_config = xcp_segment_config,")
    else:
        L.append("    .xcp_segment_config = NULL,")
    if vals["XcpFlashProgrammingEnabled"]:
        L.append("    .xcp_flash_sector_config = xcp_flash_sector_config,")
    else:
        L.append("    .xcp_flash_sector_config = NULL,")
    fr = t["fr"]
    L += ["    .xcp_rx_pdu_id = %sU," % t["pdu"]["rx"],
          "    .xcp_tx_pdu_id = %sU," % t["pdu"]["tx"],
          "    .xcp_fr_lpdu_handle = %sU," % fr.get("lpdu_handle", 0),
          "    .xcp_fr_frame_id = %sU," % fr.get("frame_id", 0),
          "    .xcp_fr_channel = %sU," % fr.get("channel", 0),
          "    .xcp_fr_cycle_rep = %sU," % fr.get("cycle_rep", 0),
          "    .xcp_fr_offset = %sU," % fr.get("offset", 0),
          "    .xcp_fr_config_idx = %sU" % fr.get("config_idx", 0)]
    L += ["};", ""]
    return "\n".join(L)

def emit_memmap():
    return """/* @generated default MemMap — the integrator overrides this with a same-named file providing the project section pragmas */
#ifdef XCP_START_SEC_CONST_UNSPECIFIED
#undef XCP_START_SEC_CONST_UNSPECIFIED
#endif
#ifdef XCP_STOP_SEC_CONST_UNSPECIFIED
#undef XCP_STOP_SEC_CONST_UNSPECIFIED
#endif
"""

def emit_a2l(vals, t):
    L = ["/begin A2ML",
         "  A2ML_VERSION 1 0",
         "/end A2ML", ""]
    if vals["XcpOnCanEnabled"]:
        L += ["/begin IF_DATA XCP_ON_CAN_P1",
              "  /* XCP on CAN v1.1 */",
              "  0x0101",
              "  CAN_ID_MASTER 0x%X" % t["pdu"]["rx"],
              "  CAN_ID_SLAVE 0x%X" % t["pdu"]["tx"],
              "  BAUDRATE 500000"]
    elif vals["XcpOnFlexRayEnabled"]:
        L += ["/begin IF_DATA XCP_ON_FLEXRAY_P1",
              "  0x0101",
              "  PDU_ID 0x%X" % t["pdu"]["rx"],
              "  FRAME_ID %s" % t["fr"].get("frame_id", 0),
              "  CHANNEL %s" % t["fr"].get("channel", 0)]
    elif vals["XcpOnEthernetEnabled"]:
        L += ["/begin IF_DATA XCP_ON_ETHERNET_IPV4",
              "  0x0101",
              "  HOST_NAME \"ECU\"",
              "  PORT 5555"]
    else:
        L += ["/begin IF_DATA XCP_ON_CDD_P1", "  0x0101"]
    L += ["  MAX_CTO %s" % vals["XcpMaxCto"],
          "  MAX_DTO %s" % vals["XcpMaxDto"],
          "  IDENTIFICATION_FIELD_TYPE %s" % vals["XcpIdentificationFieldType"],
          "  TIMESTAMP_TYPE %s" % vals["XcpTimestampType"],
          "  TIMESTAMP_UNIT %s" % vals["XcpTimestampUnit"],
          "  TIMESTAMP_TICKS %s" % vals["XcpTimestampTicks"],
          "  TIMESTAMP_FIXED %s" % (1 if vals["XcpTimestampFixed"] else 0),
          "  DAQ_CONFIG_TYPE %s" % vals["XcpDaqConfigType"],
          "  MIN_DAQ %s" % vals["XcpMinDaq"],
          "  MAX_DAQ %s" % vals["XcpMaxDaq"],
          "  MAX_ODT %s" % vals["XcpOdtCount"],
          "  MAX_ODT_ENTRIES %s" % vals["XcpOdtEntriesCount"],
          "  MAX_EVENT_CHANNEL %s" % vals["XcpMaxEventChannel"],
          "  DAQ_SUPPORTED %s" % (1 if vals["XcpDaqEnabled"] else 0),
          "  PGM_SUPPORTED %s" % (1 if vals["XcpFlashProgrammingEnabled"] else 0),
          "  CAL_PAGE_SUPPORTED %s" % (1 if vals["XcpCalibrationPageEnabled"] else 0),
          "  RESUME_SUPPORTED %s" % (1 if vals["XcpResumeModeEnabled"] else 0),
          "  SEED_AND_KEY %s" % (1 if vals["XcpSeedKeyEnabled"] else 0),
          "  MAIN_FUNCTION_PERIOD %s" % vals["XcpMainFunctionPeriod"],
          "/end IF_DATA", ""]
    return "\n".join(L)

def cmd_list_profiles():
    d = profiles_dir()
    rows = sorted(d.glob("*.toml")) if d.is_dir() else []
    if not rows:
        print(f"no profiles in {d}")
        return 0
    for p in rows:
        doc = ""
        for ln in p.read_text(encoding="utf-8").splitlines():
            s = ln.strip()
            if s.startswith("#"):
                doc = s.lstrip("# ").strip()
                break
            if s:
                break
        try:
            cfg = load_config(p)
            g = cfg.get("general", {})
            link = str(g.get("LinkType", "")).upper()
            if not link:
                link = next((n for k, n in zip(LINKS, LINK_NAMES) if g.get(k)), "?")
        except SchemaError as e:
            print(f"{p.stem:10s} ERROR {e}")
            continue
        print(f"{p.stem:10s} [{link:8s}] {doc}")
    return 0

def cmd_show(cfg):
    print(dump_toml(cfg), end="")
    return 0

def cmd_diff(cfg, other_name):
    other = (resolve_profile(other_name) if (profiles_dir() / f"{other_name}.toml").exists()
             else Path(other_name))
    a, b = flatten(load_config(other)), flatten(cfg)
    keys = sorted(set(a) | set(b))
    diffs = [(k, a.get(k, "<unset>"), b.get(k, "<unset>"))
             for k in keys if a.get(k, "<unset>") != b.get(k, "<unset>")]
    if not diffs:
        print(f"identical: {other} == current")
        return 0
    print(f"{other} -> current  ({len(diffs)} differences)")
    for k, va, vb in diffs:
        print(f"  {k}: {va} -> {vb}")
    return 0

def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--config", metavar="FILE.toml")
    src.add_argument("--profile", metavar="NAME")
    src.add_argument("--list-profiles", action="store_true")
    ap.add_argument("--out", default="Gen")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--show", action="store_true", help="print the effective config and exit")
    ap.add_argument("--diff", metavar="FILE|PROFILE", help="diff item-by-item against another config")
    ap.add_argument("--set", action="append", default=[], metavar="ECUC=v")
    args = ap.parse_args()

    if args.list_profiles:
        return cmd_list_profiles()
    try:
        path = resolve_profile(args.profile) if args.profile else Path(args.config)
        cfg = load_config(path)
        if args.show:
            return cmd_show(cfg)
        if args.diff:
            return cmd_diff(cfg, args.diff)
        vals, tables = normalize(cfg, args.set)
    except SchemaError as e:
        print("CONFIG ERROR:\n" + str(e), file=sys.stderr)
        return 1

    if args.check:
        print("check OK")
        return 0

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    try:
        src = path.relative_to(Path(__file__).resolve().parents[2]).as_posix()
    except ValueError:
        src = path.as_posix()
    # Explicit UTF-8 + LF: the default is the locale encoding, which is cp1252
    # on Windows runners, and the banner contains a non-ASCII character.
    (out / "Xcp_Cfg.h").write_text(emit_h(vals, src), encoding="utf-8", newline="\n")
    (out / "Xcp_Cfg.c").write_text(emit_c(vals, tables, src), encoding="utf-8", newline="\n")
    (out / "Xcp_MemMap.h").write_text(emit_memmap(), encoding="utf-8", newline="\n")
    (out / "Xcp_IfData.a2l").write_text(emit_a2l(vals, tables), encoding="utf-8", newline="\n")
    print(f"generated -> {out}/ (Xcp_Cfg.h/.c, Xcp_MemMap.h, Xcp_IfData.a2l)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
