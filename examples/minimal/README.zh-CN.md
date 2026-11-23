> **English**: [examples/minimal/README.md](README.md)

# 最小化 XCP 从站示例

单文件启动示例，通过内部入口点端到端驱动 `can` profile——无需真实 CAN 硬件。

## 构建与运行

在仓库根目录下执行，适用于 Linux/macOS 或带 MinGW gcc 的 Windows：

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

预期输出——五个 `PID_RES` 帧（每条命令一个）：

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

（CONNECT/GET_STATUS 第 1 字节中的 `0F` 是 `RESOURCE = CAL|DAQ|STIM|PGM`。）

## 本示例演示的内容

- `Xcp_Init(NULL_PTR)`——选用生成的 `xcp_config`（`Gen/can/Xcp_Cfg.c`）。
- `Xcp_HandleRxIndication` 是唯一的 RX 入口——将每个到来的
  XCP 帧（CTO 或 STIM DTO）都送入它。
- `Xcp_MainFunction()` 处理待执行的命令队列。
- `Xcp_HandleTxConfirmation(E_OK)` 模拟下层确认发送完成，
  从而释放响应 FIFO 或 `dto_queue` 中的下一个 CTO。
- `test_tx_pop` 是 stub 的 TX 捕获点——在真实硬件上，这里对应你的
  `CanIf_Transmit` / `SoAd_IfTransmit` 等。

## 自己动手试试

- 修改 `send_cmd` 的参数以触发其他命令——例如用 `SET_MTA` +
  `UPLOAD` 读取 RAM，或在启用 `XcpSeedKeyEnabled` 后使用 `GET_SEED`/`UNLOCK`。
- 切换 profile：`python Tools/xcpgen/xcpgen.py --profile ilv --out Gen/ilv`，
  并用 `-IGen/ilv` 重新构建——同一份代码即可演练交错（interleaved）命令队列。
- 将 `send_cmd` 指向 UDP socket 而不是 `Xcp_HandleRxIndication`，
  你就得到了一个可实际运行的回环从站。
