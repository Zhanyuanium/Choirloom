# Choirloom — Agent Rules

## Source of Truth

- `/spec` is the single source of truth. `spec/v0.1/` is the **Frozen Baseline**
  (v0.1, frozen 2026-08-12). It is the common baseline for implementation,
  review, testing, model training, and version migration.
- Frozen specs must never be silently re-interpreted or edited in code. Any
  change to a frozen decision requires a new ADR under `spec/adr/`
  (`spec/development-workflow.md §9`).
- Spec set:
  - `spec/v0.1/README.md` — index, terminology, keyword semantics (MUST/SHOULD/MAY)
  - `spec/v0.1/01-product-requirements.md` — PRD/SRS, v1 scope, acceptance standards
  - `spec/v0.1/02-system-architecture.md` — architecture, layers, key invariants (§22)
  - `spec/v0.1/03-development-plan.md` — M0–M7 roadmap, harness, Definition of Done (§13)
  - `spec/v0.1/04-decision-register.md` — frozen decisions D/M/G/O/P/J/R/A/V
- Workflow and control plane: `spec/development-workflow.md`,
  `.opencode/oh-my-opencode-slim.jsonc`,
  `.opencode/oh-my-opencode-slim/orchestrator_append.md`,
  `.opencode/commands/`.

## Frozen Invariants (never silently broken)

1. **Source vs derived**: ScoreIR is the source of musical fact; GeometryGraph,
   PerformanceIR, JianpuIR are derived. User transforms (PitchTransform /
   TempoTransform / VocalizationProfile / PronunciationProfile) never rewrite
   the Source Score.
2. **Four layers separate**: ScoreIR / GeometryGraph / PerformanceIR / JianpuIR.
   No layer is another layer's hidden private truth.
3. **No external tech as core truth**: MusicXML, Sparks, WinUI, or any model
   must not become the core data model.
4. **Inline Jianpu alignment** is strictly `RhythmicAnchor`-based and shared
   with the staff.
5. **Resize/reflow** never changes ScoreIR event identity or RhythmicAnchor
   relations.
6. **HumanVerified** data is never silently overwritten by model reruns; reruns
   produce suggestions/conflicts only.
7. **Piano accompaniment** is not required to recover complex logical voice /
   cross-staff semantics.
8. **ModelPackage** is declarative data only; no arbitrary code execution from
   model repositories.
9. **Offline-first**: fully offline after models are installed; remote
   execution is explicit user opt-in and never a silent fallback or upload.
10. **Unknown / Opaque / Unsupported** are legal first-class states; never force
    a "most-likely" label.
11. **Caches are disposable**; project facts (ScoreIR / GeometryGraph /
    provenance) survive cache deletion.
12. **Source View ↔ Responsive View** locate bidirectionally via GeometryGraph.

## Workflow

- Method: schema-first, harness-first, vertical-slice (`spec/v0.1/03 §1`).
- Done means: Definition of Done per `spec/v0.1/03 §13`; milestone gates M0–M7
  per `§15`; acceptance standards per `spec/v0.1/01 §6`.
- Every task names a validation owner (default: orchestrator). Run project
  operations through `.\dev.ps1` (target contract — created during M0, see
  `spec/development-workflow.md §4`); do not bypass it with ad-hoc commands.

## Git

- Conventional Commits. Commit only when explicitly asked.
- No force-push, no amending published commits, no hook bypasses.
- Never commit secrets, model weights, large corpus files, or user source
  scores. Code: Apache-2.0; models/data licensed and disclosed separately.

## Remote Training & Model Promotion

- Training samples and corrections stay local by default (0 telemetry); export
  only on explicit user action.
- Never upload source scores, project DB, lyrics, or corrections without
  explicit user consent.
- Promotion path: corpus → training → evaluation → ONNX export → ModelPackage
  (manifest/license/hash) → Windows local benchmark → app rollout.
- Mark models official/community/unverified; official packages are signed and
  hash-verified.
- Model updates are independent of app updates and never trigger silent re-OMR
  of existing projects.
