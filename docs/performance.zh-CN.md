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

UPLOAD/DOWNLOAD 在两种构建上传输相同的 8 KiB 应用数据。DAQ 使用最大动态
配置：8 列表 × 4 ODT，事件通道 0（10 ms）用于持续负载，事件通道 1（1 ms）
用于峰值测试。STIM 使用相同的 8 × 4 ODT 几何，并校验最终内存镜像。

### 指标

- **CTO 延迟**：一次命令/响应往返，包含 pyxcp 与桥接线程；p50/p95/p99 基于
  300 次命令。
- **批量吞吐**：UPLOAD/DOWNLOAD 的应用字节/秒，使用 CTO 能容纳的最大负载
  （UPLOAD 7/63 字节，DOWNLOAD 6/62 字节）。
- **DAQ 采样字节**：仅配置的 entry 数据（`entries × 4`）。
- **DAQ 线上字节**：PID 之后的完整 DTO 负载，包含 slave 补齐到
  `XCP_MAX_DTO` 的字节。
- **模拟时间**：slave OS tick 计数器的增量，即真正调度事件的时间。Python
  tick 线程可能慢于墙钟，因此采样吞吐同时按墙钟秒和模拟秒报告。
- **投递率**：实际 DTO 数除以模拟 tick 调度出的 DTO 数。
- **DTO 丢弃**：slave 自身的队列溢出计数。

## 参考运行

环境：Windows 10 x64、CPython 3.13.13、pyxcp 0.30.5、python-can 4.6.1、
提交 `8920e38`、`XCP_PERF_SCALE=1.0`（运行约 22 秒）。

| 指标 | 经典 CAN | CAN-FD |
|------|---------:|-------:|
| CTO 延迟 p50 | 1.21 ms | 1.28 ms |
| CTO 延迟 p99 | 2.23 ms | 2.30 ms |
| CTO 速率 | 762 cmd/s | 746 cmd/s |
| UPLOAD | 5.2 kB/s | 49.3 kB/s |
| DOWNLOAD | 4.4 kB/s | 47.2 kB/s |
| DAQ event0 帧率 | 2 177 /s | 2 337 /s |
| DAQ event0 采样（墙钟） | 8.7 kB/s | 140.2 kB/s |
| DAQ event0 采样（模拟时间） | 12.7 kB/s | 191.1 kB/s |
| DAQ event0 线上字节 | 15.2 kB/s | 147.2 kB/s |
| DAQ event0 投递 / 丢弃 | 1.00 / 0 | 1.00 / 0 |
| DAQ event1 帧率 | 13 383 /s | 13 062 /s |
| DAQ event1 采样（模拟时间） | 127.8 kB/s | 1 897.6 kB/s |
| DAQ event1 丢弃 | 0 | 185 |
| STIM 帧率 | 54 674 /s | 49 939 /s |
| STIM 负载 | 219 kB/s | 2 996 kB/s |
| Flash 编程 | 3.1 kB/s | 8.6 kB/s |

DAQ 采样在模拟时间下与配置精确吻合：

```
经典:   32 DTO × 4 B  / 10 ms = 12.8 kB/s
CAN-FD: 32 DTO × 60 B / 10 ms = 192 kB/s
```

在事件通道 1（1 ms）下，32 DTO 的突发超过本机 128 深队列，FD 构建报告
185 次丢弃；经典构建仍在队列容量内。这是预期的队列深度限制，不是数据
损坏（所有已投递采样均与 RAM 一致）。

## 解读

`virtual` 总线是本地内存队列，不建模仲裁、1 Mbit/s 仲裁段、2–5 Mbit/s
FD 数据段、CRC 或收发器时序。因此：

- **帧受限负载没有 FD 收益。** DAQ 每个 ODT 每个事件产生一个 DTO，与 DTO
  大小无关；CTO 是一次请求/响应往返。帧率和命令速率基本不变。
- **字节受限负载获得 payload 比例收益。** UPLOAD/DOWNLOAD 约 9–10×
  （63/7 与 62/6），DAQ 采样吞吐 15×，因为 FD 的每个 ODT 承载 15 个 entry
  而非 1 个。
- **填充字节影响口径。** DTO 被补齐到 `XCP_MAX_DTO`，所以线上字节增长 9×，
  而真实采样字节增长 15×；只把整帧当 payload 会低估 FD 优势。
- **真实 CAN-FD 没有这些比例那么快。** 仲裁段仍为 1 Mbit/s，FD CRC 更长；
  相对经典 CAN 的净吞吐提升通常在 3–7×，不是 15×。
- **Flash 受命令数限制。** 每个 32 字节 prepare 块仍需 `PROGRAM_PREPARE`、
  `SET_MTA` 和一次或多次 `PROGRAM` 往返，因此实测只有约 2.8×。

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
