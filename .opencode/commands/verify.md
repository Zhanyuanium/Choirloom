---
description: Run verification for the current work with a named validation owner; all project checks run through .\dev.ps1.
agent: orchestrator
---

Verify the current work described by: $ARGUMENTS

1. Identify the validation owner for each lane (default: you/orchestrator; reviewer lanes own their review verdicts).
2. Determine what must be verified from the Frozen Baseline acceptance criteria (`spec/v0.1/01 §6`), milestone exit conditions (`spec/v0.1/03 §3–§10`), and Definition of Done (`spec/v0.1/03 §13`).
3. Run validations through `.\dev.ps1` (contract/status in `spec/development-workflow.md §4`). Implemented commands: `doctor` (read-only); scoped harness commands `build|test|verify rational-time` (M0-002), `build|test|verify entity-revision` (M0-003), `build|test|verify schema-foundation` (M0-004), and `build|test|verify scoreir` (M0-005) (local CMake/CTest only; require cmake >= 3.20 and ctest on PATH; `dev.ps1` initializes the existing local MSVC Build Tools C++ environment when its build cache selects MSVC but the process lacks the MSVC environment, and never downloads/installs a toolchain). For `build|test|verify`, exactly one scope argument is accepted: a missing scope yields a **Usage** error, an unknown scope yields an **Unknown scope** error, and extra arguments are rejected — all exit non-zero. Each `verify <scope>` additionally runs the control-plane dispatch checks (`tests/dev/dispatch.tests.ps1`, negative-path mode). Every other verb/scope reports `NotImplemented:` or a prerequisite message and exits non-zero — never report them as passed; record them as skipped/not-implemented with the reason. Do not substitute ad-hoc shell commands for project checks.
4. Reuse still-valid evidence; only re-run what the final state requires.
5. Report: what was verified, the commands run, a per-item result (passed/failed/skipped with reason), and the validation owner. Do not broaden the scope beyond the requested verification. Note explicitly when only a scoped slice (e.g. rational-time) was verified and the full M0 milestone gate remains open.
