> **English**: [CHANGELOG.md](CHANGELOG.md)

# 更新日志

本文件记录项目的所有重要变更。

格式遵循 [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/spec/v2.0.0.html)。

## [未发布]

### 新增
- `PROGRAM_*` 参数、声明大小、callout 失败与 sector info 错误路径的主机测试；
  `Xcp_FlashProg.c` 文件覆盖率提升到 95.4%

### 变更
- CI 质量工具安装增加 apt 重试、网络与锁等待上限，并在 10 分钟内失败，
  不再无限挂起

## [0.3.1] — 2026-11-20

### 新增
- 块 UPLOAD 性能基准：每次请求 252 个元素，输出 `upload_block` JSON 结果，
  并断言其请求数少于普通 UPLOAD
- pyxcp 覆盖：DAQ 运行期间进行块 UPLOAD
- `Tests/quality.sh` 生成 cppcheck 的提示性 MISRA 报告，CI 将其作为构建
  产物上传；该报告不是合规门禁

### 变更
- actions/checkout、actions/setup-python 与 actions/upload-artifact
  更新到当前 Node 24 版本
- Windows CI 改用 MSYS2 UCRT64 工具链，替代已弃用的 MINGW64

### 修复
- 新的块 UPLOAD 会取消上一次下载遗留的块传输，不再返回 `ERR_SEQUENCE`

## [0.3.0] — 2026-11-20

### 新增
- `Tests/quality.sh`：clang 主机 + 103 变体矩阵、ASan/UBSan、gcovr 覆盖率
  门禁（行 ≥80%、函数 ≥90%）、Cortex-M0/M4 ARM 交叉编译与 cppcheck 静态分析
- CI `quality` job 在 Ubuntu 上运行完整门禁；覆盖率 XML/HTML 作为构建产物上传
- 从站块上传：启用块传输时，单个超过 `MAX_CTO-1` 个元素的 `UPLOAD` 会连续
  发送多个 `RES` 包；CONNECT 通告 `SLAVE_BLOCK_MODE`

### 变更
- 收紧架构棘轮上限：`g_xcp_channel` 177 → 172，
  `xcp_preprocessor` 171 → 167

## [0.2.0] — 2026-11-20

### 新增
- 开源就绪：MIT LICENSE、README、CONTRIBUTING、CODE_OF_CONDUCT、
  SECURITY、.github CI + 模板、.editorconfig、.gitattributes、
  .clang-format、CHANGELOG
- `docs/` — 架构、移植指南、特性矩阵（英文）
- `examples/minimal/` — 主机可构建的最小 bring-up 示例
- 可移植钩子 `XCP_MEM_PTR`/`XCP_MEM_ADDR`；主机测试通过映射后的 32 位
  地址空间注册对象，不再依赖 PIE、ASLR 或宿主指针宽度
- `Tests/integration/` 中的 pyxcp 端到端脚手架：slave 编译为共享库，
  通过 `python-can` 虚拟总线交换真实 XCPonCAN 帧
- 功能套件：完整命令面、解锁前保护矩阵、逐资源 seed/key、标定页、
  动态 DAQ/STIM、Flash 时序与负向路径
- 特性变体套件：minimal、seed-key-off、static、predefined、interleaved
  以及 64 字节 CAN-FD profile
- 性能套件：CTO 延迟、批量上下传、最大 DAQ/STIM 配置、Flash 编程，
  并输出 JSON 报告
- `docs/testing.md` 与 `docs/performance.md`，含中文镜像
- CI 在 Ubuntu 和 Windows 上运行 pyxcp 冒烟、功能与变体套件，并将性能
  报告作为构建产物上传
- `XCP_DTO_QUEUE_DEPTH` 配置项（与 `XCP_MAX_ODT` 解耦）
- `Xcp_ExtValid` 用于 `SET_MTA` 地址扩展合法性校验
- `Xcp_EventChannelIdx` — event_channel_number → 配置表索引 查找

### 修复
- `SET_REQUEST`：`STORE_DAQ_NO_RESUME`（位 1）现在是合法组合
  （存储 DAQ 但跳过 RESUME）；之前会返回 `ERR_OUT_OF_RANGE`
- `GET_SEED mode=1` — 支持超过 `XCP_MAX_CTO-2` 的种子续传
- `DOWNLOAD_NEXT` 在 `XCP_BLOCK_TRANSFER_ENABLED == STD_OFF` 时
  返回 `ERR_CMD_UNKNOWN`
- `BUILD_CHECKSUM` 现在先经 `Xcp_MemTranslate` 翻译 `mta` 再调用 callout
- `START_STOP_SYNCH` mode 语义：0=stop_all、1=start_selected、
  2=stop_selected
- `GET_PAGE_INFO` 字节 2 返回 `ecu_page_properties`（原为 `segment_number`）
- `GET_SEGMENT_INFO` mode 2 返回请求页的 `(ext, page_start)`
- `START_STOP_DAQ_LIST` 的 FIRST_PID 在 relative PID 模式下返回 list 号
- `PROGRAM_PREPARE` 声明的 size 在 `PROGRAM`/`NEXT`/`MAX` 上强制校验
- `WRITE_DAQ` size 按 `XCP_ODT_ENTRY_SIZE_{DAQ,STIM}` 校验
- `Xcp_Tl_SendDto` 移出 SchM 临界区
- `SessionClose` 清除会话内运行时状态（`mta`、`seed_*`、`pgm_*`）
- `FrIf_ReconfigLPdu` 现在传入 `XCP_FR_MAX_FRAME`（含头部）
- 静态/外部 TX + 计数器状态收敛进 `Xcp_StorageType`/`Xcp_RuntimeType`
- 移除 `Xcp_ProcessStim` 死代码（STIM 是 RX 推送，不是轮询）
- 资源保护：`XCP_RESOURCE_DAQ`/`XCP_RESOURCE_STIM` 现在管控
  DAQ 配置路径与 STIM 写入路径
- `SessionOpen` 保留 `STORE_CAL_REQ`/`CLEAR_DAQ_REQ` 位供主站回读
- `GET_DAQ_CLOCK` 按 `ts_size` 返回长度，并在
  `XCP_TIMESTAMP_TYPE == NO_TIME_STAMP` 时返回 `ERR_CMD_UNKNOWN`
- `PGM_PROPERTIES` 不再声明 `NON_SEQ_PGM_SUPPORTED`（未实现）

### 移除
- `Xcp_ProcessStim`（死代码 — STIM 为 RX 推送）
- 只写不读的 `block_sequence_counter`/`expected_block_sequence` 字段
- 非标准的 `TIMESTAMP_UNIT_{1,10,100}PS` 值
- AUTOSAR 机密规范文档（`AUTOSAR_SWS_XCP.pdf`、`AUTOSAR_SRS_XCP.pdf`、
  `srs.txt`、`sws.txt`）
- `Docs/xcp-kernel.html`（内部架构草稿）
- 仓库根部的零散分析文件

## [0.1.0] — 2026-11-18

首个公开发布。按 ASAM XCP Part 2 v1.1 实现 STD + CAL + PAG +
DAQ + STIM + PGM + Seed&Key + RESUME 命令全覆盖，支持 CAN /
FlexRay / Ethernet / CDD 传输。
