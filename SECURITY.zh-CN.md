> **English**: [SECURITY.md](SECURITY.md)

# 安全策略

XCP 是一种调试/标定协议——从站实现中若存在内存安全漏洞或解锁绕过缺陷，就是真实的 ECU 攻击面。请通过私密渠道报告漏洞，而不是提交公开 issue。

## 支持的版本

| 版本    | 是否支持        |
| ------- | --------------- |
| main    | ✓               |
| < 0.1.0 | ✗（预发布版本） |

只有最新的 release 标签会收到安全修复。

## 报告漏洞

**首选方式**：[GitHub 私密漏洞报告](https://github.com/Harry/Xcp/security/advisories/new)
（如果仓库在 GitHub 上是公开的）——提交私密 advisory；维护者会在漏洞披露前看到它。

**其他方式**：提交一个常规 issue，但**不要**包含漏洞细节，并在其中请求一个私密联系渠道；维护者会跟进处理。

请勿在公开报告中包含概念验证（PoC）、内存转储、漏洞利用代码或精心构造的 XCP 帧。

## 报告应包含的内容

- 受影响的传输层（CAN / FlexRay / Ethernet / CDD）以及所用的 profile
- 触发漏洞的命令或帧序列（私密报告中可以使用 `[0xF0, ...]` 这样的十六进制表示）
- 观察到的行为与预期行为的对比（崩溃、越界读/写、解锁绕过、信息泄露、挂起等）
- `XCP_DEV_ERROR_DETECT` 和 `XCP_SEED_KEY_ENABLED` 是否已开启

## 范围

在范围内：
- XCP 从站核心中的内存安全违规（`Xcp/*.c`、`Xcp/*.h`）
- `Xcp_SeedKey.c` 中的认证/解锁绕过
- 通过 `Xcp_MemTranslate`/`Xcp_MemPtr*`/`mta`+`ext` 进行的越界访问
- `XcpOn{Can,Fr,Eth,Cdd}.c` 中的传输层解析缺陷
- DAQ/STIM/RESUME 状态损坏

不在范围内：
- 你自己的 callout / 传输层实现中的问题（它们由你负责）
- 用户提供的 seed/key callout 的密码学强度
- 针对非 XCP 端点的 DoS

## 披露时间线

- 确认收到：7 天
- 分级处理 + 修复时间预估：高危 30 天，中危 90 天
- 公开披露：修复发布后与报告者协调进行
