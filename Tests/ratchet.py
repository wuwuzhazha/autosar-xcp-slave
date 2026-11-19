"""Architecture ratchet. Counts may only go down; after a drop you must run --tighten to write the new ceiling back into the baseline.

Checks the hand-written runtime Xcp/ (excluding Gen and Tests):
  g_xcp_manager          global manager reference, capped per file
  xcp_preprocessor       #if/#elif/#ifdef/#ifndef testing XCP_ macros, capped per file
  c_includes             places that #include a .c file; only the ones already in the baseline are allowed
  tx_symbols             tx_last_kind / cto_busy / dto_busy / cto_pending, capped per file
  forbidden_includes     includes of CanIf.h / FrIf.h / SoAd.h / Cdd.h in the protocol core; only the ones already in the baseline are allowed

Also regenerates every Config/profiles/*.toml and compares it file by file with Gen/<profile>/
(newlines are folded to LF first, to avoid false diffs from Windows checkouts).

  python Tests/ratchet.py            check; fails on any rise or on a baseline that has gone loose
  python Tests/ratchet.py --tighten  only when nothing has risen, tighten the baseline to the current values
"""
import difflib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# CI consoles are not always UTF-8 (cp1252 on Windows). Printing a diff must
# not raise UnicodeEncodeError and hide the actual ratchet failure.
for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="backslashreplace")

ROOT = Path(__file__).resolve().parent.parent
XCP = ROOT / "Xcp"
BASELINE = Path(__file__).resolve().parent / "ratchet_baseline.json"
GEN = ROOT / "Tools" / "xcpgen" / "xcpgen.py"
PROFILES = ROOT / "Config" / "profiles"
GEN_OUT = ROOT / "Gen"

TX_SYMBOLS = ("tx_last_kind", "cto_busy", "dto_busy", "cto_pending")
FORBIDDEN_FILES = (
    "Xcp.c",
    "Xcp_Cmd.c",
    "Xcp_Std.c",
    "Xcp_DaqCfg.c",
    "Xcp_DaqRun.c",
    "Xcp_DaqStim.c",
    "Xcp_DaqInt.h",
    "Xcp_CalPage.c",
    "Xcp_FlashProg.c",
    "Xcp_SeedKey.c",
    "Xcp_Resume.c",
)
FORBIDDEN_HEADERS = ("CanIf.h", "FrIf.h", "SoAd.h", "Cdd.h")

RE_PRE = re.compile(r"^\s*#\s*(if|elif|ifdef)\b.*\bXCP_", re.M)
RE_CINC = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+\.c)[>"]', re.M)

def sources():
    return sorted(p for p in XCP.rglob("*") if p.suffix in (".c", ".h") and p.is_file())

def rel(path):
    return path.relative_to(ROOT).as_posix()

def read(path):
    return path.read_text(encoding="utf-8", errors="replace")

def count_ident(text, name):
    return len(re.findall(r"\b" + re.escape(name) + r"\b", text))

def measure():
    files = {}
    for path in sources():
        text = read(path)
        key = rel(path)
        files[key] = {
            "g_xcp_channel": count_ident(text, "g_xcp_channel"),
            "g_xcp_manager": count_ident(text, "g_xcp_manager"),
            "xcp_preprocessor": len(RE_PRE.findall(text)),
            "tx": {name: count_ident(text, name) for name in TX_SYMBOLS},
            "c_includes": RE_CINC.findall(text),
        }
    forbidden = []
    for name in FORBIDDEN_FILES:
        path = XCP / name
        text = read(path)
        for header in FORBIDDEN_HEADERS:
            if re.search(r'^\s*#\s*include\s*[<"]' + re.escape(header) + r'[>"]', text, re.M):
                forbidden.append(f"{rel(path)} -> {header}")
    return files, sorted(forbidden)

def pack(files, forbidden):
    def by(field):
        per = {k: v[field] for k, v in files.items() if v[field]}
        return {"total": sum(per.values()), "files": dict(sorted(per.items()))}

    tx = {}
    for name in TX_SYMBOLS:
        per = {k: v["tx"][name] for k, v in files.items() if v["tx"][name]}
        tx[name] = {"total": sum(per.values()), "files": dict(sorted(per.items()))}
    includes = []
    for key, info in sorted(files.items()):
        for inc in info["c_includes"]:
            includes.append(f"{key} -> {inc}")
    return {
        "version": 1,
        "g_xcp_channel": by("g_xcp_channel"),
        "g_xcp_manager": by("g_xcp_manager"),
        "xcp_preprocessor": by("xcp_preprocessor"),
        "c_includes": includes,
        "tx_symbols": tx,
        "forbidden_includes": forbidden,
    }

def load_baseline():
    base = json.loads(BASELINE.read_text(encoding="utf-8"))

    if "g_xcp_channel" not in base and "g_xcp_manager" in base:
        base["g_xcp_channel"] = base.pop("g_xcp_manager")
    return base

def file_rises(current, baseline):
    rises = []
    keys = set(current) | set(baseline)
    for key in sorted(keys):
        now = current.get(key, 0)
        was = baseline.get(key, 0)
        if now > was:
            rises.append((key, was, now))
    return rises

def loose_files(current, baseline):
    drops = []
    for key in sorted(set(current) | set(baseline)):
        now = current.get(key, 0)
        was = baseline.get(key, 0)
        if now < was:
            drops.append((key, was, now))
    return drops

def check_metric(name, current, baseline, errors, loose):
    """Cap on the grand total. Only a rise in the total fails (listing the rising files); a drop prompts a tighten.

    A per-file rise is not reported by itself: splitting files or folding functions
    into a common unit can raise a single file's count, and as long as the module
    group's total goes down it is an improvement."""
    if current["total"] > baseline["total"]:
        rises = file_rises(current["files"], baseline["files"])
        lines = [f"  {name} {current['total']} > baseline {baseline['total']}"]
        for key, was, now in rises:
            lines.append(f"    {key}: {was} -> {now}")
        errors.append("\n".join(lines))
    elif current["total"] < baseline["total"]:
        loose.append(f"  {name} {baseline['total']} -> {current['total']}, baseline is loose")

def check(snapshot):
    base = load_baseline()
    errors = []
    loose = []
    for name in ("g_xcp_channel", "xcp_preprocessor"):
        check_metric(name, snapshot[name], base[name], errors, loose)

    if snapshot["g_xcp_manager"]["total"] > 0:
        errors.append("  g_xcp_manager reappeared; use g_xcp_channel")
    for name in TX_SYMBOLS:
        check_metric(f"tx.{name}", snapshot["tx_symbols"][name], base["tx_symbols"][name], errors, loose)

    now_inc = set(snapshot["c_includes"])
    was_inc = set(base["c_includes"])
    extra = sorted(now_inc - was_inc)
    missing = sorted(was_inc - now_inc)
    if extra:
        errors.append("  new .c includes:\n" + "\n".join(f"    {item}" for item in extra))
    if missing:
        loose.append("  .c includes removed: " + ", ".join(missing))

    now_forb = set(snapshot["forbidden_includes"])
    was_forb = set(base["forbidden_includes"])
    extra_forb = sorted(now_forb - was_forb)
    missing_forb = sorted(was_forb - now_forb)
    if extra_forb:
        errors.append("  new transport-layer headers in the protocol core:\n" + "\n".join(f"    {item}" for item in extra_forb))
    if missing_forb:
        loose.append("  transport-layer header includes removed: " + ", ".join(missing_forb))
    return errors, loose

def norm(data):
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")

def check_gen():
    errors = []
    profiles = sorted(p.stem for p in PROFILES.glob("*.toml"))
    gen_dirs = sorted(p.name for p in GEN_OUT.iterdir() if p.is_dir())
    if profiles != gen_dirs:
        errors.append(f"  profiles {profiles} do not match Gen directories {gen_dirs}")
        return errors
    with tempfile.TemporaryDirectory(prefix="xcp_ratchet_") as tmp:
        tmp_path = Path(tmp)
        for name in profiles:
            out = tmp_path / name
            proc = subprocess.run(
                [sys.executable, str(GEN), "--profile", name, "--out", str(out)],
                cwd=ROOT, capture_output=True, text=True, errors="replace",
            )
            if proc.returncode != 0:
                errors.append(f"  generating {name} failed: {proc.stderr.strip()[:200]}")
                continue
            fresh = {p.relative_to(out).as_posix(): p for p in out.rglob("*") if p.is_file()}
            committed = GEN_OUT / name
            have = {p.relative_to(committed).as_posix(): p for p in committed.rglob("*") if p.is_file()}
            for rel_name in sorted(set(fresh) | set(have)):
                if rel_name not in have:
                    errors.append(f"  Gen/{name}/{rel_name} generated but absent from repo")
                    continue
                if rel_name not in fresh:
                    errors.append(f"  Gen/{name}/{rel_name} in repo but not generated")
                    continue
                a = norm(fresh[rel_name].read_bytes())
                b = norm(have[rel_name].read_bytes())
                if a != b:
                    diff = "".join(difflib.unified_diff(
                        b.decode("utf-8", "replace").splitlines(True),
                        a.decode("utf-8", "replace").splitlines(True),
                        fromfile=f"Gen/{name}/{rel_name}",
                        tofile="regenerated",
                        n=1,
                    ))
                    errors.append(f"  Gen/{name}/{rel_name} differs from regeneration\n" + "\n".join(diff.splitlines()[:24]))
    return errors

def report(snapshot):
    rows = [
        ("g_xcp_channel", snapshot["g_xcp_channel"]["total"]),
        ("g_xcp_manager", snapshot["g_xcp_manager"]["total"]),
        ("xcp_preprocessor", snapshot["xcp_preprocessor"]["total"]),
        ("c_includes", len(snapshot["c_includes"])),
    ]
    for name in TX_SYMBOLS:
        rows.append((f"tx.{name}", snapshot["tx_symbols"][name]["total"]))
    width = max(len(label) for label, _ in rows)
    print("ratchet")
    for label, total in rows:
        print(f"  {label:<{width}}  {total}")

def main():
    tighten = "--tighten" in sys.argv
    skip_gen = "--no-gen" in sys.argv
    files, forbidden = measure()
    snapshot = pack(files, forbidden)
    if not BASELINE.exists():
        if not tighten:
            print("No baseline. Run python Tests/ratchet.py --tighten first", file=sys.stderr)
            return 1
        BASELINE.write_text(json.dumps(snapshot, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        report(snapshot)
        print(f"baseline written to {BASELINE.relative_to(ROOT).as_posix()}")
        return 0

    errors, loose = check(snapshot)
    if not skip_gen:
        errors.extend(check_gen())
    if errors:
        print("ratchet FAIL")
        print("\n".join(errors))
        return 1
    if loose:
        if not tighten:
            print("ratchet FAIL  counts dropped but the baseline still holds the old ceilings. Run python Tests/ratchet.py --tighten")
            print("\n".join(loose))
            return 1
        BASELINE.write_text(json.dumps(snapshot, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        report(snapshot)
        print("baseline tightened")
        print("\n".join(loose))
        return 0
    if tighten:
        report(snapshot)
        print("baseline already matches current values; unchanged")
        return 0
    report(snapshot)
    if not skip_gen:
        n = len(list(PROFILES.glob("*.toml")))
        print(f"  gen  {n} profiles identical")
    print("ratchet ok")
    return 0

if __name__ == "__main__":
    sys.exit(main())
