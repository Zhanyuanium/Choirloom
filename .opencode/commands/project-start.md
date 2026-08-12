---
description: Initialize or advance a milestone (M0–M7) vertical slice from the Frozen Baseline; all project operations go through .\dev.ps1.
agent: orchestrator
---

You are starting work on the Choirloom project for: $ARGUMENTS

1. Read the Frozen Baseline first: `spec/v0.1/README.md`, `01-product-requirements.md`, `02-system-architecture.md`, `03-development-plan.md`, `04-decision-register.md`. Do not proceed on assumptions that contradict it.
2. Decompose the request into milestones and vertical slices per `spec/v0.1/03` (schema-first + harness-first + vertical-slice, §1). Map each task to a track (Core / Recognition / Audio / Windows UX / Harness-QA) and note the relevant exit conditions.
3. Build a short work graph: lanes, dependency ordering, and a validation owner per lane. Dispatch review lanes to @architect / @score-reviewer / @release-reviewer and ML lanes to @ml-researcher as needed; keep built-in routing (@explorer recon, @fixer implementation, @designer for any UI/UX) intact.
4. All project operations MUST go through `.\dev.ps1` (it exists; exact contract and status in `spec/development-workflow.md §4`). Currently implemented: `doctor`; scoped M0-002 harness commands `build|test|verify rational-time` (local CMake/CTest only; require cmake >= 3.20 and ctest on PATH plus a toolchain visible to cmake); `remote setup` (interactive wizard, local config only); `remote doctor` (read-only, requires local config). For `build|test|verify`, exactly one scope argument is accepted: missing scope -> **Usage**, unknown scope -> **Unknown scope**, extra arguments rejected (all exit non-zero; this is not a generic NotImplemented). `verify rational-time` also runs the control-plane dispatch checks (`tests/dev/dispatch.tests.ps1`, negative-path mode). `model validate|benchmark` / `remote submit|status|logs|cancel|fetch-report|fetch-model` and every other scope report `NotImplemented:` with the prerequisite and exit non-zero — never report them as passing, and do not bypass the control plane with ad-hoc shell commands for project operations. The scoped rational-time commands are a harness slice (disposable artifacts under `build/dev/rational-time`), not a full product build/test pipeline or CI gate; full M0 harness, ScoreIR fixture/schema harness, CI, migration/revision, and WinUI gates remain open.
5. Keep changes minimal and inside the milestone. Never edit frozen spec files; if a frozen decision must change, propose an ADR under `spec/adr/` instead.
6. Report the work graph, files touched, and validation results with the named validation owner per lane.
