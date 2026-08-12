---
description: Run verification for the current work with a named validation owner; all project checks run through .\dev.ps1.
agent: orchestrator
---

Verify the current work described by: $ARGUMENTS

1. Identify the validation owner for each lane (default: you/orchestrator; reviewer lanes own their review verdicts).
2. Determine what must be verified from the Frozen Baseline acceptance criteria (`spec/v0.1/01 §6`), milestone exit conditions (`spec/v0.1/03 §3–§10`), and Definition of Done (`spec/v0.1/03 §13`).
3. Run validations through `.\dev.ps1` (contract/status in `spec/development-workflow.md §4`). Pre-M0 only `doctor` is implemented (`.\dev.ps1 doctor`). `build` / `test` / `verify` currently report `NotImplemented:` and exit non-zero — never report them as passed; record them as skipped/not-implemented with the reason. Do not substitute ad-hoc shell commands for project checks.
4. Reuse still-valid evidence; only re-run what the final state requires.
5. Report: what was verified, the commands run, a per-item result (passed/failed/skipped with reason), and the validation owner. Do not broaden the scope beyond the requested verification.
