# Security Policy

XCP is a debugging/calibration protocol — a slave implementation with a memory
safety or unlock-bypass bug is a real ECU attack surface. Please report
vulnerabilities privately rather than via a public issue.

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| main    | ✓                  |
| < 0.1.0 | ✗ (pre-release)    |

Only the latest release tag receives security fixes.

## Reporting a Vulnerability

**Preferred**: [GitHub private vulnerability reporting](https://github.com/Harry/Xcp/security/advisories/new)
(if the repo is public on GitHub) — file a private advisory; maintainers see it
before it is disclosed.

**Otherwise**: open a regular issue **without** the vulnerability details and
ask for a private contact channel; a maintainer will follow up.

Do not include a proof-of-concept, memory dumps, exploit code, or a crafted
XCP frame in the public report.

## What to include

- Affected transport (CAN / FlexRay / Ethernet / CDD) and profile
- Which command or frame sequence triggers it (`[0xF0, ...]` hex is fine for a
  private report)
- Observed vs expected behavior (crash, out-of-bounds read/write, unlock bypass,
  information disclosure, hang, etc.)
- Whether `XCP_DEV_ERROR_DETECT` and `XCP_SEED_KEY_ENABLED` were on

## Scope

In scope:
- Memory safety violations in the XCP slave core (`Xcp/*.c`, `Xcp/*.h`)
- Authentication / unlock bypass in `Xcp_SeedKey.c`
- Out-of-bounds access through `Xcp_MemTranslate`/`Xcp_MemPtr*`/`mta`+`ext`
- Transport-level parsing bugs in `XcpOn{Can,Fr,Eth,Cdd}.c`
- DAQ/STIM/RESUME state corruption

Out of scope:
- Issues in your own callout / transport implementations (they are yours)
- Cryptographic quality of a user-supplied seed/key callout
- DoS against a non-XCP endpoint

## Disclosure timeline

- Acknowledgement: 7 days
- Triage + fix estimate: 30 days for high severity, 90 days for medium
- Public disclosure: coordinated with the reporter once a fix ships
