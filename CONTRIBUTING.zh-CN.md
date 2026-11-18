> **English**: [CONTRIBUTING.md](CONTRIBUTING.md)

# 贡献指南

感谢你的关注。本项目是 ASAM XCP Part 2 v1.1 从站（slave）的独立实现 —— 只要能让代码库保持小巧、可移植，并如实说明所实现的内容，欢迎任何形式的贡献。

> **关于代码来源。** 本代码库的大部分内容由 AI agent 在自测循环下生成
> （详见 README "项目起源与免责声明"）。来自领域专家的缺陷报告与修复
> 尤其有价值 —— 如果某行为对照规范可疑，请开 issue 并引用具体的
> ASAM/AUTOSAR 条款，而不是只修补表象。
>
> 如果你认为本仓库任何内容侵犯了你的版权或知识产权，请通过 issue 告知。
> 涉及文件将被立即删除；如有需要，整个仓库也可以被关闭或转为私有。

## 环境准备

```bash
git clone <repo>
cd Xcp

# Validate config + regen all profiles
python Tools/xcpgen/xcpgen.py --config Config/xcp_base.toml --check

# Run the full suite (Windows)
Tests\run_all_tests.bat

# Linux/macOS equivalent
python3 Tests/build_matrix.py
```

所需工具：`python3`（推荐 3.11+，使用 `tomllib`）、`gcc`（C99）、
`make`/`bash` 用于本地测试编排。没有其他依赖 —— 主机测试框架使用
`Tests/stubs/` 下的桩代码。

## 工作流程

1. 挑选一个 issue，或者新开一个 issue 说明你要做什么。
2. Fork / 新建分支，完成修改，添加或扩展测试。
3. 在本地运行 `Tests\run_all_tests.bat`（或等价的 Python 脚本）—— 测试套件必须保持全绿。
4. 如果你的改动降低了 `Tests/ratchet.py` 跟踪的架构计数器（例如减少了
   `g_xcp_channel` 引用、`#if XCP_*` 条件编译或 `.c` 文件的 `#include`），
   请运行 `python Tests/ratchet.py --tighten --no-gen`，并在同一次提交中提交基线更新。
5. 提交 PR，并在描述中说明你改了什么、为什么改。

## 约定规范

- **语言**：代码、注释和提交信息一律使用英文。代码库中历史遗留的中文注释正在逐步淘汰 —— 请确保新的贡献使用英文。
- **C 语言**：仅限 C99。不使用 C11 atomics，不使用 GCC 8 无法解析的指派初始化器指针链，不使用动态内存分配。请与现有风格保持一致 ——
  局部变量用 `snake_case`，导出符号用 `Xcp_PascalCase`，宏用 `XCP_*`。
- **配置**：绝不要手工编辑 `Gen/<profile>/` —— 始终通过
  `python Tools/xcpgen/xcpgen.py --profile <name> --out Gen/<name>` 重新生成，并提交差异。
  `Config/xcp_base.toml` 和 `Config/profiles/*.toml` 是唯一事实源。
- **测试**：每一处行为改动都必须有断言覆盖，位于 `Tests/test_main.c`
  （完整变体）、`Tests/test_matrix_func.c`（按特性门控的矩阵测试）或
  `Tests/test_predef.c`（静态/预定义 DAQ）中。测试断言通过分区字母 + 序号标识
  （例如 `B3`、`E27`、`K12d`）—— 新检查取所属分区的下一个序号。
- **提交**：主题行使用简洁的祈使句，中英文均可；每次提交只包含一个逻辑改动。
  只有当你与另一个人类或代理共同署名时，才添加 `Co-Authored-By` 行。

## 报告缺陷

提交 issue 时请附上：
- 你构建的 XCP 变体 / profile（`Config/profiles/<name>.toml`）
- 主机环境（操作系统、编译器、`gcc --version` 输出）
- 复现方式：命令序列（例如 `[CONNECT, SET_MTA, UPLOAD n=4]`）以及期望与实际响应帧的对比

对于**安全相关**的问题（内存安全、通过构造 XCP 帧造成的拒绝服务、unlock 绕过），
请**不要**提交公开 issue —— 私密报告渠道请参见
[`SECURITY.md`](SECURITY.md)。

## PR 审查清单

- [ ] 新行为至少有一个 `CHECK()` 断言覆盖，位于 `Tests/test_main.c` 或矩阵测试套件中
- [ ] `python Tests/ratchet.py --no-gen` 通过（或已在提交信息中说明理由并收紧基线）
- [ ] `run_all_tests.bat` / `build_matrix.py` 在受影响的变体上全绿
- [ ] 没有在 `Xcp/Xcp.c` 之外新增 `g_xcp_channel` 引用（ratchet 禁止在模块文件中新增）
- [ ] `.c` 文件顶层没有新增 `#if XCP_*`（应下沉到叶子辅助函数 —— 参见 `Xcp_MemTranslate` 模式）
- [ ] 注释和标识符均为英文
- [ ] 新增公共 API 已在 `docs/` 中记录；如影响线上行为，还需更新 `docs/feature-matrix.md`

## 许可证

提交贡献即表示你同意你的贡献按本项目的 [MIT License](LICENSE) 授权。
