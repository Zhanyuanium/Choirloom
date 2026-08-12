# EntityId / Revision primitives - M0-003 implementation note (DRAFT, NON-FROZEN)

- Status: **Draft / non-frozen**. This is the M0-003 vertical slice
  (`core/score/entity_revision.{h,cpp}` plus tests, golden fixture, draft wire
  schemas, and the scoped `entity-revision` harness commands). It is **not**
  part of the Frozen Baseline (`spec/v0.1/`) and does not modify it. No
  ScoreIR skeleton (Score/Part/Measure/Event...), SQLite, MusicXML/OMR, UI,
  or CI is introduced by this slice.
- Lane owner: fixer (implementation + harness/control-plane checks). Project
  control-plane validation is owned by the orchestrator.

## 1. Identity contract (exact)

- `EntityId` and `RevisionId` are **distinct, nominal, opaque 128-bit values**.
  The canonical wire form is the lower-case UUIDv4-compatible string
  `xxxxxxxx-xxxx-4xxx-[89ab]xxx-xxxxxxxxxxxx`; nil/all-zero values are invalid.
- **Persistent identity only**: there is no ordering implication (neither type
  defines ordering operators), no content hash, no container index/address/
  rowid, and no MusicXML/OMR/WinUI coupling.
- The core **never silently generates random IDs**. Values are constructed from
  validated 16 bytes (`from_bytes`) or from the canonical string parser
  (`from_string`); a generator helper is deliberately NOT provided. Identity
  allocation, uniqueness, and collision rejection are the **repository
  boundary's** responsibility, and the bytes must not encode user/device/path/
  source content. No crypto guarantees are claimed or implied.
- `RevisionId` is equally opaque and non-ordering. `EntityId` and `RevisionId`
  are intentionally non-interchangeable at compile time (distinct types; no
  implicit conversion; no cross-type equality).
- Serialize/deserialize must preserve exact identity: `from_string(x)` then
  `to_string()` returns exactly `x`, and `from_bytes(b)` then `bytes()` returns
  exactly `b`.

## 2. Entity vs revision identity

- A normal edit changes the project **revision** while preserving existing
  **entity IDs**.
- Reflow / cache rebuild / serialize / reopen preserve identity and create **no**
  new revision.
- `EntityId` is **generic persistent identity**. ScoreIR and GeometryGraph each
  own **separate identity spaces**; the relation between them is explicit, and
  equal IDs never imply cross-layer correspondence.
- `EntityId`/`RevisionId` are NOT `RationalTime` values or measure offsets. The
  separation from `RationalTime` is **compile-time tested** (neither type
  constructs, converts to, or compares with it); the separation from
  `RhythmicAnchor`/`VisualAnchor` is a **documented contract**, because those
  anchor types do not yet exist in this repository.
- A new insertion, business clone, derived-layer object, or new re-recognition
  candidate gets a **new EntityId**.
- **Re-recognition policy**: a candidate from re-recognition gets a **fresh ID
  pending resolution**. An accepted unambiguous one-to-one correspondence
  resumes/preserves the prior logical EntityId; new/split/merge/uncertain cases
  retain explicit new IDs with lineage/conflict handling. There is **no silent
  HumanVerified overwrite**. The correspondence/lineage solver itself stays out
  of scope.
- Correspondence between a new revision/candidate and a prior entity is a
  **future explicit relationship**: it may relate a new revision/candidate to a
  prior entity, but it never treats ID equality as correspondence and never
  silently overwrites HumanVerified data.
- No entity taxonomy and no source/derived implementations exist in this slice.

## 3. Persistence / migration

- `EntityId`, `RevisionId`, and future correspondence are **durable project
  facts**: they survive migration, cache deletion, and reopen, and are **never
  regenerated**.
- A legacy migration needs a **deterministic recorded mapping** (old -> new
  identities), not ad-hoc regeneration.
- Persistence in SQLite remains out of scope for this slice.

## 3. ProjectRevisionMetadata (minimum snapshot metadata)

Only four fields:

- `revisionId` (required);
- `parentRevisionId` (optional; must not equal `revisionId` - self-parent is
  rejected);
- `origin`: narrow `RevisionOrigin` enum `{ManualEdit, Recognition, Migration}`
  (wire strings are the enum spellings exactly; unknown origins are rejected);
- `summary`: non-empty human-readable text (whitespace-only is rejected).

This is a **minimum project-snapshot metadata primitive**, NOT a correction
log, undo engine, recognition solver, provenance store, or HumanVerified
protection.

## 4. compare_project_revisions

`compare_project_revisions(a, b)` is a standalone, deterministic helper that
compares **only** `RevisionId`/metadata fields in this defined order:

1. `revisionId` (defined byte order - used only for deterministic comparison,
   it does not give RevisionId an ordering semantic elsewhere);
2. `parentRevisionId` (absent < present; then defined byte order);
3. `origin` (enum declaration order: ManualEdit < Recognition < Migration);
4. `summary` (lexicographic).

It does **NOT** perform ScoreIR revision comparison.

## 5. Wire validation (two levels, declared accurately)

- **Syntactic**: the draft JSON Schemas enforce canonical grammar via
  `pattern`/`enum`/`required`. JSON Schema regexes cannot enforce the exact
  16-byte non-nil/v4 semantics, so the patterns express the grammar only.
- **Semantic**: the core enforces non-nil, exact 16-byte identity, version (4)
  and variant (8,9,a,b) nibbles, self-parent rejection, whitespace-only summary
  rejection, and unknown-origin rejection. `from_string` rejects uppercase,
  braces, missing/misplaced hyphens, wrong version/variant, nil, truncation and
  extra bytes.
- **Exact JSON inverse**: `from_json(to_json(m)) == m` holds for every
  accepted summary. `to_json` escapes quotes, backslash, named control
  escapes, and other control bytes as `\u00XX`; `from_json` decodes all of
  them (including `\uXXXX` and UTF-16 surrogate pairs) with no silent lossy
  conversion.

Schema files (all draft, non-frozen, version `0.1.0`):

- `schemas/score/entity-id.schema.json` (`$id choirloom:score/entity-id/0.1.0`);
- `schemas/score/revision-id.schema.json` (`$id choirloom:score/revision-id/0.1.0`);
- `schemas/score/project-revision-metadata.schema.json`
  (`$id choirloom:score/project-revision-metadata/0.1.0`);
- `schemas/score/entity-revision-collection.schema.json`
  (`$id choirloom:score/entity-revision-collection/0.1.0`) - envelope for the
  golden fixture, with if/then per-case value typing by `kind`.

Fixture: `tests/golden/entity_revision.samples.json` (entity ids, revision ids,
metadata with and without parent, all three origins). Uniqueness contract:
EntityId samples are unique and disjoint from the RevisionId space; RevisionId
values legitimately repeat in metadata references.

## 6. Schema versioning / migration boundary

- These wire representations are **draft proposals only**; they are not frozen
  and claim no migration gate.
- Schema evolution requires **explicit versioned migrations** (versioned schema
  namespace; additive changes bump the minor version and stay
  backward-readable; breaking changes bump the major version and require an
  explicit migration path and compatibility strategy).
- An **ADR is required only when a frozen decision changes**
  (`spec/development-workflow.md S9`); evolving this non-frozen draft does not,
  by itself, require an ADR.

## 7. Tests

`tests/unit/entity_revision_tests.cpp` (separate CTest test `entity_revision_tests`):

- compile-time nominal distinctness / non-interchangeability / absence of
  ordering and cross-type operators, and constexpr wire round-trips;
- deterministic wire round-trips (exact canonical identity preservation);
- constructed-byte uniqueness;
- equality / hash;
- strict parse rejection (case, braces, hyphens, version/variant, nil,
  truncation/extra bytes, byte-level validation);
- metadata values validation (self-parent, empty/whitespace summary, unknown
  origin) and canonical JSON encode/decode;
- `compare_project_revisions` determinism and defined order;
- compile-time proof that EntityId/RevisionId neither construct, convert to,
  nor compare with RationalTime (identity is not a time/anchor value;
  separation from the not-yet-existing `RhythmicAnchor`/`VisualAnchor` types
  is a documented contract rather than a compile-time check);
- a minimal local durable reopen test: serialize canonical EntityId + metadata
  JSON to a temporary test file, destroy/replace the in-memory values,
  reopen/read/parse, and prove exact equality (proves primitive durable
  serialization/reopen only - NOT SQLite migration or crash-safe
  transactions), including malformed/truncated on-disk failure;
- golden fixture parse / validate / deserialize / canonical reserialize /
  exact compare, plus canonical uniqueness (entity ids unique and disjoint
  from the revision id space; distinct revision id set equals the fixture's
  intended set while metadata references reuse them by design).

## 8. Harness (M0-003 scope)

`dev.ps1 build|test|verify entity-revision` mirrors the M0-002 `rational-time`
harness: exactly one scope argument; deterministic git-ignored build directory
`build/dev/entity-revision`; builds only the `entity_revision_tests` target;
CTest runs exactly one matching test (`^entity_revision_tests$`; zero-match or
multiple-match fails); `verify` runs the build + test gate, then the
non-recursive control-plane dispatch checks, and on success emits scoped
language such as "M0-003 EntityId/Revision primitive verification passed" while
stating the full M0 milestone gate remains open. See
`spec/development-workflow.md S4`.

## 9. Build / validation status

- Implemented harness scopes: `rational-time` (M0-001/M0-002) and
  `entity-revision` (M0-003). All other verbs/scopes report
  `NotImplemented:` or a prerequisite message and exit non-zero.
- Prerequisites: `cmake` (>= 3.20) and `ctest` on PATH. On Windows, `dev.ps1`
  initializes the **already-installed** Visual Studio Build Tools MSVC C++
  environment itself when its build cache selects MSVC but the process lacks
  the MSVC standard-library environment (vcvars64.bat / VsDevCmd.bat
  discovery; no manual Developer PowerShell launch required). A fresh
  configure deterministically selects that local MSVC toolchain when
  installed; an existing non-MSVC cache is left unchanged. Ninja used when
  available. No downloads, installs, remote, network, or model use. Works from
  any working directory (repo root resolved from the module location).
- Open M0 gates: full M0 harness, ScoreIR fixture/schema harness, CI,
  project SQLite migration/revision primitives, and WinUI host gates.

## 10. Scope guardrails

This slice contains no ScoreIR entity taxonomy, no source/derived
implementations, no correction/undo/provenance/HumanVerified machinery, no
MusicXML/OMR/UI/C-ABI/SQLite/CI/ML code, and no random-ID generation.
