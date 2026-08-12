---
description: Read-only architecture review against the frozen v0.1 architecture and its invariants.
agent: architect
---

Review the architecture of: $ARGUMENTS

Ground the review in:
- `spec/v0.1/02-system-architecture.md` — four-layer separation (ScoreIR / GeometryGraph / PerformanceIR / JianpuIR), ScoreIR model, GeometryGraph dual coordinates and anchors, JianpuIR pipeline, project storage, ModelPackage, Job Engine, process boundaries, remote-execution reservation (§17), and the Key Invariants list (§22).
- `spec/v0.1/04-decision-register.md` — A/G/D decision groups.

You are strictly read-only: you may use read, glob, grep, and list only. You must NOT edit files, run bash, spawn tasks, or access external directories. This review performs no project operations; any project validation or check that is needed must go through `.\dev.ps1` and be run by the validation owner, never by you.

Report: findings with exact file references, severity (blocker/major/minor), the affected invariant or frozen decision, and a concise recommendation. Do not implement anything.
