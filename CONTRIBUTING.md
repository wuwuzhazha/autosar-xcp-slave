# Contributing
> **中文**: [CONTRIBUTING.zh-CN.md](CONTRIBUTING.zh-CN.md)

Thanks for your interest. This project is an independent implementation of the
ASAM XCP Part 2 v1.1 slave — contributions are welcome as long as they keep the
codebase small, portable, and honest about what it implements.

> **A note on provenance.** The bulk of this codebase was written by an AI
> agent under self-test iteration (see README "Origin & Disclaimer"). Bug
> reports and fixes from domain experts are especially valuable — if a
> behavior looks suspicious against the spec, open an issue and cite the
> ASAM/AUTOSAR clause rather than just patching symptoms.
>
> If you believe any content infringes your copyright or IP, please open an
> issue. The offending files will be removed promptly, or the repository
> taken private/deleted on request.

## Setup

```bash
git clone <repo>
cd Xcp

# Validate config + regen all profiles
python Tools/xcpgen/xcpgen.py --config Config/xcp_base.toml --check

# Run the full suite (Windows)
Tests\run_all_tests.bat

# Linux/macOS equivalent
python3 Tests/build_matrix.py

# End-to-end master/slave exchange (pyxcp over a python-can virtual bus)
python3 -m pip install -r Tests/integration/requirements.txt
python3 Tests/integration/test_pyxcp_master.py
```

Required tools: `python3` (3.11+ recommended, uses `tomllib`), `gcc` (C99),
`make`/`bash` for local test orchestration. No other dependencies — the host
harness uses stubs under `Tests/stubs/`.

Host tests do not treat raw host pointers as XCP addresses: test objects are
registered in a mapped 32-bit address space (`Tests/test_stubs.c`), so the
suite is independent of PIE, ASLR, and the host pointer width.

## Workflow

1. Pick an issue or open one describing what you're doing.
2. Fork / branch, make the change, add or extend tests.
3. Run `Tests\run_all_tests.bat` (or the Python equivalent) locally — the suite
   must stay green.
4. If your change lowers an architecture counter tracked by `Tests/ratchet.py`
   (e.g. fewer `g_xcp_channel` references, fewer `#if XCP_*` conditionals,
   fewer `.c`-file `#include`s), run `python Tests/ratchet.py --tighten --no-gen`
   and commit the baseline update in the same commit.
5. Open a PR with a description of what you changed and why.

## Conventions

- **Language**: code + comments + commit messages in English. The historical
  Chinese comments in the codebase are being phased out — please keep new
  contributions in English.
- **C**: C99 only. No C11 atomics, no designated-initializer pointer chains that
  GCC 8 can't parse, no dynamic allocation. Match the existing style —
  `snake_case` locals, `Xcp_PascalCase` exported symbols, `XCP_*` macros.
- **Config**: never hand-edit `Gen/<profile>/` — always regenerate via
  `python Tools/xcpgen/xcpgen.py --profile <name> --out Gen/<name>` and commit
  the diff. `Config/xcp_base.toml` + `Config/profiles/*.toml` are the source
  of truth.
- **Tests**: every behavioral change must be covered by an assertion in
  `Tests/test_main.c` (full variant), `Tests/test_matrix_func.c` (feature-gated
  matrix), or `Tests/test_predef.c` (static/predefined DAQ). Test asserts are
  identified by their section letter + index (e.g. `B3`, `E27`, `K12d`) — new
  checks get the next index in their section.
- **Commits**: concise imperative subject in Chinese or English; one logical
  change per commit. Add `Co-Authored-By` lines only if you are co-authoring
  with another human or agent.

## Reporting a bug

Open an issue with:
- XCP variant / profile you built (`Config/profiles/<name>.toml`)
- Host environment (OS, compiler, `gcc --version`)
- Reproduction: the command sequence (e.g. `[CONNECT, SET_MTA, UPLOAD n=4]`)
  and the expected vs actual response frame

For **security-relevant** issues (memory safety, denial of service via crafted
XCP frames, unlock bypass), please do **not** open a public issue — see
[`SECURITY.md`](SECURITY.md) for the private reporting channel.

## PR review checklist

- [ ] New behavior is asserted by at least one `CHECK()` in `Tests/test_main.c`
      or the matrix suite
- [ ] `python Tests/ratchet.py --no-gen` passes (or baseline tightened with
      justification in the commit message)
- [ ] `run_all_tests.bat` / `build_matrix.py` green on the touched variant(s)
- [ ] No `g_xcp_channel` references added outside `Xcp/Xcp.c` (the ratchet
      forbids new ones in module files)
- [ ] No new `#if XCP_*` at top level of `.c` files (push them to leaf
      helpers instead — see `Xcp_MemTranslate` pattern)
- [ ] Comments and identifiers are in English
- [ ] Public API additions are documented in `docs/` and, if they change the
      wire behavior, in `docs/feature-matrix.md`

## License

By contributing you agree that your contributions are licensed under the
project's [MIT License](LICENSE).
