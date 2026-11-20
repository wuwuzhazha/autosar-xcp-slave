> **English**: [docs/feature-matrix.md](feature-matrix.md)

# XCP 功能矩阵

对照 ASAM XCP Part 2 v1.1 命令集的覆盖情况。`✓` = 已实现且被主机测试套件覆盖
（`Tests/test_main.c`、`Tests/test_matrix_func.c`、`Tests/test_predef.c`）。
通过真实 XCP 传输（pyxcp）的端到端覆盖见
[docs/testing.zh-CN.md](testing.zh-CN.md)。

## 标准命令（STD）

| 代码 | 命令 | 状态 |
|------|---------|--------|
| 0xFF | CONNECT | ✓ |
| 0xFE | DISCONNECT | ✓ |
| 0xFD | GET_STATUS | ✓ |
| 0xFC | SYNCH | ✓ |
| 0xFB | GET_COMM_MODE_INFO | ✓ |
| 0xFA | GET_ID（类型 0） | ✓ |
| 0xF9 | SET_REQUEST | ✓ |
| 0xF8 | GET_SEED（mode 0 首次 / mode 1 续传） | ✓ |
| 0xF7 | UNLOCK | ✓ |
| 0xF6 | SET_MTA | ✓ |
| 0xF5 | UPLOAD（启用块传输时支持从站块上传） | ✓ |
| 0xF4 | SHORT_UPLOAD | ✓ |
| 0xF3 | BUILD_CHECKSUM | ✓ |
| 0xF2 | TRANSPORT_LAYER_CMD（CAN GET_SLAVE_ID） | ✓ |
| 0xF1 | USER_CMD（callout） | ✓ |
| 0xF0 | DOWNLOAD | ✓ |
| 0xEF | DOWNLOAD_NEXT | ✓ |
| 0xEE | DOWNLOAD_MAX | ✓ |
| 0xED | SHORT_DOWNLOAD | ✓ |
| 0xEC | MODIFY_BITS | ✓ |

## 标定页（PAG）

| 代码 | 命令 | 状态 |
|------|---------|--------|
| 0xEB | SET_CAL_PAGE | ✓ |
| 0xEA | GET_CAL_PAGE | ✓ |
| 0xE9 | GET_PAG_PROCESSOR_INFO | ✓ |
| 0xE8 | GET_SEGMENT_INFO（mode 0 基本信息，mode 1 页信息，mode 2 映射） | ✓ |
| 0xE7 | GET_PAGE_INFO | ✓ |
| 0xE6 | SET_SEGMENT_MODE | ✓ |
| 0xE5 | GET_SEGMENT_MODE | ✓ |
| 0xE4 | COPY_CAL_PAGE（callout） | ✓ |

## DAQ / STIM

| 代码 | 命令 | 状态 |
|------|---------|--------|
| 0xE3 | CLEAR_DAQ_LIST | ✓ |
| 0xE2 | SET_DAQ_PTR | ✓ |
| 0xE1 | WRITE_DAQ | ✓ |
| 0xE0 | SET_DAQ_LIST_MODE | ✓ |
| 0xDF | GET_DAQ_LIST_MODE | ✓ |
| 0xDE | START_STOP_DAQ_LIST（mode 0/1/2 = 停止/启动/选中 + FIRST_PID） | ✓ |
| 0xDD | START_STOP_SYNCH（mode 0 全部停止，1 启动选中项，2 停止选中项） | ✓ |
| 0xDC | GET_DAQ_CLOCK（时间戳类型按 `XCP_TIMESTAMP_TYPE`） | ✓ |
| 0xDB | READ_DAQ | ✓ |
| 0xDA | GET_DAQ_PROCESSOR_INFO | ✓ |
| 0xD9 | GET_DAQ_RESOLUTION_INFO | ✓ |
| 0xD8 | GET_DAQ_LIST_INFO | ✓ |
| 0xD7 | GET_DAQ_EVENT_INFO（按 `event_channel_number`） | ✓ |
| 0xD6 | FREE_DAQ | ✓ |
| 0xD5 | ALLOC_DAQ | ✓ |
| 0xD4 | ALLOC_ODT | ✓ |
| 0xD3 | ALLOC_ODT_ENTRY | ✓ |

## Flash 编程（PGM）

| 代码 | 命令 | 状态 |
|------|---------|--------|
| 0xD2 | PROGRAM_START | ✓ |
| 0xD1 | PROGRAM_CLEAR | ✓ |
| 0xD0 | PROGRAM | ✓ |
| 0xCF | PROGRAM_RESET | ✓ |
| 0xCE | GET_PGM_PROCESSOR_INFO | ✓ |
| 0xCD | GET_SECTOR_INFO（mode 0 地址，mode 1 长度，mode 2 名称=0） | ✓ |
| 0xCC | PROGRAM_PREPARE（强制校验声明的大小） | ✓ |
| 0xCB | PROGRAM_FORMAT | ✓ |
| 0xCA | PROGRAM_NEXT | ✓ |
| 0xC9 | PROGRAM_MAX | ✓ |
| 0xC8 | PROGRAM_VERIFY | ✓ |

## 事件 / 服务报文

| 代码 | 名称 | 发出方 |
|------|------|------------|
| 0x00 | EV_RESUME_MODE | `Xcp_RestoreDaqConfig` |
| 0x01 | EV_CLEAR_DAQ | `Xcp_ClearStoredDaqConfig`（规划中） |
| 0x02 | EV_STORE_DAQ | `Xcp_NvMWriteRamBlockToNvmCallback` |
| 0x03 | EV_STORE_CAL | `Xcp_SendEventPacket`（集成方） |
| 0x05 | EV_CMD_PENDING | 派发器按需发出 |
| 0x06 | EV_DAQ_OVERLOAD | `Xcp_ReportDtoOverload` |
| 0x07 | EV_SESSION_TERMINATED | `Xcp_SendSessionTerminatedEvent` |
| 0x08 | EV_TIME_SYNC | `Xcp_SendEventPacket`（集成方） |
| 0x09 | EV_STIM_TIMEOUT | `Xcp_CheckStimTimeout` |
| 0x0A | EV_SLEEP | `Xcp_SendSleepEvent` |
| 0x0B | EV_WAKE_UP | `Xcp_SendWakeUpEvent` |
| 0xFE/0xFF | EV_USER / EV_TRANSPORT | `Xcp_SendEventPacket` |

## 发出的错误码

`ERR_CMD_SYNCH`、`ERR_CMD_BUSY`、`ERR_DAQ_ACTIVE`、`ERR_PGM_ACTIVE`、
`ERR_CMD_UNKNOWN`、`ERR_CMD_SYNTAX`、`ERR_OUT_OF_RANGE`、`ERR_ACCESS_DENIED`、
`ERR_ACCESS_LOCKED`、`ERR_PAGE_NOT_VALID`、`ERR_MODE_NOT_VALID`、
`ERR_SEGMENT_NOT_VALID`、`ERR_SEQUENCE`、`ERR_DAQ_CONFIG`、
`ERR_MEMORY_OVERFLOW`、`ERR_GENERIC`、`ERR_VERIFY`、
`ERR_RESOURCE_TEMP_NOT_ACC`、`ERR_WRITE_PROTECTED`。

## 块传输

`XcpBlockTransferEnabled=1` 同时启用两个方向：

- **主站块下载**——`DOWNLOAD_NEXT` / `DOWNLOAD_MAX`
- **从站块上传**——单个 `UPLOAD` 请求超过 `MAX_CTO-1` 个元素时，从站连续发送
  多个 `RES` 包；CONNECT 通告 `SLAVE_BLOCK_MODE`。`GET_COMM_MODE_INFO`
  通告 `MASTER_BLOCK_MODE` 及配置的 `MAX_BS` / `MIN_ST`。

## 声明的非目标

- **多通道**——设计上只有单个 `Xcp_Channel`。
- **运行时生成 A2L**——`Tools/xcpgen` 在生成时输出 `Gen/<profile>/Xcp_IfData.a2l`
  （离线生成，不走总线）。
- **SWS_00716 性能参数**——属于主站侧关注点。

## 传输层

| 链路 | 实现 |
|---|---|
| CAN | `XcpOnCan.c` —— CanIf_Transmit + GET_SLAVE_ID `0xFF` |
| FlexRay | `XcpOnFr.c` —— `LEN(2)|CTR(1)|DATA|FILL` 帧格式，初始化时调用 `FrIf_ReconfigLPdu` |
| Ethernet | `XcpOnEth.c` —— `LEN(2)|CTR(2)|DATA` 帧格式，`Xcp_SoAdConnectionClosed` |
| CDD | `XcpOnCdd.c` —— 名称通过 `XcpCdd*Name` 配置 |
