# Choirloom Development Workflow

| Field | Value |
|---|---|
| Status | **Live document** — operational workflow, NOT part of the v0.1 Frozen Baseline |
| Updated | 2026-08-12 |

This document describes how the repository is driven by the OpenCode +
oh-my-opencode-slim control plane and the project-level rules. It never
overrides the Frozen Baseline (`spec/v0.1/`); it references and operationalizes
it. Where anything below is a *target* rather than existing implementation, it
is explicitly marked **Not Implemented**.

---

## 1. Development Method

Schema-first + harness-first + vertical-slice (`spec/v0.1/03 §1`). Never work
"all modules 30%". Every stage has executable input, inspectable output, and
explicit exit conditions.

## 2. Control Plane Layout

| Path | Purpose |
|---|---|
| `AGENTS.md` | Root agent rules: source of truth, frozen invariants, git, model promotion |
| `.opencode/oh-my-opencode-slim.jsonc` | Project plugin config — incremental overrides (custom agents, permissions); user preset is inherited |
| `.opencode/oh-my-opencode-slim/orchestrator_append.md` | Orchestrator project rules (kickoff, routing, acceptance, ADR behavior) |
| `.opencode/commands/*.md` | Project slash commands (see §11) |
| `spec/development-workflow.md` | This document |

## 3. Source of Truth and the Frozen Baseline

`spec/v0.1/` is the Frozen Baseline (v0.1, frozen 2026-08-12): PRD/SRS
(`01-product-requirements.md`), system architecture (`02-system-architecture.md`,
including the Key Invariants list in §22), development plan (`03-development-plan.md`),
and the decision register (`04-decision-register.md`, decisions D/M/G/O/P/J/R/A/V).

Frozen specs are never silently re-interpreted or edited. Any change to a frozen
decision requires an ADR (see §9).

## 4. Operations Interface: `.\dev.ps1`

**Implemented** (pre-M0 control plane). `.\dev.ps1` is a thin entry point; all
logic lives in `tools/dev/Dev.psm1`. Windows PowerShell 5.1 compatible. The
repository has **no product build/test/harness/training pipeline yet**, so the
build/test/verify/model verbs accurately report **Not Implemented** and exit
non-zero — nothing fakes success.

Contract:

```text
.\dev.ps1 <verb> [args...]
```

| Verb | Status | Purpose |
|---|---|---|
| `doctor` | Implemented | Read-only environment/repo diagnostics (PowerShell, pwsh, git, repo layout; optional OhMy CLI and ssh reported but never failing; remote config validation and read-only remote probe when configured). Never writes files. |
| `build <scope>` | `NotImplemented` | Reports no build system exists yet; exits non-zero. |
| `test <scope>` | `NotImplemented` | Reports no test harness exists yet; exits non-zero. |
| `verify <scope>` | `NotImplemented` | Reports no verification harness exists yet; exits non-zero. |
| `remote setup` | Implemented (interactive) | Interactive wizard (entry gated by `-Confirm`, interactive-only): collects non-secret fields into `.dev/remote.local.json` (git-ignored); optional explicitly-confirmed local `ssh-keygen`; shows public key + suggested ssh config stanza but **never writes SSH config**, never copies/transfers a private key, never runs a remote probe. |
| `remote doctor` | Implemented | Read-only; requires `.dev/remote.local.json` + ssh; fixed safe probes only (hostname/uname/python/nvidia-smi/base directory). |
| `remote submit/status/logs/cancel/fetch-report/fetch-model` | `NotImplemented` | Stable CLI contract; executor-side runner protocol is future (§5). Exit non-zero with a prerequisite message. |
| `model validate/benchmark <path>` | `NotImplemented` | Stable CLI contract; path existence is checked, then reports no validator/benchmark harness; exits non-zero. |

All long-running jobs must be cancellable/resumable/stage-retryable per
`spec/v0.1/02 §14` (future, once real jobs exist). Agents and commands must
operate through `.\dev.ps1` and not bypass it with ad-hoc shell commands for
project operations.

## 5. Remote Runner CLI Contract

The CLI surface below is **stable**; the executor-side protocol it targets is
**Not Implemented** (reserved capability per `spec/v0.1/02 §17` and decision
A15). The control plane keeps these verbs stable and fails them cleanly until a
runner exists.

- The core contract must use `InputAsset / ContentIdentity / JobRequest`
  semantics — never `D:\path\score.pdf`-style absolute local paths. A Local
  Executor may read local assets; a Remote Executor is responsible for its own
  transfer.
- Remote execution is always an explicit user choice. Local failure or
  unsupported hardware only prompts the user; there is never a silent fallback
  and never silent upload (decisions A12, A15; invariant 9).
- CLI (stable surface; executor-side protocol future):

```text
.\dev.ps1 remote submit        [manifestPath]   # requires -Confirm; NotImplemented until runner exists
.\dev.ps1 remote status
.\dev.ps1 remote logs
.\dev.ps1 remote cancel                          # requires -Confirm; NotImplemented until runner exists
.\dev.ps1 remote fetch-report
.\dev.ps1 remote fetch-model   [outputPath]      # requires -Confirm; NotImplemented until runner exists
```

Results are returned as artifacts referenced by content identity; provenance is
retained. Remote setup is performed via `.\dev.ps1 remote setup` (interactive
wizard) or the `/remote-setup` command, with the explicit-user-opt-in rules in
§10.

## 6. Agent Permission Tiers

Defined in `.opencode/oh-my-opencode-slim.jsonc` (project custom agents) and the
user-level oh-my-opencode-slim preset (built-in agents).

| Tier | Agent | Write access | Tool profile |
|---|---|---|---|
| Read-only reviewer | `architect`, `score-reviewer`, `release-reviewer` | None (edit/bash/task/external_directory denied) | read, glob, grep, list allowed; no skills, no MCP |
| Writable specialist (prompt-constrained) | `ml-researcher` | Yes, only within ML duties (corpus, training, benchmark, ONNX, ModelPackage); no per-path permission rules so basic project edits are never accidentally denied | no MCP |
| Built-in team | `orchestrator`, `explorer`, `librarian`, `oracle`, `designer`, `fixer`, `observer`, `council` | Per built-in role (unchanged defaults) | Per user-level preset |

Review agents must never write; `ml-researcher` must never touch frozen spec
files, source-fact data, or HumanVerified data, and must obtain explicit user
confirmation before remote/network or training operations.

## 7. Experiment Manifest Fields

Every ML experiment record (created by `/ml-experiment`) must contain:

| Field | Meaning |
|---|---|
| `experiment_id` | Unique, dated ID |
| `hypothesis` | What is being tested and why |
| `corpus` | Dataset(s) + hashes (Synthetic Gold / Curated Real Gold / Adversarial) |
| `model` | Model id/version and ONNX/opset where applicable |
| `config` | Training/config hash and key parameters |
| `environment` | CPU/GPU/NPU, memory, EP |
| `metrics` | Fields F1, exact-measure, exact-system, corrections per page, review-time proxy, throughput |
| `license/status` | Model/data license and official/community/unverified status |
| `reproducibility` | Commands and data needed to rerun |

ModelPackage manifests must include the fields required by `spec/v0.1/02 §13.2`:
model ID/version, task/profile, API compatibility, ONNX/opset, input/output
specification, license, hash/signature, recommended hardware/EP, approximate
memory requirements, and capability declaration. ModelPackage is declarative
data only — no arbitrary code execution (decision A8; `spec/v0.1/01 §5.4`).

## 8. Model Promotion Workflow

Per `spec/v0.1/03 §2.2` and decisions A10/A25:

```text
Corpus / Synthetic generator
  → training
  → evaluation
  → ONNX export
  → ModelPackage (manifest / license / hash)
  → Windows local benchmark
  → application rollout
```

- Model updates are independent of app updates and never trigger silent re-OMR
  of existing projects.
- Reruns never silently overwrite HumanVerified data; correspondence migration
  is used, and uncertain cases go to a conflict queue (decision O7).
- Models are marked official/community/unverified; official packages are signed
  and hash-verified (decision A10).
- Training samples and corrections stay local by default (0 telemetry); export
  only on explicit user action (`spec/v0.1/01 §5.3`, decision O11).

## 9. ADR Policy (Frozen ADR behavior)

- Changing any frozen decision requires a new ADR under `spec/adr/`.
- An ADR records: the old decision, the reason for the change, migration
  impact, and the compatibility strategy.
- ADRs are authored against the frozen documents; the frozen documents
  themselves are never edited directly.
- Candidate ADRs are pre-listed in `spec/v0.1/04` ("关键 ADR 候选",
  ADR-001–ADR-012).
- Current state: `spec/adr/` is empty (contains only `.gitkeep`); no ADRs exist
  yet.

## 10. Remote Setup Rules

Remote capability is configured via `.\dev.ps1 remote setup` (interactive
wizard) or the `/remote-setup` command and must follow:

1. Explicit user opt-in before anything is enabled (decisions A12/A15).
2. No silent upload of source scores, project DB, corrections, or lyrics.
3. Core contract via InputAsset / ContentIdentity / JobRequest (see §5).
4. No arbitrary code execution from model packages or remote artifacts (A8).
5. Host settings live only in `.dev/remote.local.json` (repository-local,
   git-ignored, no secrets; private key material never appears in it and the
   private key never leaves the local machine).

## 11. Slash Commands

| Command | Agent | Purpose |
|---|---|---|
| `/project-start` | orchestrator | Kickoff/decomposition of milestone work; requires `.\dev.ps1` |
| `/arch-review` | architect | Read-only architecture review (invariants, decisions) |
| `/score-review` | score-reviewer | Read-only score-domain semantics review |
| `/ml-experiment` | ml-researcher | Scoped ML experiment with safety gates |
| `/release-review` | release-reviewer | Read-only release-readiness review (gates, scope, privacy) |
| `/verify` | orchestrator | Verification with named validation owner; checks via `.\dev.ps1` |
| `/remote-setup` | orchestrator | Remote runner/executor configuration with explicit opt-in |

## 12. M0 State (current)

The repository currently contains:

- `spec/v0.1/` — the Frozen Baseline;
- `dev.ps1` + `tools/dev/Dev.psm1` — the dev control plane (see §4): `doctor`
  implemented (read-only), `remote setup` interactive wizard implemented (local
  config only; optional user-confirmed local key generation; never writes SSH
  config), `remote doctor` implemented (read-only), all other remote/model
  verbs stable `NotImplemented`;
- `.dev/remote.example.json` — committed secret-free remote config template;
- `tools/dev/remote/README.md` — remote workflow documentation (English);
- `.opencode/` control-plane files listed in §2;
- `.gitignore`, `LICENSE`.

There is **no product code**, no CLI/harness, no CI, no schemas or model
packages, and no ADRs. Everything in §§5–8 described as a target is **Not
Implemented** and must not be reported as existing.

The next milestone is **M0 — Architecture & Corpus Foundation**
(`spec/v0.1/03 §3`): monorepo, CI, ScoreIR/GeometryGraph/JianpuIR schemas,
ModelPackage v0.1, project SQLite migration framework, stable ID/revision
primitives, CLI/harness skeleton, Synthetic Gold generator skeleton, Curated
Real Gold data-management spec, WinUI shell, and C++ Core ↔ Windows host
interop skeleton. Exit conditions are defined in `spec/v0.1/03 §3`.
