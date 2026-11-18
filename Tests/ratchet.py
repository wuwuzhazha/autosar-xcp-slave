"""架构棘轮。计数只许下降，下降后必须 --tighten 把新上限写回基线。

检查手写运行时 Xcp/（不含 Gen、Tests）:
  g_xcp_manager          全局管理器引用，按文件封顶
  xcp_preprocessor       测试 XCP_ 宏的 #if/#elif/#ifdef/#ifndef，按文件封顶
  c_includes             #include 一个 .c 文件的位置，只许是基线里已有的那几处
  tx_symbols             tx_last_kind / cto_busy / dto_busy / cto_pending，按文件封顶
  forbidden_includes     协议核心对 CanIf.h / FrIf.h / SoAd.h / Cdd.h 的包含，只许是基线里已有的那几处

另外把每个 Config/profiles/*.toml 重新生成，与 Gen/<profile>/ 逐文件比较
（换行符先折成 LF，避免 Windows 检出造成假差异）。

  python Tests/ratchet.py            检查，越界或基线偏松都失败
  python Tests/ratchet.py --tighten  仅当没有任何上升时，把基线收紧到当前值
"""
import difflib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

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

# 只数特性/配置开关：#if/#elif 测试 XCP_ 宏。
# #ifndef 一律排除（XCP_*_H 文件守卫 + #ifndef XCP_CHECK_* 默认回填），它们不是裁剪。
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
        "g_xcp_manager": by("g_xcp_manager"),
        "xcp_preprocessor": by("xcp_preprocessor"),
        "c_includes": includes,
        "tx_symbols": tx,
        "forbidden_includes": forbidden,
    }


def load_baseline():
    return json.loads(BASELINE.read_text(encoding="utf-8"))


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
    """合计封顶。总合计上升才 fail（列上升文件）；下降则提示收紧。

    per-file 上升本身不报：拆文件或把函数收口进公共单元会让局部文件涨，
    只要模块组总合计降就是改进。"""
    if current["total"] > baseline["total"]:
        rises = file_rises(current["files"], baseline["files"])
        lines = [f"  {name} {current['total']} > 基线 {baseline['total']}"]
        for key, was, now in rises:
            lines.append(f"    {key}: {was} -> {now}")
        errors.append("\n".join(lines))
    elif current["total"] < baseline["total"]:
        loose.append(f"  {name} {baseline['total']} -> {current['total']}，基线偏松")


def check(snapshot):
    base = load_baseline()
    errors = []
    loose = []
    for name in ("g_xcp_manager", "xcp_preprocessor"):
        check_metric(name, snapshot[name], base[name], errors, loose)
    for name in TX_SYMBOLS:
        check_metric(f"tx.{name}", snapshot["tx_symbols"][name], base["tx_symbols"][name], errors, loose)

    now_inc = set(snapshot["c_includes"])
    was_inc = set(base["c_includes"])
    extra = sorted(now_inc - was_inc)
    missing = sorted(was_inc - now_inc)
    if extra:
        errors.append("  新的 .c 包含:\n" + "\n".join(f"    {item}" for item in extra))
    if missing:
        loose.append("  .c 包含减少: " + ", ".join(missing))

    now_forb = set(snapshot["forbidden_includes"])
    was_forb = set(base["forbidden_includes"])
    extra_forb = sorted(now_forb - was_forb)
    missing_forb = sorted(was_forb - now_forb)
    if extra_forb:
        errors.append("  协议核心新增传输层头文件:\n" + "\n".join(f"    {item}" for item in extra_forb))
    if missing_forb:
        loose.append("  传输层头文件包含减少: " + ", ".join(missing_forb))
    return errors, loose


def norm(data):
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def check_gen():
    errors = []
    profiles = sorted(p.stem for p in PROFILES.glob("*.toml"))
    gen_dirs = sorted(p.name for p in GEN_OUT.iterdir() if p.is_dir())
    if profiles != gen_dirs:
        errors.append(f"  profile {profiles} 与 Gen 目录 {gen_dirs} 不一致")
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
                errors.append(f"  生成 {name} 失败: {proc.stderr.strip()[:200]}")
                continue
            fresh = {p.relative_to(out).as_posix(): p for p in out.rglob("*") if p.is_file()}
            committed = GEN_OUT / name
            have = {p.relative_to(committed).as_posix(): p for p in committed.rglob("*") if p.is_file()}
            for rel_name in sorted(set(fresh) | set(have)):
                if rel_name not in have:
                    errors.append(f"  Gen/{name}/{rel_name} 生成有、仓库无")
                    continue
                if rel_name not in fresh:
                    errors.append(f"  Gen/{name}/{rel_name} 仓库有、生成无")
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
                    errors.append(f"  Gen/{name}/{rel_name} 与重新生成不一致\n" + "\n".join(diff.splitlines()[:24]))
    return errors


def report(snapshot):
    rows = [
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
            print("没有基线。先运行 python Tests/ratchet.py --tighten", file=sys.stderr)
            return 1
        BASELINE.write_text(json.dumps(snapshot, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        report(snapshot)
        print(f"基线已写入 {BASELINE.relative_to(ROOT).as_posix()}")
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
            print("ratchet FAIL  计数下降了，基线还停在旧上限。运行 python Tests/ratchet.py --tighten")
            print("\n".join(loose))
            return 1
        BASELINE.write_text(json.dumps(snapshot, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        report(snapshot)
        print("基线已收紧")
        print("\n".join(loose))
        return 0
    if tighten:
        report(snapshot)
        print("基线已与当前一致，未改动")
        return 0
    report(snapshot)
    if not skip_gen:
        n = len(list(PROFILES.glob("*.toml")))
        print(f"  gen  {n} profiles identical")
    print("ratchet ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
