# Minimal XCP slave example
> **中文**: [examples/minimal/README.zh-CN.md](README.zh-CN.md)

One-file bring-up that drives the `can` profile end-to-end through the
internal entry point — no real CAN hardware required.

## Build + run

From the repository root, on Linux/macOS or Windows with MinGW gcc:

```bash
python Tools/xcpgen/xcpgen.py --profile can --out Gen/can

gcc -std=c99 -g -Wall -Wextra -Wno-unused-parameter \
    -Werror=implicit-function-declaration \
    -ITests/stubs -ITests -IXcp -IGen/can \
    -include Tests/test_config.h \
    Xcp/Xcp.c Xcp/Xcp_Cmd.c Xcp/Xcp_Std.c Xcp/Xcp_DaqCfg.c \
    Xcp/Xcp_DaqRun.c Xcp/Xcp_DaqStim.c Xcp/Xcp_SeedKey.c \
    Xcp/Xcp_CalPage.c Xcp/Xcp_FlashProg.c Xcp/Xcp_Resume.c \
    Xcp/XcpOnCan.c Xcp/XcpOnFr.c Xcp/XcpOnEth.c Xcp/XcpOnCdd.c \
    Tests/test_stubs.c Gen/can/Xcp_Cfg.c \
    examples/minimal/main.c -o examples/minimal/xcp_minimal

./examples/minimal/xcp_minimal
```

Expected output — five `PID_RES` frames (one per command):

```
--- XCP slave bring-up (CAN profile) ---
CONNECT            FF 0F 40 08 08 00 01 01
GET_STATUS         FF 00 0F 00 00 00
GET_COMM_MODE_INFO FF 00 03 00 00 00 04 11
GET_PAG_PROCESSOR_INFO FF 02 00
GET_DAQ_CLOCK      FF 00 00 00 .. .. .. ..
DISCONNECT         FF
--- all frames emitted ---
```

(The `0F` in byte 1 of CONNECT/GET_STATUS is `RESOURCE = CAL|DAQ|STIM|PGM`.)

## What this demonstrates

- `Xcp_Init(NULL_PTR)` — picks the generated `xcp_config` (`Gen/can/Xcp_Cfg.c`).
- `Xcp_HandleRxIndication` is the single RX funnel — feed every incoming
  XCP frame (CTO or STIM DTO) through it.
- `Xcp_MainFunction()` services the pending command queue.
- `Xcp_HandleTxConfirmation(E_OK)` simulates the lower layer confirming the
  transmit, which releases the next CTO in `tx.parked` / `dto_queue`.
- `test_tx_pop` is the stub's TX capture — on real hardware this is your
  `CanIf_Transmit` / `SoAd_IfTransmit` etc.

## Try it yourself

- Change `send_cmd` arguments to fire other commands — e.g. `SET_MTA` +
  `UPLOAD` to read RAM, or `GET_SEED`/`UNLOCK` if you enable `XcpSeedKeyEnabled`.
- Switch profile: `python Tools/xcpgen/xcpgen.py --profile ilv --out Gen/ilv`
  and rebuild with `-IGen/ilv` — the same code exercises the interleaved
  command queue.
- Point `send_cmd` at a UDP socket instead of `Xcp_HandleRxIndication` and
  you've got a live loopback slave.
