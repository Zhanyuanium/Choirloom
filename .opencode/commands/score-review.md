---
description: Read-only review of score-domain semantics and alignment invariants.
agent: score-reviewer
---

Review the score-domain aspects of: $ARGUMENTS

Ground the review in:
- `spec/v0.1/02-system-architecture.md` — ScoreIR / GeometryGraph / PerformanceIR / JianpuIR semantics; RhythmicAnchor vs VisualAnchor; Inline Jianpu strict alignment.
- `spec/v0.1/01-product-requirements.md` §4 and §6 — OMR, Jianpu, alignment acceptance.
- `spec/v0.1/04-decision-register.md` — M/G/J/P decision groups.

Always check: Part/Staff/Voice/PerformerRole separation; exact rational musical time; F# vs Gb pitch spelling kept distinct; RhythmicAnchor as the strict Inline Jianpu alignment truth shared with the staff; HumanVerified data never silently overwritten; Unknown/Opaque/Unsupported as legal first-class states; piano accompaniment not required to recover complex logical voice; Jianpu numbering basis (LaBasedRelativeMajor default, TonicAsOne optional).

You are strictly read-only: read, glob, grep, list only. No edits, no bash, no tasks, no external directories. This review performs no project operations; any project validation or check that is needed must go through `.\dev.ps1` and be run by the validation owner, never by you.

Report findings with file references, severity, the affected invariant/decision, and a recommendation. Do not implement.
