## Choirloom Project Rules (append to the built-in Orchestrator prompt)

These rules apply to every session in this repository. The built-in
oh-my-opencode-slim pantheon roles and their delegation rules remain in force
unchanged: `@explorer` for codebase reconnaissance, `@librarian` for external
knowledge/docs research, `@oracle` for architecture judgment and hard debugging,
`@designer` for ALL UI/UX work (layout, styling, visual hierarchy, responsive
behavior, animation, component feel), `@fixer` for scoped mechanical
implementation, `@observer` for visual/media analysis, and `@council` for
multi-model consensus.

### Kickoff decomposition

- Before starting substantive work, read the Frozen Baseline:
  `spec/v0.1/README.md`, `01-product-requirements.md`,
  `02-system-architecture.md`, `03-development-plan.md`,
  `04-decision-register.md`. Do not proceed on assumptions that contradict it.
- Decompose the request into milestones (M0–M7 per `spec/v0.1/03`) and vertical
  slices using the project method (schema-first + harness-first +
  vertical-slice, `spec/v0.1/03 §1`). Never work "all modules 30%".
- Build a short work graph before dispatching: lanes, dependency ordering, and a
  validation owner per lane. Prefer background dispatch for independent lanes
  and avoid overlapping write ownership.

### Domain routing

- Review work routes to the read-only custom agents:
  - `@architect` — architecture, schema/format boundaries, migration risk,
    checks against the v0.1 architecture invariants;
  - `@score-reviewer` — ScoreIR / GeometryGraph / PerformanceIR / JianpuIR
    semantics, alignment and HumanVerified invariants, OMR/lyrics data;
  - `@release-reviewer` — v1 scope contract, M0–M7 release gates,
    privacy/security/offline compliance.
  Never route review work to writer lanes, and never let reviewers write.
- ML work routes to `@ml-researcher` (corpus, training, benchmark, ONNX export,
  ModelPackage). It is writable only within its ML duties and must obtain
  explicit user confirmation before remote/network or training operations.
- Keep the built-in routing intact: recon → `@explorer`; external
  docs/research → `@librarian`; hard debugging/architecture → `@oracle`; any
  UI/UX → `@designer` (never implement design directly); scoped implementation
  → `@fixer`.

### Council limits

- `@council` is the highest-cost path in the system; automatic delegation to it
  stays strict. Use it only when the user explicitly asks for
  consensus/multiple opinions or when a critical decision genuinely requires
  cross-model perspectives. Do not route routine review or single-specialist
  work to `@council`.

### Acceptance verification

- Every delegation names a validation owner and an allowed scope.
- Verify against: `spec/v0.1/01 §6` acceptance standards, `spec/v0.1/03 §13`
  Definition of Done, and the milestone exit conditions in `spec/v0.1/03 §3–§10`.
- Run project validation through `.\dev.ps1` (target contract,
  `spec/development-workflow.md §4`); do not substitute ad-hoc shell commands
  for project checks.
- Reuse still-valid evidence; do not re-verify unchanged states.

### Frozen ADR behavior

- `spec/v0.1/` is the Frozen Baseline. Implementation must never silently change
  its meaning.
- If a frozen decision must change, create an ADR under `spec/adr/` recording
  the old decision, the change reason, migration impact, and compatibility
  strategy, and update references; never edit the frozen documents directly.
  Candidate ADRs are pre-listed in `spec/v0.1/04` ("关键 ADR 候选",
  ADR-001–ADR-012).
