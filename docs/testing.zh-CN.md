> **English**: [docs/testing.md](testing.md)

# 测试

测试按层次拆分，使失败能直接指向一类缺陷：协议逻辑、配置生成、特性开关、
传输行为或吞吐能力。**CI 是 pass/fail 的唯一事实源**；本文说明每层覆盖什么、
如何复现。

## 分层

| 层次 | 入口 | 目的 |
|------|------|------|
| 主机断言 | `Tests/test_main.c` | 每条已实现命令、错误路径和内存边界，按功能变体分别编译 |
| 特性矩阵 | `Tests/build_matrix.py` | 103 种传输/profile/特性组合，以 `-Werror` 编译 |
| 预定义 DAQ | `Tests/test_predef.c` | 静态与预定义 DAQ 列表配置 |
| 架构棘轮 | `Tests/ratchet.py` | 禁止全局符号、预处理复杂度、生成文件漂移回归 |
| 生成器测试 | `Tests/test_xcpgen.py` | `xcpgen` 校验、profile 展开与输出确定性 |
| pyxcp 冒烟 | `Tests/integration/test_pyxcp_master.py` | 连接、识别、解锁、内存访问、单个 DAQ 列表 |
| pyxcp 功能 | `Tests/integration/test_pyxcp_functional.py` | 完整命令面、保护矩阵、负向路径 |
| pyxcp 变体 | `Tests/integration/test_pyxcp_variants.py` | 特性关闭、静态/预定义 DAQ、交错模式、CAN-FD 边界 |
| pyxcp 性能 | `Tests/integration/test_pyxcp_perf.py` | CTO 延迟、批量传输、DAQ/STIM 吞吐、Flash |

## 主机测试

```bash
# Linux/macOS
python3 Tests/build_matrix.py

# Windows
Tests\run_all_tests.bat

# 架构与生成器检查
python3 Tests/ratchet.py --no-gen
python3 Tests/test_xcpgen.py
```

主机测试不把宿主指针直接当作 XCP 地址：测试对象通过
`Tests/test_stubs.c` 注册到映射后的 32 位地址空间，因此 PIE、ASLR 和宿主
指针宽度不会泄漏进协议地址。pyxcp 桥接层复用同一映射。

## pyxcp 端到端测试

slave 运行时被编译为共享库，并通过桥接层接入 `python-can` 的 `virtual`
总线。pyxcp 经正常传输回调与 C 代码交换真实 XCPonCAN 帧，无需 CAN 硬件。

```bash
python3 -m pip install -r Tests/integration/requirements.txt

python3 Tests/integration/test_pyxcp_master.py      # 冒烟
python3 Tests/integration/test_pyxcp_functional.py  # 命令 + 错误矩阵
python3 Tests/integration/test_pyxcp_variants.py    # 特性裁剪变体
python3 Tests/integration/test_pyxcp_perf.py        # 性能包络
```

### 功能覆盖

`test_pyxcp_functional.py` 使用默认 CAN profile，覆盖：

- 连接、状态、识别、`GET_COMM_MODE_INFO`、全部 `GET_ID` 模式、`SYNCH`、
  `TL_CMD` 与 `USER_CMD`
- 解锁前 CAL/DAQ/STIM/PGM 的保护矩阵（`ERR_ACCESS_LOCKED`），随后逐资源
  校验 seed 模式与解锁位
- 内存访问：`SET_MTA`、`UPLOAD`、`SHORT_UPLOAD`、`DOWNLOAD`、
  `DOWNLOAD_NEXT`、`DOWNLOAD_MAX`、`SHORT_DOWNLOAD`、`MODIFY_BITS`、
  `BUILD_CHECKSUM`，以及拒绝访问、越界和截断 CTO
- 标定页：processor/segment/page 信息、页访问模式、segment mode、页复制
  与非法页/段
- 动态 DAQ：最大分配、事件通道、预分频、时间戳、resume、同步启停、
  `READ_DAQ` 和实时 DTO 值；STIM 写入
- Flash：prepare、format、clear、program、next、max、verify、reset，时序
  错误、大小上限和校验失败
- resume 请求、畸形命令长度和非法连接模式

经典 CAN 的 CTO 只有 8 字节，带负载的 `SHORT_DOWNLOAD` 无法传输；功能套件
验证零负载形式与声明长度溢出诊断，CAN-FD 变体覆盖带负载传输。

### 特性变体

| 变体 | 构建 | 断言 |
|------|------|------|
| `minimal` | 关闭 DAQ、seed/key、CAL、PGM、块传输与 resume | 禁用命令返回 `ERR_CMD_UNKNOWN`；普通内存、ID、SYNCH 仍可用 |
| `seedkey_off` | `XcpSeedKeyEnabled=0` | 资源无保护；CAL/DAQ/PGM 无需解锁 |
| `daq_static` | `XcpDaqConfigType=DAQ_STATIC`、`XcpMinDaq=1` | 无需分配即可见静态列表；`ALLOC_DAQ`/`ALLOC_ODT` 被拒绝 |
| `daq_predef` | `XcpMinDaq=2` | 预定义列表、写保护、池溢出、启停 |
| `interleaved` | `XcpInterleavedModeEnabled=1` | 声明交错模式且命令通道仍可用 |
| `can_fd` | 64 字节 CTO/DTO、15 个 ODT entry | 带负载 `SHORT_DOWNLOAD`、60/62 字节传输与校验和 |

### 性能

性能套件分别运行经典 CAN 与 CAN-FD 构建，报告延迟、批量吞吐、最大
DAQ/STIM 配置和 Flash 编程。虚拟总线不建模 CAN 位时序；方法、参考数据与
解读见 [docs/performance.zh-CN.md](performance.zh-CN.md)。

## CI

`.github/workflows/ci.yml` 在 Ubuntu 和 Windows 上运行：

- 架构棘轮、profile 重新生成、xcpgen 测试
- 主机 CAN 构建与 103 变体矩阵
- pyxcp 冒烟、功能与特性变体套件
- pyxcp 性能（`XCP_PERF_SCALE=0.5`），JSON 报告作为构建产物上传

实时状态见 README 的 CI 徽章。文档不固定断言数量与耗时，因为它们随每次
提交变化；以 workflow 运行结果为准。
