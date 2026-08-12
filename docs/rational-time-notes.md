# RationalTime - M0-001 implementation note (DRAFT, NON-FROZEN)

- Status: **Draft / non-frozen**. This is the first portable-core vertical
  slice (`core/score/rational_time.{h,cpp}` plus its tests, fixtures and
  draft wire schemas). It is **not** part of the Frozen Baseline
  (`spec/v0.1/`) and does not modify it. It is also **not** the full M0
  schema migration gate: adopting this representation into the ScoreIR
  schema is a separate M0/M1 deliverable.
- Lane owner: fixer (implementation + harness/control-plane checks). Project
  control-plane validation is owned by the orchestrator.

## 1. Why this primitive exists

The Frozen Baseline mandates exact rational musical time:

- Decision **M1**: 音乐时间使用精确有理数 (musical time uses exact rationals).
- `spec/v0.1/02 S4.5` (ScoreIR time model): exact duration, tuplet ratio,
  same-onset relationship.
- `spec/v0.1/02 S8.3`: MusicalTime is exact rational; PlaybackTime (seconds)
  is derived and separate.

`RationalTime` is the shared primitive for those requirements: immutable,
fully-reduced `n/d` with `d > 0`, canonical zero `0/1`, whole note == 1
(a quarter note is `1/4`).

## 2. Value domain and field-level policy (exact)

- `RationalTime` is the **arithmetic value domain**: it is signed, and all
  negative mathematical values are supported and exact.
- Field-level constraints are **not** enforced by this primitive; they belong
  to the ScoreIR event/measure layer. When that layer lands, it must hold:
  - **measure-relative offsets are non-negative** (`>= 0`);
  - **ordinary time-consuming durations are positive** (`> 0`);
  - **grace / non-consuming event semantics must not be inferred from zero**:
    a zero or non-consuming duration is an explicit notational fact, never a
    guess derived from the value alone;
  - a **weak pickup is an incomplete measure**: its real (partial) duration is
    preserved, never fabricated into a full measure;
  - **PlaybackTime is separate**: MusicalTime stays exact rational; seconds /
    samples are derived (tempo realization, fermata, rit.) in the playback
    layer.

## 3. Semantics of the primitive

- **Normalization**: every constructor/operator returns a reduced fraction
  with a strictly positive denominator; `gcd(|n|, d) == 1`; zero is `0/1`.
- **Zero**: canonical form is `0/1`; `-0` does not exist as a distinct value.
- **Ordering**: exact total ordering via an overflow-free magnitude-based
  Euclidean comparison (no int64 cross-multiplication), so comparisons never
  overflow at any magnitude.
- **Arithmetic**: checked and exact. add/subtract use exact two-limb
  (uint64-pair) intermediates with factor-cancellation reduction, so a
  representable reduced result is never rejected because an unreduced
  intermediate or lcm overflowed int64. multiply/divide use cross-cancellation
  in unsigned magnitudes with signed reconstruction (no narrowing of uint64
  gcds, including `gcd(int64_min, int64_min) == 2^63`).
- **Errors** (exceptions; no wrap, no saturation, no floating-point fallback):
  - zero denominator on construction, division by zero, reciprocal of zero
    -> `std::domain_error`;
  - any int64 representability overflow in construction, unary minus,
    reciprocal, or binary arithmetic -> `std::overflow_error`.
- `to_double()` exists only for display/export and is **never** treated as
  source truth (frozen invariant: exact rational numbers, no float truth).

## 4. Musical tuplets (test focus)

Tuplet durations are ratios applied to base durations, e.g. a triplet ratio
is `2/3`, a duplet ratio `3/2`. Unit tests assert, exactly (no floating
point):

- `1/8 * 2/3 == 1/12` (triplet eighth);
- three triplet eighths sum to exactly `1/4` (the beat);
- two triplet quarters equal `1/3`;
- nested exact tuplet arithmetic: `1/8 * (2/3) * (2/3) == 1/18`;
- quintuplet `4/5`, septuplet `4/7`, duplet `3/2` (two such duplets equal
  `3/8`, a dotted quarter), undo-by-ratio, mixed tuplet + ordinary note sums.

## 5. Canonical JSON serialization (draft wire format)

`RationalTime` serializes to a JSON object with **decimal-string**
`numerator` / `denominator` (never JSON numbers - avoids JS/int64 precision
loss). Whole note == 1.

```json
{ "numerator": "1", "denominator": "4" }
```

- `numerator`: reduced numerator, decimal string, sign carried here
  (canonical zero is `"0"`; `"-0"` is not canonical; no `+`; no leading zeros).
- `denominator`: reduced positive decimal string (`"1"` for whole values;
  `0` and negative values are rejected).
- The fraction must be fully reduced (zero requires denominator `1`);
  non-reduced input is rejected, never silently normalized.

**Two-level validation (declared accurately):**

1. **Syntactic** level: the JSON Schema files enforce the canonical
   decimal-string grammar via `pattern` regexes. JSON Schema regexes cannot
   bound the numeric range, so the patterns intentionally express only the
   grammar (they would also accept out-of-range digit strings).
2. **Semantic** level: the core
   (`RationalTime::from_canonical_json`) establishes the signed/positive
   int64 range, positivity of the denominator, and the reduced-canonical
   invariants. `to_canonical_json` / `from_canonical_json` are exact inverses
   for every representable value.

Files:

- `schemas/score/rational-time.schema.json` - strict per-value RationalTime
  schema (version `0.1.0`, `$id choirloom:score/rational-time/0.1.0`).
- `schemas/score/rational-time-collection.schema.json` - versioned envelope
  schema for the golden fixture; each case requires `name` and `value`, allows
  an optional `note`, and references the value schema by its `$id`.
- `tests/golden/rational_time.samples.json` - golden fixture conforming to
  the collection envelope; every `value` is a strict RationalTime object.

## 6. Schema versioning / migration boundary

- This wire representation and both schemas are **draft proposals only**.
  They are not frozen and do not claim a migration gate.
- Schema evolution requires **explicit versioned migrations** (a versioned
  schema namespace; additive changes bump the minor version and stay
  backward-readable; breaking changes bump the major version and require an
  explicit migration path and compatibility strategy).
- An **ADR is required only when a frozen decision changes**
  (`spec/development-workflow.md S9`); the frozen documents themselves are
  never edited directly. Evolving this non-frozen draft does not, by itself,
  require an ADR.

## 7. Tests

The unit test executable (`tests/unit/rational_time_tests.cpp`) now:

- reads the golden fixture and both schema files from disk (paths resolved via
  the `CHOIRLOOM_TEST_SOURCE_DIR` compile definition in
  `tests/unit/CMakeLists.txt`);
- structurally validates the collection envelope and each RationalTime value
  against the schema contract (shape, required fields, schema `pattern`
  regexes, and the semantic checks enforced by `from_canonical_json`);
- deserializes each value via `RationalTime::from_canonical_json`, reserializes
  via `to_canonical_json`, and compares the exact canonical JSON;
- covers the JSON API (extremes, whitespace/field order, malformed input,
  unknown/missing/duplicate/non-string fields, invalid decimals, `-0`,
  zero/negative denominator, non-reduced input, out-of-int64 wire values);
- covers arithmetic regressions for final-representable overflow cases
  (`1/6000000006 + 1/6000000009 == 1333333335/4000000010000000006`,
  `kMax/3 + (kMax-2)/3 == 6148914691236517204` (unreduced numerator exceeds
  int64 while the reduced result fits), `int64_min / int64_min == 1`).

## 8. Build / validation status (scoped harness; M0-002/M0-003)

- The project control plane (`dev.ps1`) now implements **scoped harness
  commands** for this slice (the M0-002 `rational-time` scope; the sibling
  M0-003 `entity-revision` scope is documented in `docs/entity-revision-notes.md`):
  - `dev.ps1 build rational-time` — configure + build the RationalTime test
    target (CMake/CTest only);
  - `dev.ps1 test rational-time` — configure + build first, then run exactly
    the `rational_time_tests` CTest test with output-on-failure (a zero-match
    fails);
  - `dev.ps1 verify rational-time` — build + test gate; on success it reports
    scoped language such as "M0-001 RationalTime slice verification passed"
    and states that the full M0 milestone gate remains open. It also runs the
    control-plane dispatch checks (`tests/dev/dispatch.tests.ps1`,
    negative-path mode: missing/unknown/extra scope arguments must exit
    non-zero) and reports their check count; any dispatch failure fails the
    verify gate.
  - A missing or unknown scope, and every other verb/scope, exit non-zero
    with usage/prerequisite text. There is no default scope and no
    all/core/scoreir aliases.
- Prerequisites: `cmake` (>= 3.20) and `ctest` on PATH. On Windows, `dev.ps1`
  initializes the **already-installed** Visual Studio Build Tools MSVC C++
  environment itself when its build cache selects MSVC but the process lacks
  the MSVC standard-library environment (vcvars64.bat / VsDevCmd.bat
  discovery; no manual Developer PowerShell launch required). A fresh
  configure deterministically selects that local MSVC toolchain when
  installed; an existing non-MSVC cache is left unchanged. Ninja is used when
  available. No downloads, installs, remote, network, or model use.
- The build directory is the deterministic, git-ignored
  `build/dev/rational-time`. CMake/CTest is the **current harness toolchain**
  for this slice; it is no longer described merely as a local diagnostic.
- This is **not** a full product build/test pipeline and no full M0 harness,
  ScoreIR fixture/schema harness, CI, migration/revision, or WinUI gates are
  claimed. `dev.ps1` implements exactly the harness scopes `rational-time`
  (M0-002) and `entity-revision` (M0-003); all other scopes and verbs remain
  `NotImplemented`.

## 9. Scope guardrails for this slice

This slice deliberately contains **no** Measure / Note / Voice / tuplet
objects, no MusicXML, OMR, UI, C ABI, or SQLite code. Those are later lanes
and must not be smuggled into this slice.
