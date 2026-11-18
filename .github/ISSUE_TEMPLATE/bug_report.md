---
name: Bug report
about: Report a behavioral bug in the XCP slave
title: ''
labels: bug
assignees: ''
---

**Describe the bug**
A clear description of what the protocol behavior is vs what the spec requires.

**Profile / variant**
Which `Config/profiles/<name>.toml` (or custom config) you built. Link / on-CAN /
on-FlexRay / on-Ethernet / on-CDD?

**Reproduction**
Command sequence — hex bytes is ideal:

```
CONNECT 0xFF 0x00            → RES
SET_MTA 0xF6 ...             → RES
UPLOAD 0xF5 0x04             → ??? (expected ..., got ...)
```

**Environment**
- Host OS + toolchain: e.g. Ubuntu 24.04, gcc 13.2
- `Xcp_VersionInfo` / commit SHA
- Any deviation from `Config/xcp_base.toml`

**Extra context**
Logs, captures, anything else that helps reproduce.
