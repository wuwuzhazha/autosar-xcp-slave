## What

(One line — what does this PR do?)

## Why

(Motivation — bug fix / ASAM gap / cleanup / etc.)

## How tested

- [ ] `Tests\run_all_tests.bat` passes on Windows (or `python3 Tests/build_matrix.py` on Linux)
- [ ] `python Tests/ratchet.py` — if counters went down, `--tighten` baseline committed
- [ ] New behavior has at least one `CHECK()` in `Tests/test_main.c` or `Tests/test_matrix_func.c`

## ASAM / SWS references (if applicable)

SWS_Xcp_NNNNN, ASAM Part 2 §X.Y, Table N

## Checklist

- [ ] `Gen/` changes are generated, not hand-edited
- [ ] Public-facing comments/identifiers in English
- [ ] No new `g_xcp_channel` references outside `Xcp/Xcp.c`
- [ ] Public API changes documented in `docs/`
