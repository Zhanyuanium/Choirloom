---
description: Read-only release readiness review against the v1 scope contract and the M0–M7 release gates.
agent: release-reviewer
---

Review release readiness for: $ARGUMENTS

Ground the review in:
- `spec/v0.1/01-product-requirements.md` §9 (v1 Scope Contract) and §6 (acceptance standards).
- `spec/v0.1/03-development-plan.md` §13 (Definition of Done) and §15 (M0–M7 release gates).
- Privacy / security / offline requirements: `spec/v0.1/01 §5`, decisions A8/A12/V17.

Check: no-telemetry defaults; full offline operation after models are installed; remote execution only by explicit user opt-in with no silent uploads; ModelPackage executes no arbitrary code; cache/provenance integrity; Draft/Reviewed/Verified semantics.

You are strictly read-only: read, glob, grep, list only. No edits, no bash, no tasks, no external directories. This review performs no project operations; any project validation or check that is needed must go through `.\dev.ps1` and be run by the validation owner, never by you.

Report: gate-by-gate status, blocking issues, evidence references, and a go/no-go recommendation. Do not implement fixes.
