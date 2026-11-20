> **English**: [docs/performance.md](performance.md)

# 性能

以下数字是**参考包络，不是芯片或总线基准**。它们描述集成测试使用的完整
路径：pyxcp、Python 帧泵、ctypes、桥接层和 C slave。这些数据适合做回归对比
和负载横向比较，不构成量产 ECU 或真实 CAN/CAN-FD 网络的性能承诺。

## 方法

两个生成的 profile 通过 pyxcp 在 `python-can` 的 `virtual` 总线上运行：

| 构建 | CTO/DTO | ODT entry | DTO 队列 |
|------|---------|-----------|----------|
| 经典 CAN | 8 字节 | 1 个 4 字节 entry | 128 |
| CAN-FD | 64 字节 | 15 个 4 字节 entry | 128 |

UPLOAD/DOWNLOAD 在两种构建上传输相同的 8 KiB 应用数据。UPLOAD 测量两次：
普通模式每帧发送一个 `CTO-1` 大小的请求（7/63 字节）；块模式每个 `UPLOAD`
发送 252 个元素，slave 连续返回 36 个经典 CAN 或 4 个 CAN-FD `RES` 帧。
DAQ 使用最大动态配置：8 列表 × 4 ODT，事件通道 0（10 ms）用于持续负载，
事件通道 1（1 ms）用于峰值测试。STIM 使用相同的 8 × 4 ODT 几何，通过
pyxcp 的 `Stim.triggerEvent` 发送；每种构建发送 12 800 帧，桥接层统计
每一帧是否到达 slave。

### 指标

- **CTO 延迟**：一次命令/响应往返，包含 pyxcp 与桥接线程；p50/p95/p99 基于
  300 次命令。
- **批量吞吐**：UPLOAD/DOWNLOAD 的应用字节/秒。普通 UPLOAD 使用单帧 CTO
  能容纳的最大负载（7/63 字节）；块 UPLOAD 每次请求 252 个元素并连续返回。
  DOWNLOAD 每帧 6/62 字节。
- **DAQ 采样字节**：仅配置的 entry 数据（`entries × 4`）。
- **DAQ 线上字节**：PID 之后的完整 DTO 负载，包含 slave 补齐到
  `XCP_MAX_DTO` 的字节。
- **模拟时间**：slave OS tick 计数器的增量，即真正调度事件的时间。Python
  tick 线程可能慢于墙钟，因此采样吞吐同时按墙钟秒和模拟秒报告。
- **投递率**：实际 DTO 数除以模拟 tick 调度出的 DTO 数。
- **DTO 丢弃**：slave 自身的队列溢出计数。

## 参考运行

环境：Windows 10 x64、CPython 3.13.13、pyxcp 0.30.5、python-can 4.6.1、
`v0.3.0` 之后的 main、`XCP_PERF_SCALE=1.0`（运行约 28 秒）。

| 指标 | 经典 CAN | CAN-FD |
|------|---------:|-------:|
| CTO 延迟 p50 | 1.59 ms | 1.22 ms |
| CTO 延迟 p99 | 7.66 ms | 2.18 ms |
| CTO 速率 | 533 cmd/s | 749 cmd/s |
| UPLOAD 普通 | 4.4 kB/s | 46.8 kB/s |
| UPLOAD 块 | 30.5 kB/s | 155.0 kB/s |
| UPLOAD 块请求数 | 33 | 33 |
| DOWNLOAD | 3.6 kB/s | 48.3 kB/s |
| DAQ event0 帧率 | 1 752 /s | 2 417 /s |
| DAQ event0 采样（墙钟） | 7.0 kB/s | 145.0 kB/s |
| DAQ event0 采样（模拟时间） | 12.8 kB/s | 192.0 kB/s |
| DAQ event0 线上字节 | 12.3 kB/s | 152.2 kB/s |
| DAQ event0 投递 / 丢弃 | 1.01 / 0 | 1.00 / 0 |
| DAQ event1 帧率 | 5 399 /s | 12 961 /s |
| DAQ event1 采样（模拟时间） | 81.8 kB/s | 1 920.1 kB/s |
| DAQ event1 丢弃 | 2 954 | 0 |
| STIM 帧率 | 10 499 /s | 31 906 /s |
| STIM 负载 | 42.0 kB/s | 1 914.4 kB/s |
| Flash 编程 | 2.6 kB/s | 8.0 kB/s |

DAQ 采样在模拟时间下与配置精确吻合：

```
经典:   32 DTO × 4 B  / 10 ms = 12.8 kB/s
CAN-FD: 32 DTO × 60 B / 10 ms = 192 kB/s
```

在事件通道 1（1 ms）下，如果主机来不及排空，32 DTO 的突发可能超过 128 深
队列。本次运行报告经典 CAN 丢弃 2 954 次、FD 为 0；计数会随主机负载变化。
这是预期的队列深度限制，不是数据损坏（所有已投递采样均与 RAM 一致）。

## 解读

`virtual` 总线是本地内存队列，不建模仲裁、1 Mbit/s 仲裁段、2–5 Mbit/s
FD 数据段、CRC 或收发器时序。因此：

- **帧受限负载没有 FD 收益。** DAQ 每个 ODT 每个事件产生一个 DTO，与 DTO
  大小无关；CTO 是一次请求/响应往返。帧率和命令速率基本不变。
- **字节受限负载获得 payload 比例收益。** UPLOAD/DOWNLOAD 约 9–10×
  （63/7 与 62/6），DAQ 采样吞吐 15×，因为 FD 的每个 ODT 承载 15 个 entry
  而非 1 个。
- **块 UPLOAD 消除逐帧往返。** 同样 8 KiB 从 1 171 个经典 CAN / 131 个 FD
  请求降为 33 个请求，本次运行中经典 CAN 应用吞吐提升 7×，FD 提升 3.3×。
  在真实总线上，收益受链路帧率限制，而不是主机往返。
- **填充字节影响口径。** DTO 被补齐到 `XCP_MAX_DTO`，所以线上字节增长 9×，
  而真实采样字节增长 15×；只把整帧当 payload 会低估 FD 优势。
- **真实 CAN-FD 没有这些比例那么快。** 仲裁段仍为 1 Mbit/s，FD CRC 更长；
  相对经典 CAN 的净吞吐提升通常在 3–7×，不是 15×。
- **Flash 受命令数限制。** 每个 32 字节 prepare 块仍需 `PROGRAM_PREPARE`、
  `SET_MTA` 和一次或多次 `PROGRAM` 往返，因此实测只有约 2.8×。

### 为什么 STIM 比 DOWNLOAD 和 DAQ 快很多

STIM 是单向、无确认的注入路径：pyxcp 的 `Stim.triggerEvent` 构造帧后直接
发送，不等待响应；slave 在接收路径里直接写内存，没有响应帧、DTO 调度器或
发送队列。DOWNLOAD 每帧都要等一个 RES，DAQ 是事件驱动的 ECU 发送，所以
三者测的是不同瓶颈。约 49 000 帧/s 是 Python/pyxcp 发送端加桥接层的容量，
不是 CAN 总线能力：1 Mbit/s 下 8 字节经典帧占用总线约 130 µs，真实总线
上限约 7–8 k 帧/s；2 Mbit/s 下 64 字节 FD 帧约 300 µs，上限约 3 k 帧/s。
不要直接把 STIM 的帧率与 DAQ 或 DOWNLOAD 的帧率比较。

## 复现

```bash
python3 -m pip install -r Tests/integration/requirements.txt
python3 Tests/integration/test_pyxcp_perf.py

# CI 规模的快速运行（传输大小和观察窗口减半）
XCP_PERF_SCALE=0.5 python3 Tests/integration/test_pyxcp_perf.py
```

JSON 报告写入 `Tests/integration/.build/perf.json`，CI 会将其作为构建产物
上传。脚本只断言数据完整性和宽松的能力下限（例如批量传输至少 1 kB/s），
不锁定上面的参考数值，避免较慢的 CI 主机产生误报。
