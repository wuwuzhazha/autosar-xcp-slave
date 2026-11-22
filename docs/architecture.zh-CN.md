> **English**: [docs/architecture.md](architecture.md)

# 架构

单通道 XCP 从站，采用分层设计，在协议状态、传输层与集成层之间划清界限。

```
   ┌──────────────────────────────────────────────────────────┐
   │                   Host application                       │
   │    Xcp_Event(n)  Xcp_SendEventPacket  Xcp_SetTransmissionMode │
   └──────────────────────────────────────────────────────────┘
                            │
   ┌──────────────────────────────────────────────────────────┐
   │                  Xcp.c (entry layer)                     │
   │   Init | MainFunction | EnqueueCommand | ProcessCommandQueue │
   │   HandleRxIndication | HandleTxConfirmation | TriggerTransmit │
   │   SendResponse | SendPendingCto | SessionOpen/Close       │
   └──────────────────────────────────────────────────────────┘
                            │
   ┌──────────────────────────────────────────────────────────┐
   │                  Xcp_Cmd.c (dispatch)                    │
   │   { cmd, min_len, resource, guard, fn } table            │
   │   only writer to Xcp_Tl_SendCto (CTO path)               │
   └──────────────────────────────────────────────────────────┘
                            │
   ┌─────────────┬───────────────┬──────────────┬─────────────┐
   │  Xcp_Std.c  │ Xcp_DaqCfg.c  │ Xcp_CalPage.c│ Xcp_FlashProg.c│
   │  STD/CAL    │ DAQ config    │ PAG commands │ PGM commands │
   │             │               │              │              │
   │  Xcp_DaqRun.c  Xcp_DaqStim.c  Xcp_SeedKey.c  Xcp_Resume.c  │
   │  DAQ run   │  STIM receive │  Seed & Key │  RESUME+NvM   │
   └─────────────┴───────────────┴──────────────┴─────────────┘
                            │
   ┌──────────────────────────────────────────────────────────┐
   │      XcpOnCan / XcpOnFr / XcpOnEth / XcpOnCdd            │
   │   TL shells + frame pack for FR/ETH                      │
   └──────────────────────────────────────────────────────────┘
                            │
   ┌──────────────────────────────────────────────────────────┐
   │              Host integration (your code)                │
   │   CanIf_Transmit | FrIf_Transmit | SoAd_IfTransmit        │
   │   callouts (seed/key, flash, checksum, ecu reset)         │
   └──────────────────────────────────────────────────────────┘
```

## 状态所有权

`Xcp_Channel` 是传给内部函数的唯一对象：

```c
struct Xcp_Channel {
    const Xcp_ConfigType  *config_ptr;   /* generated const config    */
    Xcp_StorageType       *storage_ptr;  /* DTO/queue/segment buffers */
    Xcp_RuntimeType       *runtime_ptr;  /* session + protocol state  */
};
extern Xcp_Channel g_xcp_channel;
```

- **`g_xcp_channel`** 是唯一的模块级单例。其余一切都挂在
  `storage`/`runtime`/`config` 上，因此重新初始化就是一次 `memset` 加重新绑定。
- **`Xcp_TxPipe`**（位于 `Xcp_RuntimeType` 中）—— CTO 与 DTO 共用的一个发送槽。
  `state` ∈ {`IDLE`, `CTO`, `DTO`}，`cto_*` 是保存被推迟应答的 FIFO（每条排队
  命令一条，`cto_claimed` 记录已为某条命令预留、`cto_inflight` 标记已交给下层
  的那条），`dto_latch` 是看门狗标志。
- **`Xcp_ActiveDaqIndices`/`Count`** —— 重建索引列表，使 `Xcp_ProcessDaq`
  只遍历处于 `RUNNING` 状态的列表。
- **`Xcp_SegmentRuntime[]`** —— 每个段的 `active_page_ecu`/`active_page_xcp`。

## 发送流程

```
User code (DAQ event)             Command dispatch
        │                                 │
        ▼                                 ▼
  Xcp_DtoSendOrQueue              Xcp_SendResponse
        │                                 │
        ▼                                 ▼
  state=DTO ──send──► bus   OR   state=CTO ──send──► bus
        │                                 │
        └──────►  追加 CTO 到 tx.cto_*  ◄───────┘
                            │
              TxConfirmation 释放队头 → 继续 DTO 队列
```

调度器是唯一的 CTO 发送方。`Xcp_SendPendingCto` 由 `Xcp_ReportDtoOverload`
和 `TxConfirmation` 调用，因此即使 DTO 抢占了总线，被推迟的 RES/ERR
最终也会发出；FIFO 保持命令顺序：发送失败的那条留在队头重试，不会被改写。

## 内存寻址

`mta`（32 位）+ `mta_extension`（8 位扩展）→ 转换后的指针：

```
ext >> 4 == 0      → raw addr (no translation)
ext >> 4 == s      → segment s-1's start + active_page_xcp × page_len + addr
```

所有访问都经过 `Xcp_MemPtrRead`/`Xcp_MemPtrWrite` —— 这一处统一调用
`XCP_CHECK_{READ,WRITE}_ACCESS_EX`，访问被拒绝时返回 `NULL`。
`Xcp_ExtValid` 在前置校验中检查扩展字段（`SET_MTA`、`WRITE_DAQ`、
`SHORT_DOWNLOAD`、`SHORT_UPLOAD`）。

## 配置 → 代码

```
Config/xcp_base.toml  +  Config/profiles/*.toml (extends)
            │
            ▼
       Tools/xcpgen/xcpgen.py
            │
            ▼
   Gen/<profile>/Xcp_Cfg.h    — all XCP_* macros + #error invariants
   Gen/<profile>/Xcp_Cfg.c    — const tables (event/segment/sector/daq)
   Gen/<profile>/Xcp_MemMap.h — placeholder
   Gen/<profile>/Xcp_IfData.a2l — master-side description file
```

`xcpgen.py` 是**唯一**的配置来源。`Xcp/Xcp_Config.h` 只是一个 `#include`
了 `Xcp_Cfg.h` 的垫片。棘轮脚本（`Tests/ratchet.py`）会将每个 profile
重新生成到临时目录，并与 `Gen/<profile>/` 逐字节比对，以发现配置漂移。

## 测试

```
Tests/run_all_tests.bat     一键：regen → ratchet → 构建+运行 6 个变体
                            → predef → xcpgen 单测 → 103 个变体矩阵
Tests/test_main.c           ~317 断言/变体 — 每条命令 + 错误路径
Tests/test_matrix_func.c    按特性自适应的冒烟测试
Tests/test_predef.c         静态/预定义 DAQ 覆盖
Tests/build_matrix.py       跨平台变体构建器
Tests/ratchet.py            架构棘轮（回归上限）
```
