> **English**: [docs/porting.md](porting.md)

# 移植 / 集成指南

将 `Xcp/` 放入你的 ECU 需要提供以下内容。`Xcp/` 之外的一切均可替换——
核心代码仅通过 **callout** 和 **传输钩子（transport hooks）** 与宿主交互。

## 必需的 callout —— `Xcp/Xcp_Callback.h`

| 钩子 | 用途 |
|---|---|
| `Xcp_GetSeed_Callout(resource, seed, *len)` | 返回 `resource` 的解锁 seed |
| `Xcp_VerifyKey_Callout(resource, key, len)` | 校验解锁 key（返回 `TRUE`/`FALSE`） |
| `Xcp_CopyCalPage_Callout(src_seg, src_page, dst_seg, dst_page)` | 复制一个标定页 |
| `Xcp_BuildChecksum_Callout(addr, size, *csum, *csum_len)` | 对内存块计算校验和 |
| `Xcp_FlashErase_Callout(addr, len)` | 擦除 Flash |
| `Xcp_FlashProgram_Callout(addr, data, len)` | 编程 Flash |
| `Xcp_FlashVerify_Callout(addr, len, expected)` | 校验 |
| `Xcp_EcuReset_Callout()` | 调度 MCU 复位——**在响应发出之前不得返回** |
| `Xcp_GetSeed_Callout` / `Xcp_VerifyKey_Callout` | 参见 Seed & Key 一节 |

`Xcp_Callouts.c` 附带了桩实现——请将其替换为你自己的实现。

## 传输发送

在配置时（`Xcp_LinkType`）选择**其中一种**：

| 链路 | TX 时调用的函数 |
|---|---|
| CAN | `CanIf_Transmit(pdu_id, &pdu)` |
| FlexRay | `FrIf_Transmit(pdu_id, &pdu)` |
| Ethernet | `SoAd_IfTransmit(pdu_id, &pdu)` |
| CDD | 通过 `XcpCddTransmitName` 配置自定义名称——生成的 `Xcp_Cfg.h` 会输出 `XCP_CDD_TRANSMIT_NAME` |

如果你自带底层驱动，改为实现 `Xcp_Tl_SendCto` / `Xcp_Tl_SendDto`，
并将不需要的 `XcpOn*.c` 文件从编译中剔除。

## 内存访问

`Xcp_MemTranslate` / `Xcp_MemPtrRead` / `Xcp_MemPtrWrite` 将 `mta`+`ext` 解析为
物理指针。你的 `XCP_CHECK_{READ,WRITE}_ACCESS[_EX]` 宏决定某个地址范围是否
合法——默认生成的 `Xcp_Cfg.h` 允许所有访问；**量产版本必须对其进行限制**
（RAM 边界、仅标定窗口等）。

XCP 地址是 32 位。`ext == 0` 时核心通过 `XCP_MEM_PTR(addr)`（默认
`(uint8 *)(uintptr_t)addr`）转成指针。如果 64 位宿主的对象位于 4 GiB 以上，
必须在配置头中覆盖 `XCP_MEM_PTR(addr)` 与 `XCP_MEM_ADDR(ptr)`，把 32 位 XCP
地址空间映射到真实指针（基址偏移或查表）。`XCP_MEM_ADDR` 是反方向，GET_ID
用它发布 ID 字符串地址。

`ext` 语义（单一 `Xcp_MemTranslate`）：
- `ext == 0` → `addr` 是裸指针
- `ext >> 4 == s`（s = 1..XCP_MAX_SEGMENT）→ `addr` 是当前激活 XCP 页下
  **段 `s-1` 内的偏移量**

## DAQ 运行时

- `Xcp_MainFunction` 驱动事件通道调度：每个 `event_channel` 的
  `time_cycle` × `time_unit` 节拍会被换算为毫秒，并与
  `XCP_MAIN_FUNCTION_PERIOD_MS` 比较。
- `Xcp_Event(n)` 是手动触发方式——当事件实际发生时（例如“新的凸轮同步”）
  从你的应用调用它，或者依赖周期性调度器。
- `Xcp_DtoSendOrQueue` 是**唯一的** DTO 出口——永远不要自己调用
  `Xcp_Tl_SendDto`；始终经由它，这样队列、忙看门狗和过载事件才能正常工作。

## 标定页段

`Config/xcp_base.toml` 中的 `xcp_segment_config` 行映射 `ext` 地址空间。
`ext >> 4` 选择对应行；`active_page_xcp` 会乘入页偏移量。

## RESUME / NvM

如果启用了 `XcpResumeModeEnabled`：
- `NvM_ReadBlock` / `NvM_WriteBlock` / `NvM_InvalidateNvBlock` 必须存在。
- 块 id 为 `XCP_NVM_BLOCK_ID_RESUME`（默认 10，可配置）。
- 数据布局为 `Xcp_ResumeDataType`（magic、version、checksum、每个 DAQ 的 ODT 条目）。

## 线程 / 临界区

`XCP_DTO_ENTER_CRITICAL` / `XCP_DTO_EXIT_CRITICAL` 保护 DTO 队列和每一次
`tx.state` 迁移（包括 `Xcp_HandleTxConfirmation`）。默认映射到
`SchM_Enter_Xcp/SchM_Exit_Xcp(XCP_EXCLUSIVE_AREA_0)`——可通过
`Xcp_Cfg.h` 覆盖，或将其定义为你自己的锁原语。

`Xcp_Tl_SendCto`/`SendDto` 位于临界区**之外**（这些调用可能较慢）。

并发契约：

- 只有 `Xcp_CanIfRxIndication` / `Xcp_CanIfTxConfirmation`（以及 Fr/Eth/Cdd
  对应回调）可能在中断上下文运行。它们只入队帧并更新 TX 状态，不直接调用
  下层发送函数。
- `Xcp_MainFunction` 处理命令队列、DAQ 事件与块上传，只能在一个任务上下文调用。
- 命令交接是单生产者/单消费者：RX 回调写 `cmd_queue_buffer`/`cmd_pending`，
  主循环读取并清除 `cmd_pending`。命令未处理时生产者置 `busy_pending`
  而不是覆盖，主循环应答 `ERR_CMD_BUSY` 后清除。其他上下文不得写这些字段。
- 主循环尚未处理上一条命令时到达的新命令会得到 `ERR_CMD_BUSY`。块模式主站
  必须按通告的 `MIN_ST`（默认 1 ms）控制帧间隔；启用
  `XcpInterleavedModeEnabled` 时改为排队而不是应答 busy。

## 会话生命周期

- `Xcp_Init(&xcp_config)` ——启动时调用一次（或用于复位）。
- `Xcp_SessionOpen` / `Xcp_SessionClose` ——由 `CONNECT`/`DISCONNECT` 调用；
  当 socket 打开/关闭时你也可以直接调用它们（Ethernet 场景参见
  `Xcp_SoAdConnectionClosed`）。
- `Xcp_HandleRxIndication(pdu)` ——把每个收到的帧送入这里；PID ≥ 0xC0 是
  命令，PID < 0xC0 是 STIM DTO。
- `Xcp_HandleTxConfirmation(result)` ——当下层确认一次 TX 时调用。
- `Xcp_HandleTriggerTransmit(pdu)` ——当下层拉取数据时调用。

## 配置参考

所有配置项位于 `Tools/xcpgen/xcpgen.py` 的 `SCHEMA` 表中（约 54 个 ECUC +
vendor 参数）。规范 profile 见 `Config/xcp_base.toml`，以及展示覆盖语法的
`Config/profiles/*.toml` 覆盖文件。

常用检查：
```bash
python Tools/xcpgen/xcpgen.py --config Config/xcp_base.toml --check     # validate
python Tools/xcpgen/xcpgen.py --config Config/xcp_base.toml --show      # effective
python Tools/xcpgen/xcpgen.py --diff Config/profiles/can.toml           # vs base
python Tools/xcpgen/xcpserve.py                                       # web UI
```

## MISRA / 风格

- C99，`-Wall -Wextra -Werror=implicit-function-declaration` 零告警
- 无动态分配；全部状态位于 `Xcp_StorageType` + `Xcp_RuntimeType` +
  `g_xcp_channel`
- `Xcp_Channel` 携带 `config_ptr`/`storage_ptr`/`runtime_ptr`——内部函数
  显式接收它（除 `g_xcp_channel` 本身外无隐藏全局变量）

## 测试套件

- `Tests/run_all_tests.bat` —— Windows：regen + ratchet + 6 个功能变体 +
  predef + xcpgen 单元测试 + 矩阵
- `Tests/build_matrix.py` —— 跨平台矩阵运行器（103 个变体 × 编译 + 运行）
- `Tests/ratchet.py --tighten --no-gen` —— 清理之后更新 ratchet 基线

## 注意事项

- **`Xcp_EcuReset_Callout` 不得立即复位。** `PROGRAM_RESET` 在 callout 返回
  *之后*才发送其响应——请延迟复位（例如 `NvM`/`EcuM` 的 post-task）。
- `Xcp_DtoSendOrQueue` 保持队列 ≤ `XCP_DTO_QUEUE_DEPTH`——丢弃会触发
  `EV_DAQ_OVERLOAD` 事件，并带有冷却时间。
- `GET_DAQ_CLOCK` 以 `XCP_TIMESTAMP_TYPE` 指定大小的 `timestamp` 应答——请确认
  你的 `Xcp_GetTimestamp` 时间源（`OS` 计数器 vs 软件节拍）与
  `XcpTimestampSourceOs` 匹配。
