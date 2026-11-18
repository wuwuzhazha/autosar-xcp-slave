"""XCP 配置器 Web UI: 本地 HTTP 服务 + 单页界面, 直接读写 Config/profiles/*.toml。

用法:
  python Tools/xcpgen/xcpserve.py            # 启动并打开浏览器 (默认 8765)
  python Tools/xcpgen/xcpserve.py --port 9000 --no-browser

只用标准库 (http.server + json + tkinter 无), 生成/校验复用 xcpgen 的 schema 与 emitter。
保存时只写与基线不同的键, profile 文件保持最小差异。
"""
import argparse
import importlib.util
import json
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

ROOT = Path(__file__).resolve().parents[2]
XCPGEN = ROOT / "Tools" / "xcpgen" / "xcpgen.py"
BASE_NAME = "xcp_base"

GROUPS = [
    ("传输层", lambda n: n.startswith("XcpOn") or n.startswith("XcpEth")
     or n.startswith("XcpFr") or n.startswith("XcpCdd")),
    ("基本 STD", lambda n: n in (
        "XcpMaxCto", "XcpMaxDto", "XcpMaxBs", "XcpMinSt", "XcpIdentificationFieldType",
        "XcpIdText", "XcpMainFunctionPeriod", "XcpDevErrorDetect", "XcpVersionInfoApi",
        "XcpSuppressTxSupport")),
    ("DAQ / STIM", lambda n: ("Daq" in n or "Odt" in n or "EventChannel" in n
                              or "Prescaler" in n or "Stim" in n or "Dto" in n
                              or "Interleaved" in n or "BlockTransfer" in n
                              or "CmdQueue" in n or "TxRetry" in n)),
    ("时间戳", lambda n: n.startswith("XcpTimestamp") or n == "XcpOsCounter"),
    ("特性开关", lambda n: n in (
        "XcpSeedKeyEnabled", "XcpCalibrationPageEnabled", "XcpResumeModeEnabled",
        "XcpFlashProgrammingEnabled", "XcpEnableDtoDropStats")),
    ("内存 / 编程", lambda n: n in (
        "XcpMaxKeyLength", "XcpSeedLength", "XcpKeyLength", "XcpMaxSegment",
        "XcpMaxFlashSector", "XcpMaxPgmBlockSize", "XcpNvmBlockIdResume")),
    ("版本", lambda n: n.startswith("XcpSw")),
    ("集成钩子", lambda n: n.startswith("XcpCheck") or n.startswith("XcpDto")),
]
CONTAINERS = {
    "event_channels": ("XcpEventChannel", "XcpMaxEventChannel", None),
    "static_daq_lists": ("XcpDaqList (预定义)", "XcpMinDaq", None),
    "segments": ("XcpSegment", "XcpMaxSegment", "XcpCalibrationPageEnabled"),
    "flash_sectors": ("XcpFlashSector", "XcpMaxFlashSector", "XcpFlashProgrammingEnabled"),
}
SIMPLE_TABLES = ("pdu", "fr")

def load_gen():
    spec = importlib.util.spec_from_file_location("xcpgen", XCPGEN)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod

X = load_gen()

def group_of(name):
    for title, pred in GROUPS:
        if pred(name):
            return title
    return "其它"

def base_path():
    return X.profiles_dir().parent / f"{BASE_NAME}.toml"

def base_config():
    return X.load_config(base_path())

def base_effective(base=None):
    """基线 + schema 默认值后的生效 general, 供差异比较。"""
    b = base if base is not None else base_config()
    g = b.get("general", {})
    return {name: g.get(name, default) for name, _k, default, _lo, _hi, _m, _c in X.P}

def field_specs(rows, skip=()):
    order, types = [], {}
    for r in rows:
        for k, v in r.items():
            if k in skip:
                continue
            if k not in types:
                order.append(k)
                types[k] = "int" if isinstance(v, int) and not isinstance(v, bool) else "str"
    return [{"name": k, "type": types[k]} for k in order]

def schema_payload():
    base = base_config()
    params = []
    for name, kind, default, lo, hi, macro, cls in X.P:
        params.append({
            "name": name, "kind": kind, "default": default, "lo": lo, "hi": hi,
            "macro": macro, "cls": cls, "group": group_of(name),
            "domain": list(hi) if kind == "enum" else None,
            "inBase": name in base.get("general", {}),
        })
    groups = []
    for title, _p in GROUPS:
        n = sum(1 for p in params if p["group"] == title)
        if n:
            groups.append({"title": title, "count": n})
    containers = {}
    for key, (label, need, dep) in CONTAINERS.items():
        rows = base.get(key, [])
        entry = {"label": label, "need": need, "dep": dep,
                 "fields": field_specs(rows, skip=("entries",))}
        if key == "static_daq_lists":
            ent = []
            for r in rows:
                ent = field_specs(r.get("entries", []))
                if ent:
                    break
            entry["entries"] = ent
        containers[key] = entry
    simple = {}
    for key in SIMPLE_TABLES:
        simple[key] = [{"name": k, "type": "int" if isinstance(v, int) else "str"}
                       for k, v in base.get(key, {}).items()]
    return {
        "params": params, "groups": groups, "containers": containers,
        "simpleTables": simple, "links": {"params": list(X.LINKS),
                                          "names": list(X.LINK_NAMES)},
        "baseName": BASE_NAME,
    }

def profile_list():
    out = []
    d = X.profiles_dir()
    for p in sorted(d.glob("*.toml")):
        doc = ""
        for ln in p.read_text(encoding="utf-8").splitlines():
            s = ln.strip()
            if s.startswith("#"):
                doc = s.lstrip("# ").strip()
            break
        try:
            cfg = X.load_config(p)
            g = cfg.get("general", {})
            link = str(g.get("LinkType", "")).upper() or next(
                (n for k, n in zip(X.LINKS, X.LINK_NAMES) if g.get(k)), "")
        except X.SchemaError:
            link, doc = "ERROR", doc or "解析失败"
        out.append({"name": p.stem, "doc": doc, "link": link})
    return out

def _eq(a, b):
    return json.dumps(a, sort_keys=True) == json.dumps(b, sort_keys=True)

def profile_delta(cfg, base, base_eff):
    """只保留与基线不同的部分, 用于最小化 profile 文件。"""
    delta = {}
    g = {k: v for k, v in cfg.get("general", {}).items()
         if k not in base_eff or not _eq(v, base_eff[k])}
    if g:
        delta["general"] = g
    for key, (_l, _n, _d) in CONTAINERS.items():
        if key in cfg and not _eq(cfg[key], base.get(key, [])):
            delta[key] = cfg[key]
    for key in SIMPLE_TABLES:
        if key in cfg and not _eq(cfg[key], base.get(key, {})):
            delta[key] = cfg[key]
    return delta

def emit_profile_text(cfg, base, doc="", base_eff=None):
    delta = profile_delta(cfg, base, base_eff if base_eff is not None
                          else base_effective(base))
    lines = []
    if doc:
        lines.append(f"# {doc}")
    lines.append('extends = "../xcp_base.toml"')
    if "general" in delta:
        lines += ["", "[general]"]
        lines += [f"{k} = {X._toml_scalar(v)}" for k, v in delta.pop("general").items()]
    rest = {k: v for k, v in delta.items() if k in CONTAINERS}
    tail = {k: v for k, v in delta.items() if k in SIMPLE_TABLES}
    if rest:
        lines += X.dump_toml(rest).rstrip("\n").splitlines()
    if tail:
        lines += [""] + X.dump_toml(tail).rstrip("\n").splitlines()
    out, prev_blank = [], False
    for ln in lines:
        blank = (ln == "")
        if blank and prev_blank:
            continue
        out.append(ln)
        prev_blank = blank
    return "\n".join(out).strip("\n") + "\n"

def diff_rows(cfg, base, base_eff=None):
    eff = base_eff if base_eff is not None else base_effective(base)
    ga = {**eff, **{k: v for k, v in base.get("general", {}).items() if k != "LinkType"}}
    gb = {**eff, **{k: v for k, v in cfg.get("general", {}).items() if k != "LinkType"}}
    a = X.flatten({**base, "general": ga})
    b = X.flatten({**cfg, "general": gb})
    out = []
    for k in sorted(set(a) | set(b)):
        va, vb = a.get(k, "<unset>"), b.get(k, "<unset>")
        if va != vb:
            out.append({"key": k, "base": va, "cur": vb})
    return out

def equivalent(cfg, other, base_eff):
    """cfg 与 other 在 schema 语义上等价 (忽略未显式列出的默认值)。"""
    return not diff_rows(cfg, other, base_eff)

def coerce_config(raw):
    """前端传来的是字符串/布尔混合; 交给 xcpgen 做权威校验。"""
    cfg = {"general": dict(raw.get("general", {}))}
    for key, (_l, _n, _d) in CONTAINERS.items():
        rows = raw.get(key)
        if rows is None:
            continue
        clean = []
        for r in rows:
            row = {}
            for k, v in r.items():
                if v == "" or v is None:
                    continue
                row[k] = v
            if key == "static_daq_lists":
                row["entries"] = [{k2: v2 for k2, v2 in e.items()
                                   if v2 != "" and v2 is not None}
                                  for e in r.get("entries", [])]
            clean.append(row)
        cfg[key] = clean
    for key in SIMPLE_TABLES:
        if key in raw:
            cfg[key] = raw[key]
    return cfg

class Handler(BaseHTTPRequestHandler):
    server_version = "xcpui"

    def log_message(self, *_a):
        pass

    def _send(self, code, body, ctype="application/json; charset=utf-8"):
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj, ensure_ascii=False))

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return json.loads(self.rfile.read(n) or b"{}")

    def do_GET(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        try:
            if u.path in ("/", "/index.html"):
                return self._send(200, PAGE, "text/html; charset=utf-8")
            if u.path == "/api/bootstrap":
                prof = q.get("profile") or "can"
                b = base_config()
                return self._json({
                    "schema": schema_payload(),
                    "profiles": profile_list(),
                    "profile": prof,
                    "config": X.load_config(X.resolve_profile(prof)),
                    "base": b,
                    "baseEff": base_effective(b),
                })
            if u.path == "/api/config":
                prof = q.get("profile") or "can"
                return self._json({"config": X.load_config(X.resolve_profile(prof))})
            if u.path == "/api/show":
                prof = q.get("profile") or "can"
                cfg = X.load_config(X.resolve_profile(prof))
                return self._send(200, X.dump_toml(cfg), "text/plain; charset=utf-8")
            if u.path == "/api/diff":
                prof = q.get("profile") or "can"
                cfg = X.load_config(X.resolve_profile(prof))
                return self._json({"rows": diff_rows(cfg, base_config())})
            return self._json({"error": "not found"}, 404)
        except X.SchemaError as e:
            return self._json({"error": str(e)}, 400)
        except Exception as e:
            return self._json({"error": f"{type(e).__name__}: {e}"}, 500)

    def do_POST(self):
        u = urlparse(self.path)
        try:
            body = self._body()
            if u.path == "/api/check":
                cfg = coerce_config(body.get("config", {}))
                X.normalize(cfg, [])
                return self._json({"ok": True, "errors": [],
                                   "diff": len(diff_rows(cfg, base_config()))})
            if u.path == "/api/save":
                prof = body["profile"]
                cfg = coerce_config(body["config"])
                X.normalize(cfg, [])
                path = X.resolve_profile(prof)
                b = base_config()
                eff = base_effective(b)
                text = emit_profile_text(cfg, b, body.get("doc", ""), eff)
                path.write_text(text, encoding="utf-8")
                back = X.load_config(path)
                if not equivalent(back, cfg, eff):
                    left = diff_rows(back, cfg, eff)
                    return self._json({"ok": False, "errors": [
                        "保存后回读不一致: " + ", ".join(r["key"] for r in left[:5])]}, 500)
                return self._json({"ok": True, "path": str(path.relative_to(ROOT)),
                                   "lines": len(text.splitlines()),
                                   "bytes": len(text.encode()),
                                   "diff": len(diff_rows(back, b, eff))})
            if u.path == "/api/generate":
                prof = body["profile"]
                cfg = coerce_config(body["config"])
                vals, tables = X.normalize(cfg, [])
                out = Path(body.get("out") or f"Gen/{prof}")
                out.mkdir(parents=True, exist_ok=True)
                src = f"Config/profiles/{prof}.toml"
                files = {}
                for fn, content in (
                    ("Xcp_Cfg.h", X.emit_h(vals, src)),
                    ("Xcp_Cfg.c", X.emit_c(vals, tables, src)),
                    ("Xcp_MemMap.h", X.emit_memmap()),
                    ("Xcp_IfData.a2l", X.emit_a2l(vals, tables)),
                ):
                    (out / fn).write_text(content)
                    files[fn] = len(content)
                return self._json({"ok": True, "out": out.as_posix(), "files": files})
            if u.path == "/api/newprofile":
                prof = (body.get("name") or "").strip()
                if not prof or not prof.replace("_", "").isalnum():
                    return self._json({"ok": False, "errors": ["名称只能含字母数字下划线"]}, 400)
                p = X.profiles_dir() / f"{prof}.toml"
                if p.exists():
                    return self._json({"ok": False, "errors": ["同名 profile 已存在"]}, 400)
                src = body.get("from") or "can"
                cfg = X.load_config(X.resolve_profile(src))
                p.write_text(emit_profile_text(cfg, base_config(),
                                               body.get("doc") or f"{src} 的副本"),
                             encoding="utf-8")
                return self._json({"ok": True, "profile": prof})
            return self._json({"error": "not found"}, 404)
        except X.SchemaError as e:
            return self._json({"ok": False, "errors": str(e).split("\n")}, 400)
        except Exception as e:
            return self._json({"ok": False, "errors": [f"{type(e).__name__}: {e}"]}, 500)

PAGE = r"""<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8">
<title>XCP 配置器</title>
<style>
:root{
  --bg:#fbfbfc; --pane:#f3f4f6; --card:#fff; --line:#e2e4e8; --line2:#eef0f3;
  --fg:#1b1d21; --muted:#6b7280; --dim:#9aa2ae; --accent:#0a63c9; --accent-bg:#eaf2fd;
  --ok:#0f7b32; --warn:#a86400; --err:#c0261f; --sel:#e8f0fb; --mono:"Cascadia Mono","Consolas",monospace;
}
*{box-sizing:border-box}
html,body{height:100%;margin:0}
body{font:13px/1.45 "Segoe UI Variable Text","Segoe UI",system-ui,-apple-system,sans-serif;
  color:var(--fg);background:var(--bg);display:flex;flex-direction:column;overflow:hidden}
header{display:flex;align-items:center;gap:8px;padding:0 12px;height:48px;
  background:var(--card);border-bottom:1px solid var(--line);flex:none}
.brand{font-weight:600;letter-spacing:.2px;margin-right:4px}
.brand span{color:var(--dim);font-weight:400;margin-left:6px}
select,input,button{font:inherit;color:inherit}
select{appearance:none;background:var(--card);border:1px solid var(--line);border-radius:6px;
  padding:5px 26px 5px 9px;cursor:pointer;
  background-image:linear-gradient(45deg,transparent 50%,var(--muted) 50%),linear-gradient(135deg,var(--muted) 50%,transparent 50%);
  background-position:calc(100% - 14px) 11px,calc(100% - 9px) 11px;
  background-size:5px 5px,5px 5px;background-repeat:no-repeat}
button{border:1px solid var(--line);background:var(--card);border-radius:6px;padding:5px 11px;
  cursor:pointer;white-space:nowrap}
button:hover:not(:disabled){background:var(--line2)}
button:disabled{color:var(--dim);cursor:default}
button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
button.primary:hover:not(:disabled){background:#0b57ac}
.spacer{flex:1}
.chip{font:11px/1 var(--mono);color:var(--muted);background:var(--pane);
  border:1px solid var(--line);border-radius:20px;padding:4px 9px}
.chip.dirty{color:var(--warn);border-color:#f0dcc0;background:#fdf6ec}
main{flex:1;display:flex;min-height:0}
nav{width:236px;flex:none;background:var(--pane);border-right:1px solid var(--line);
  overflow:auto;padding:8px 0}
nav h4{margin:12px 0 4px;padding:0 14px;font:600 11px/1.6 inherit;letter-spacing:.08em;
  text-transform:uppercase;color:var(--dim)}
nav button{display:flex;width:100%;justify-content:space-between;align-items:center;gap:8px;
  border:0;background:none;padding:6px 14px 6px 12px;border-left:3px solid transparent;
  border-radius:0;text-align:left}
nav button:hover{background:#e9ebef}
nav button.on{background:var(--card);border-left-color:var(--accent);font-weight:600}
nav button .n{font:11px/1 var(--mono);color:var(--dim)}
nav button.on .n{color:var(--accent)}
section{flex:1;display:flex;flex-direction:column;min-width:0;min-height:0}
.bar{display:flex;align-items:center;gap:8px;padding:8px 14px;border-bottom:1px solid var(--line);
  background:var(--card);flex:none}
.bar h2{margin:0;font-size:14px;font-weight:600}
.bar .hint{color:var(--muted);font-size:12px}
.searchbox{position:relative;margin-left:auto}
.searchbox input{padding:5px 9px 5px 27px;width:200px;border:1px solid var(--line);
  border-radius:6px;background:var(--card)}
.searchbox::before{content:"";position:absolute;left:9px;top:7px;width:9px;height:9px;
  border:1.5px solid var(--dim);border-radius:50%}
.content{flex:1;overflow:auto;min-height:0}
.row{display:flex;align-items:center;gap:12px;padding:7px 14px;border-bottom:1px solid var(--line2)}
.row:hover{background:#f7f9fc}
.row .label{flex:1;min-width:0}
.row .name{font-family:var(--mono);font-size:12.5px}
.row .desc{color:var(--dim);font-size:11px;font-family:var(--mono);margin-top:1px}
.row .ctl{flex:none;display:flex;align-items:center;gap:8px}
.row.changed{background:var(--accent-bg)}
.row.changed .name::after{content:" •";color:var(--accent)}
input.num,input.txt{width:190px;padding:4px 8px;border:1px solid var(--line);border-radius:5px;
  background:var(--card);font-family:var(--mono);font-size:12.5px}
input.num{width:104px;text-align:right}
input.invalid{border-color:var(--err);background:#fdf1f0}
select.enum{min-width:170px}
.sw{appearance:none;width:36px;height:20px;border-radius:11px;background:#cfd4db;position:relative;
  cursor:pointer;transition:background .12s;flex:none}
.sw:checked{background:var(--accent)}
.sw::after{content:"";position:absolute;top:2px;left:2px;width:16px;height:16px;border-radius:50%;
  background:#fff;transition:transform .12s}
.sw:checked::after{transform:translateX(16px)}
table{border-collapse:collapse;width:100%;font-size:12.5px}
th{position:sticky;top:0;background:var(--pane);text-align:left;font:600 11px/1.7 inherit;
  letter-spacing:.04em;text-transform:uppercase;color:var(--muted);padding:0 8px;
  border-bottom:1px solid var(--line);white-space:nowrap;z-index:1}
td{padding:0;border-bottom:1px solid var(--line2);vertical-align:middle}
tbody tr:hover{background:#f7f9fc}
td input{width:100%;border:0;background:transparent;padding:6px 8px;font-family:var(--mono);
  font-size:12.5px}
td input:focus{outline:2px solid var(--accent);outline-offset:-2px;background:var(--card)}
td.num input{text-align:right}
td.act{width:34px;text-align:center}
td.act button{border:0;background:none;color:var(--dim);padding:4px 6px;font-size:13px}
td.act button:hover{color:var(--err);background:none}
tr.sel td{background:var(--sel)}
.toolbar{display:flex;align-items:center;gap:8px;padding:7px 14px;background:var(--card);
  border-bottom:1px solid var(--line);flex:none}
.badge{font:11px/1 var(--mono);padding:4px 8px;border-radius:20px;background:var(--pane);
  border:1px solid var(--line);color:var(--muted)}
.badge.ok{color:var(--ok);border-color:#cfe7d6;background:#f1f9f3}
.badge.bad{color:var(--warn);border-color:#f0dcc0;background:#fdf6ec}
.sub{padding:6px 14px;font:600 11px/1.8 inherit;letter-spacing:.06em;text-transform:uppercase;
  color:var(--dim);background:var(--pane);border-bottom:1px solid var(--line)}
footer{height:150px;flex:none;border-top:1px solid var(--line);background:var(--card);
  display:flex;flex-direction:column}
.tabs{display:flex;gap:2px;padding:6px 10px 0;border-bottom:1px solid var(--line)}
.tabs button{border:0;background:none;padding:5px 10px;border-radius:5px 5px 0 0;color:var(--muted);
  font-size:12px}
.tabs button.on{background:var(--pane);color:var(--fg);font-weight:600}
.log{flex:1;overflow:auto;padding:8px 12px;font:12px/1.6 var(--mono)}
.log div{white-space:pre-wrap}
.log .t{color:var(--dim)}
.log .ok{color:var(--ok)}
.log .warn{color:var(--warn)}
.log .err{color:var(--err)}
.log .dim{color:var(--muted)}
.difftbl td{padding:4px 8px;font-family:var(--mono);font-size:12px}
.difftbl td.k{color:var(--muted)}
.difftbl td.v{color:var(--fg)}
.difftbl td.v.new{color:var(--accent);font-weight:600}
.empty{padding:30px 14px;color:var(--dim);text-align:center}
.difftbl tr{cursor:pointer}
.difftbl tr:hover td{background:var(--accent-bg)}
@keyframes fl{0%{background:#fff3cd}100%{background:transparent}}
.row.flash{animation:fl 1.5s ease-out}
</style></head><body>
<header>
  <div class="brand">XCP<span>配置器</span></div>
  <select id="profile" title="选择 profile"></select>
  <button id="newp" title="以当前 profile 为模板新建">新建…</button>
  <span class="spacer"></span>
  <span class="chip" id="stat">—</span>
  <button id="check" title="校验 (Ctrl+Enter)">校验</button>
  <button id="save" title="保存到 profile (Ctrl+S)">保存</button>
  <button id="gen" class="primary" title="生成 Xcp_Cfg.h/.c (Ctrl+G)">生成</button>
</header>
<main>
  <nav id="nav"></nav>
  <section>
    <div class="bar"><h2 id="title">—</h2><span class="hint" id="hint"></span>
      <div class="searchbox" id="swrap" hidden><input id="search" placeholder="过滤参数…"></div>
    </div>
    <div class="content" id="body"></div>
  </section>
</main>
<footer>
  <div class="tabs">
    <button data-tab="log" class="on">输出</button>
    <button data-tab="diff">差异 (基线 → 当前)</button>
    <button data-tab="eff">生效配置</button>
  </div>
  <div class="log" id="log"></div>
  <div class="content" id="diff" hidden></div>
  <div class="content" id="eff" hidden><pre class="log" id="efftext" style="margin:0"></pre></div>
</footer>
<script>
const S={schema:null,profiles:[],profile:"can",base:null,cfg:null,base0:null,
        view:"p:传输层",dirty:false,search:"",selList:0};
const $=s=>document.querySelector(s);
const el=(t,c,h)=>{const e=document.createElement(t);if(c)e.className=c;if(h!=null)e.innerHTML=h;return e;};
function log(msg,cls){const d=el("div");d.innerHTML='<span class="t">'
  +new Date().toTimeString().slice(0,8)+'  </span><span class="'+(cls||"dim")+'">'
  +String(msg).replace(/&/g,"&amp;").replace(/</g,"&lt;")+'</span>';
  $("#log").appendChild(d);$("#log").scrollTop=1e9;
  while($("#log").childElementCount>400)$("#log").firstChild.remove();}
function jget(u){return fetch(u).then(r=>r.json());}
function jpost(u,b){return fetch(u,{method:"POST",headers:{"Content-Type":"application/json"},
  body:JSON.stringify(b)}).then(r=>r.json());}
function eq(a,b){return JSON.stringify(a)===JSON.stringify(b);}
function val(name){return S.cfg.general[name];}
function changed(name){return !eq(S.cfg.general[name],S.baseEff[name]);}
function markDirty(){S.dirty=true;renderStat();}
function renderStat(){
  const on=S.schema.links.params.filter(k=>S.cfg.general[k]===true);
  const link=on.length===1?S.schema.links.names[S.schema.links.params.indexOf(on[0])]:(on.length?"MULTI":"—");
  const n=countDiff();
  $("#stat").textContent=(link?link+" · ":"")+(S.dirty?"未保存 · ":"")+(n?n+" 处差异":"基线一致");
  $("#stat").className="chip"+(S.dirty?" dirty":"");}
function countDiff(){
  let n=0;
  for(const p of S.schema.params) if(changed(p.name))n++;
  for(const k in S.schema.containers) if(!eq(S.cfg[k]||[],S.base[k]||[]))n++;
  for(const k in S.schema.simpleTables) if(!eq(S.cfg[k]||{},S.base[k]||{}))n++;
  return n;}
function navItem(key,label,count){
  const b=el("button");b.dataset.key=key;
  b.innerHTML='<span>'+label+'</span><span class="n">'+(count??"")+'</span>';
  if(S.view===key)b.classList.add("on");
  b.onclick=()=>{S.view=key;render();};
  return b;}
function renderNav(){
  const n=$("#nav");n.innerHTML="";
  n.appendChild(el("h4",null,"参数"));
  for(const g of S.schema.groups)n.appendChild(navItem("p:"+g.title,g.title,g.count));
  n.appendChild(el("h4",null,"表容器"));
  for(const k in S.schema.containers)n.appendChild(navItem("t:"+k,S.schema.containers[k].label,
    (S.cfg[k]||[]).length));
  n.appendChild(el("h4",null,"传输/杂项"));
  n.appendChild(navItem("t:pdu","PDU {rx,tx}",""));
  n.appendChild(navItem("t:fr","FlexRay LPDU",""));}
function need(key){
  const c=S.schema.containers[key];if(!c)return null;
  if(c.dep&&S.cfg.general[c.dep]!==true)return null;
  const p=S.schema.params.find(x=>x.name===c.need);if(!p)return null;
  const v=S.cfg.general[c.need];return typeof v==="number"?v:parseInt(v,10);}
function renderCtl(p){
  const wrap=el("div","ctl");
  if(p.kind==="bool"){
    const s=el("input","sw");s.type="checkbox";s.checked=S.cfg.general[p.name]===true;
    s.onchange=()=>{S.cfg.general[p.name]=s.checked;markDirty();render();};
    wrap.appendChild(s);
  }else if(p.kind==="enum"){
    const sel=el("select","enum");
    for(const d of p.domain){const o=el("option",null,d);o.value=d;
      if(S.cfg.general[p.name]===d)o.selected=true;sel.appendChild(o);}
    sel.onchange=()=>{S.cfg.general[p.name]=sel.value;markDirty();render();};
    wrap.appendChild(sel);
  }else if(p.kind==="int"){
    const i=el("input","num");i.type="number";i.value=S.cfg.general[p.name];
    i.min=p.lo!=null?p.lo:"";i.max=p.hi!=null?p.hi:"";
    i.oninput=()=>{
      const v=parseInt(i.value,10);
      const bad=isNaN(v)||(p.lo!=null&&v<p.lo)||(p.hi!=null&&v>p.hi);
      i.classList.toggle("invalid",bad);
      if(!bad){S.cfg.general[p.name]=v;markDirty();renderStat();}
    };
    i.onblur=()=>{render();};
    wrap.appendChild(i);
    wrap.appendChild(el("span","desc",p.lo+"–"+p.hi));
  }else{
    const i=el("input","txt");i.value=S.cfg.general[p.name]??"";
    i.oninput=()=>{S.cfg.general[p.name]=i.value;markDirty();renderStat();};
    wrap.appendChild(i);
  }
  return wrap;}
function renderParams(group){
  const b=$("#body");b.innerHTML="";
  let rows=0;
  for(const p of S.schema.params){
    if(p.group!==group)continue;
    if(S.search&&!(p.name.toLowerCase().includes(S.search)||p.macro.toLowerCase().includes(S.search)))continue;
    rows++;
    const r=el("div","row"+(changed(p.name)?" changed":""));
    r.id="row-"+p.name;
    const lab=el("div","label");
    const nm=el("div","name",p.name);
    nm.title=p.macro+" · "+p.cls+" · "+p.kind;
    lab.appendChild(nm);
    r.appendChild(lab);r.appendChild(renderCtl(p));
    b.appendChild(r);}
  if(!rows)b.appendChild(el("div","empty","没有匹配的参数"));}
function tableHead(fields,rows,extra){
  const t=el("table");const thead=el("thead");const tr=el("tr");
  fields.forEach(f=>{
    let w=Math.max(f.name.length,(f.type==="int"?5:10));
    for(const r of (rows||[]))w=Math.max(w,String(r[f.name]??"").length);
    const th=el("th",null,f.name);
    th.style.minWidth=Math.min(300,Math.max(56,w*7+22))+"px";
    tr.appendChild(th);});
  if(extra)tr.appendChild(el("th",null,""));
  thead.appendChild(tr);t.appendChild(thead);return t;}
function isAddr(f){return f.name.includes("address");}
function fmtInt(f,v){return isAddr(f)&&typeof v==="number"?("0x"+v.toString(16)):v;}
function parseIntLoose(s){
  const t=String(s).trim();
  if(/^0x[0-9a-f]+$/i.test(t))return parseInt(t,16);
  if(/^-?\d+$/.test(t))return parseInt(t,10);
  return NaN;}
function cellInput(f,obj,onchange){
  const td=el("td",f.type==="int"?"num":"");
  const i=el("input");i.value=fmtInt(f,obj[f.name]??"");
  i.placeholder=f.name;
  i.oninput=()=>{
    if(f.type==="int"){
      const v=parseIntLoose(i.value);
      i.classList.toggle("invalid",i.value!==""&&isNaN(v));
      obj[f.name]=isNaN(v)?i.value:v;
    }else obj[f.name]=i.value;
    markDirty();renderStat();};
  if(f.type==="int")i.title="支持十进制或 0x 十六进制";
  td.appendChild(i);return td;}
function renderTable(key){
  if(key==="pdu"||key==="fr")return renderSimple(key);
  const c=S.schema.containers[key];const rows=S.cfg[key]||(S.cfg[key]=[]);
  const b=$("#body");b.innerHTML="";
  const bar=el("div","toolbar");
  const nb=el("span","badge");const needN=need(key);
  nb.textContent=needN==null?("("+c.need+" 未开启)"):(needN===0?rows.length+" 行"
    :(rows.length+"/"+needN+" 行"));
  nb.classList.add(needN==null||needN===0?"":(rows.length===needN?"ok":"bad"));
  bar.appendChild(nb);
  if(c.dep)bar.appendChild(el("span","hint","行数由 "+c.need+" 决定 (依赖 "+c.dep+"); 改表会把整张表写入 profile"));
  else bar.appendChild(el("span","hint","行数由 "+c.need+" 决定; 改表会把整张表写入 profile"));
  bar.appendChild(el("span","spacer"));
  const add=el("button",null,"+ 增加行");
  add.onclick=()=>{const r={};c.fields.forEach(f=>r[f.name]=f.type==="int"?0:"");
    if(key==="static_daq_lists")r.entries=[];
    if(key==="event_channels"){r.event_channel_number=rows.length;}
    if(key==="segments"){r.segment_number=rows.length;}
    if(key==="flash_sectors"){r.sector_number=rows.length;}
    rows.push(r);markDirty();render();};
  bar.appendChild(add);b.appendChild(bar);

  if(!rows.length){b.appendChild(el("div","empty","没有行。点“+ 增加行”。"));return;}
  const t=tableHead(c.fields,rows,true);
  const tb=el("tbody");
  rows.forEach((row,idx)=>{
    const tr=el("tr",key==="static_daq_lists"&&idx===S.selList?"sel":"");
    c.fields.forEach(f=>tr.appendChild(cellInput(f,row,()=>renderStat())));
    const td=el("td","act");const del=el("button",null,"✕");
    del.title="删除该行";
    del.onclick=()=>{rows.splice(idx,1);markDirty();render();};
    td.appendChild(del);tr.appendChild(td);
    if(key==="static_daq_lists"){
      tr.onclick=e=>{if(e.target.tagName==="INPUT")return;S.selList=idx;render();};
    }
    tb.appendChild(tr);});
  t.appendChild(tb);b.appendChild(t);

  if(key!=="static_daq_lists")return;
  const sel=rows[S.selList];
  b.appendChild(el("div","sub","entries — 选中列表 #"+S.selList+(sel?"":"")));
  if(!sel){b.appendChild(el("div","empty","先点一行选中列表"));return;}
  if(!sel.entries)sel.entries=[];
  const want=(parseInt(sel.odt_count,10)||0)*(parseInt(sel.odt_entry_count,10)||0);
  const bar2=el("div","toolbar");
  const nb2=el("span","badge");nb2.textContent=sel.entries.length+"/"+want+" 条";
  nb2.classList.add(want===0?"":(sel.entries.length===want?"ok":"bad"));
  bar2.appendChild(nb2);
  bar2.appendChild(el("span","hint","odt_count × odt_entry_count = "+want));
  bar2.appendChild(el("span","spacer"));
  const addE=el("button",null,"+ 增加条目");
  addE.onclick=()=>{sel.entries.push({address:0,address_extension:0,size:1,
    bit_offset:"XCP_BIT_OFFSET_NONE"});markDirty();render();};
  bar2.appendChild(addE);b.appendChild(bar2);
  const t2=tableHead(c.entries,sel.entries,true);const tb2=el("tbody");
  sel.entries.forEach((e,idx)=>{
    const tr=el("tr");
    c.entries.forEach(f=>tr.appendChild(cellInput(f,e,()=>renderStat())));
    const td=el("td","act");const del=el("button",null,"✕");
    del.onclick=()=>{sel.entries.splice(idx,1);markDirty();render();};
    td.appendChild(del);tr.appendChild(td);tb2.appendChild(tr);});
  t2.appendChild(tb2);b.appendChild(t2);}
function renderSimple(key){
  const fields=S.schema.simpleTables[key];const obj=S.cfg[key]||(S.cfg[key]={});
  const b=$("#body");b.innerHTML="";
  b.appendChild(el("div","sub",key==="pdu"?"CAN 收发 PDU ID":"FlexRay LPDU 参数"));
  fields.forEach(f=>{
    const r=el("div","row"+(eq(obj[f.name],S.base[key][f.name])?"":" changed"));
    const lab=el("div","label");lab.appendChild(el("div","name",f.name));
    r.appendChild(lab);
    const w=el("div","ctl");
    const i=el("input","num");i.type="number";i.value=obj[f.name]??0;
    i.oninput=()=>{const v=parseInt(i.value,10);obj[f.name]=isNaN(v)?i.value:v;
      markDirty();renderStat();};
    w.appendChild(i);r.appendChild(w);b.appendChild(r);});}
function render(){
  const [kind,key]=S.view.split(":");
  renderNav();
  $("#swrap").hidden=kind!=="p";
  if(kind==="p"){$("#title").textContent=key;$("#hint").textContent="点击控件修改, 改动标记 •";
    renderParams(key);}
  else if(S.schema.containers[key]){$("#title").textContent=S.schema.containers[key].label;
    $("#hint").textContent="直接编辑单元格";renderTable(key);}
  else{$("#title").textContent=key==="pdu"?"PDU":"FlexRay LPDU";
    $("#hint").textContent="";renderSimple(key);}
  renderStat();renderDiff();}
function jumpTo(key){
  const container=Object.keys(S.schema.containers).find(k=>key.startsWith(k+"["));
  const simple=Object.keys(S.schema.simpleTables).find(k=>key.startsWith(k+"."));
  let name=null;
  if(key.startsWith("general.")){
    name=key.slice(8);
    const p=S.schema.params.find(x=>x.name===name);
    if(p){S.view="p:"+p.group;S.search="";$("#search").value="";render();}
  }else if(container){S.view="t:"+container;render();}
  else if(simple){S.view="t:"+simple;render();}
  else return;
  document.querySelectorAll(".tabs button").forEach(x=>x.classList.toggle("on",x.dataset.tab==="log"));
  $("#log").hidden=false;$("#diff").hidden=true;$("#eff").hidden=true;
  const n=name?document.getElementById("row-"+name):null;
  if(n){n.scrollIntoView({block:"center"});n.classList.add("flash");
    setTimeout(()=>n.classList.remove("flash"),1600);}}
function renderDiff(){
  const box=$("#diff");box.innerHTML="";
  const rows=[];
  for(const p of S.schema.params){
    if(changed(p.name))rows.push({k:"general."+p.name,a:S.baseEff[p.name],b:S.cfg.general[p.name]});
  }
  for(const k in S.schema.containers){
    const a=S.base[k]||[],b=S.cfg[k]||[];
    if(!eq(a,b))rows.push({k:k,a:a.length+" 行",b:b.length+" 行"+(eq(a,b)?"":" (内容不同)")});}
  for(const k in S.schema.simpleTables){
    const a=S.base[k]||{},b=S.cfg[k]||{};
    for(const f in a)if(!eq(a[f],b[f]))rows.push({k:k+"."+f,a:a[f],b:b[f]});}
  if(!rows.length){box.appendChild(el("div","empty","与基线一致"));return;}
  const t=el("table","difftbl");const tb=el("tbody");
  rows.forEach(r=>{const tr=el("tr");
    tr.title="点击跳转";
    tr.appendChild(el("td","k",r.k));
    tr.appendChild(el("td","v",String(r.a)));
    tr.appendChild(el("td","v new",String(r.b)));
    tr.onclick=()=>jumpTo(r.k);
    tb.appendChild(tr);});
  t.appendChild(tb);box.appendChild(t);
  const btn=document.querySelector('.tabs button[data-tab="diff"]');
  btn.textContent="差异 (基线 → 当前) · "+rows.length;}
async function refreshEff(){
  const txt=await (await fetch("/api/show?profile="+encodeURIComponent(S.profile))).text();
  $("#efftext").textContent=txt;}
function toast(msg,cls){
  const b=$("#body");
  let n=b.querySelector(".toast");
  if(n)n.remove();
  n=el("div","empty");n.style.cssText="position:sticky;bottom:0;background:var(--card);border-top:1px solid var(--line);padding:8px;color:"+(cls==="err"?"var(--err)":"var(--fg)");
  n.textContent=msg;b.appendChild(n);
  setTimeout(()=>n.remove(),4000);}
async function doCheck(){
  const r=await jpost("/api/check",{config:S.cfg});
  if(r.ok){log("校验通过 · "+r.diff+" 处与基线不同","ok");toast("校验通过");}
  else{r.errors.forEach(e=>log(e,"err"));toast("校验失败: "+r.errors[0],"err");}}
async function doSave(){
  const r=await jpost("/api/save",{profile:S.profile,config:S.cfg,
    doc:(S.profiles.find(p=>p.name===S.profile)||{}).doc||""});
  if(r.ok){log("已保存 "+r.path+" ("+r.lines+" 行, "+r.bytes+" B, "+r.diff+" 处差异)","ok");
    S.dirty=false;renderStat();toast("已保存 "+r.path);refreshEff();}
  else{r.errors.forEach(e=>log(e,"err"));toast("保存失败","err");}}
async function doGen(){
  const out="Gen/"+S.profile;
  const r=await jpost("/api/generate",{profile:S.profile,config:S.cfg,out});
  if(r.ok){const names=Object.keys(r.files);
    log("生成 "+r.out+"/ → "+names.join(", "),"ok");toast("已生成到 "+r.out);}
  else{r.errors.forEach(e=>log(e,"err"));toast("生成失败","err");}}
async function doNew(){
  const name=prompt("新 profile 名称 (字母数字下划线):","mycfg");
  if(!name)return;
  const r=await jpost("/api/newprofile",{name:name,from:S.profile});
  if(!r.ok){toast(r.errors[0],"err");return;}
  await boot(r.profile);log("已创建 profile "+r.profile,"ok");}
async function boot(profile){
  const data=await jget("/api/bootstrap"+(profile?"?profile="+encodeURIComponent(profile):""));
  S.schema=data.schema;S.profiles=data.profiles;S.profile=data.profile;
  S.base=data.base;S.baseEff=data.baseEff;
  S.cfg=JSON.parse(JSON.stringify(data.config));
  S.cfg.general=S.cfg.general||{};
  for(const p of S.schema.params)
    if(S.cfg.general[p.name]===undefined)S.cfg.general[p.name]=p.default;
  const sel=$("#profile");sel.innerHTML="";
  for(const p of S.profiles){const o=el("option",null,p.name+(p.doc?" — "+p.doc:""));
    o.value=p.name;if(p.name===S.profile)o.selected=true;sel.appendChild(o);}
  if(!S.groupsInit){S.groupsInit=true;S.view="p:"+(S.schema.groups[0]||{title:"传输层"}).title;}
  S.selList=0;S.dirty=false;
  render();refreshEff();
  log("载入 profile "+S.profile+" · "+(S.cfg.event_channels||[]).length+" 事件通道","dim");}
$("#profile").onchange=e=>boot(e.target.value);
$("#newp").onclick=doNew;
$("#check").onclick=doCheck;
$("#save").onclick=doSave;
$("#gen").onclick=doGen;
$("#search").oninput=e=>{S.search=e.target.value.toLowerCase();render();};
document.querySelectorAll(".tabs button").forEach(b=>{
  b.onclick=()=>{
    document.querySelectorAll(".tabs button").forEach(x=>x.classList.toggle("on",x===b));
    const t=b.dataset.tab;
    $("#log").hidden=t!=="log";$("#diff").hidden=t!=="diff";$("#eff").hidden=t!=="eff";
    if(t==="eff")refreshEff();};});
document.addEventListener("keydown",e=>{
  if((e.ctrlKey||e.metaKey)&&e.key==="s"){e.preventDefault();doSave();}
  if((e.ctrlKey||e.metaKey)&&e.key==="Enter"){e.preventDefault();doCheck();}
  if((e.ctrlKey||e.metaKey)&&e.key==="g"){e.preventDefault();doGen();}});
window.onbeforeunload=e=>{if(S.dirty){e.preventDefault();e.returnValue="";}};
boot();
</script></body></html>
"""

def main():
    ap = argparse.ArgumentParser(description="XCP 配置器 Web UI")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()
    srv = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    url = f"http://127.0.0.1:{args.port}/"
    print(f"XCP 配置器: {url}  (Ctrl+C 退出)")
    print(f"profiles: {X.profiles_dir()}")
    if not args.no_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")
    finally:
        srv.server_close()
    return 0

if __name__ == "__main__":
    sys.exit(main())
