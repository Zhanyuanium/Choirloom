# Schema / Versioning Foundation - M0-004 implementation note (DRAFT, NON-FROZEN)

- Status: **Draft / non-frozen**. This is the M0-004 vertical slice
  (`core/score/schema_foundation.{h,cpp}` plus tests, golden fixture, the
  source-of-truth catalog, and the scoped `schema-foundation` harness
  commands). It is **not** part of the Frozen Baseline (`spec/v0.1/`) and does
  not modify it. No ScoreIR/GeometryGraph/JianpuIR/PerformanceIR models, no
  SQLite migration engine, no C ABI, no UI/ML/model/remote/network, no app
  updater, and no Git provenance are introduced.
- Lane owner: fixer (implementation + harness/control-plane checks). Project
  control-plane validation is owned by the orchestrator. No musical semantic
  expression exists in this slice (score-reviewer was intentionally not used).

## 1. Nominal concepts (never interchangeable)

- `SchemaId`: a stable format-family identity WITHOUT a version, canonical
  `choirloom:score/<kebab-name>`. It is neither a file path nor a complete
  `$id`.
- `SchemaVersion`: strict canonical `major.minor.patch` with non-negative
  uint32-like components, no leading zeros (except `0`), exactly three
  segments, and no prerelease/build metadata. The parser is strict; there is
  no silent normalization. Grammar violations throw `std::invalid_argument`;
  a component exceeding the uint32 range throws `std::out_of_range` (and maps
  to the `schema-version-out-of-range` diagnostic in catalog/raw-version
  inspection). Version comparison is component-wise and is meaningful ONLY
  within the same `SchemaId`.
- `SchemaDocumentId`: the complete existing JSON Schema `$id`, canonical
  `<SchemaId>/<SchemaVersion>`. Parsing/roundtrip validates that relation (a
  valid `SchemaId` plus a valid `SchemaVersion`); on-disk `$id` values must
  include the version.
- `ProjectRevision`/`RevisionId`, `ModelVersion`, `ApplicationVersion`, Git
  SHA, and the SQLite store-schema version are **separate concepts** and MUST
  NOT enter these types or the catalog as substitutes. This is enforced at
  compile time for `RevisionId` (no construct/convert/compare) and documented
  for the others.

## 2. Core behavior

- Portable C++20, no external library/platform dependency. Immutable types;
  canonical formatting/parsing; no random generation.
- `ValidationDiagnostic`: structured result with a stable `DiagnosticCode`
  (stable string via `to_string`), the relevant `SchemaId`/`SchemaVersion`
  where known, a repo-relative logical document path, and a human message.
  Validation APIs return diagnostic collections, NOT only booleans.
  Construction parsing may throw `std::invalid_argument` (or
  `std::out_of_range` for overflowing version components); catalog
  inspection/compatibility uses structured diagnostics.
- `SchemaCatalog` is an immutable declarative registry: exact
  `(SchemaId, SchemaVersion)` lookup ONLY (no latest/nearest fallback) and
  deterministic iteration/order (entries sorted by schemaId then version;
  rules sorted by from/to/kind). Entries carry `schemaId`, `schemaVersion`,
  `documentId`, a repo-relative path, and `status {Draft, Stable, Deprecated}`.
  The catalog itself has a separate `catalogFormatVersion` (it is not an entry
  `SchemaVersion`).
- Validation covers duplicate (tuple, documentId, path), relative-path
  traversal and absolute paths, missing schema files, `$id` mismatch, and
  `version` mismatch. Local `$ref` resolution is allowed ONLY for registered
  entries or the declared JSON Schema dialect identifier
  (`https://json-schema.org/draft/2020-12/schema`). No network, file watcher,
  plugin, or script execution. `validate_catalog_files()` enforces repo-root
  containment BEFORE any file I/O, even when called directly without a prior
  `validate_catalog`: lexical rejects (absolute/backslash/traversal) come
  first, then the repo root and target path are canonicalized
  (`weakly_canonical`, resolving symlinks/junctions) and containment is
  compared by path COMPONENTS (not string prefix), rejecting any target that
  resolves outside the root.
- Rule validation rejects duplicate and conflicting same-direction rules and
  cross-SchemaId edges; `schema_compatibility` and `plan_migration` gate on
  catalog structural validity, so invalid/unvalidated rules never produce a
  successful compatibility or planning result.
- `inspect_schema_version(catalog, schemaId, optional raw version string)`
  performs structured raw-version inspection without forcing callers to parse
  first, yielding exact stable codes: `missing-schema-version`,
  `malformed-schema-version`, `schema-version-out-of-range`, `unknown-schema`,
  `unknown-schema-version`, `unsupported-future-version`. A future version is
  `unsupported-future-version`, never malformed.
- Diagnostics are distinct and stable: unknown schema -> `unknown-schema`;
  registered schema with unregistered (non-future) version ->
  `unknown-schema-version`; a version newer than any registered for the schema
  -> `unsupported-future-version` (NOT malformed); missing/malformed inputs ->
  their own distinct codes.

## 3. Compatibility (explicit edges only; no semver inference)

- Compatibility distinguishes at least: `Exact`, `Readable`,
  `RequiresMigration`, `Unsupported`.
- It is NEVER inferred from semver. `Readable` (additive, non-breaking) and
  `RequiresMigration` occur ONLY when the catalog declares an explicit
  directional rule covering the exact `(from, to)` pair. No declared edge ->
  `Unsupported` with a structured diagnostic. A future/unknown version is
  reported via its own diagnostics (value `Unsupported`).
- Down-migration is unsupported without an explicit edge.

## 4. Migration planning (contract only)

- `MigrationPlan` carries source/target schema identities and an ordered list
  of declared migration edges. `plan_migration` is deterministic, performs NO
  execution or mutation, and returns structured diagnostics when the path is
  missing or the direction unsupported.
- There is NO SQLite/project migration engine and NO historical schema
  conversion in this slice.

## 5. Source-of-truth catalog

- `schemas/schema-catalog.json` is the single declarative catalog INSTANCE
  (`catalogFormatVersion: 1`). It is an instance, not a schema document: its
  `$schema` binds it to the catalog schema document
  (`choirloom:score/schema-catalog/0.1.0`) and it deliberately carries no
  `$id` of its own (no document-identity conflict with
  `schema-catalog.schema.json`). It registers ALL SIX pre-existing schemas
  (rational-time, rational-time-collection, entity-id, revision-id,
  project-revision-metadata, entity-revision-collection) plus the M0-004
  schemas (schema-version, schema-id, schema-document-id, schema-catalog,
  schema-foundation-collection) and the M0-005 schemas (scoreir,
  scoreir-collection). Paths are repo-relative and source-tree only.
  The `rules` array is currently empty (all registered versions are 0.1.0);
  compatibility/migration examples live in the golden fixture and tests via
  synthetic catalogs.
- The catalog parser is strict: the instance `$schema` must be present, be a
  string, and equal exactly `choirloom:score/schema-catalog/0.1.0`; duplicate
  JSON fields, unknown top-level/entry/rule fields, wrong types, a
  missing/malformed/overflowing `catalogFormatVersion`, and an unsupported
  catalog format all reject the catalog (no catalog is returned) with
  structured diagnostics (`malformed-catalog-json`,
  `catalog-format-unsupported`, `schema-id-format`, `schema-version-format`,
  `schema-version-out-of-range`, `schema-document-id-format`). A
  `SchemaDocumentId` version overflow in an entry OR a rule maps to
  `schema-version-out-of-range` (never an uncaught exception).

## 6. Wire schemas (draft, versioned, strict)

- `schemas/score/schema-version.schema.json`
  (`$id choirloom:score/schema-version/0.1.0`);
- `schemas/score/schema-id.schema.json`
  (`$id choirloom:score/schema-id/0.1.0`);
- `schemas/score/schema-document-id.schema.json`
  (`$id choirloom:score/schema-document-id/0.1.0`);
- `schemas/score/schema-catalog.schema.json`
  (`$id choirloom:score/schema-catalog/0.1.0`);
- `schemas/score/schema-foundation-collection.schema.json`
  (`$id choirloom:score/schema-foundation-collection/0.1.0`) - envelope for the
  golden fixture; the `kind` field is a fixed enum and every kind has a
  constrained value shape (if/then).

Two-level validation is declared accurately: the schemas enforce syntactic
grammar (`pattern`/`enum`/`required`, `additionalProperties: false`); the core
enforces semantics (uint32 range, relation validity, duplicates, traversal,
on-disk `$id`/`version` matching, `$ref` resolution).

## 7. Schema versioning / migration boundary

- All M0-004 wire forms are draft proposals; they are not frozen and claim no
  migration gate.
- Schema evolution requires **explicit versioned migrations** (a versioned
  schema namespace; additive changes bump the minor version and stay
  backward-readable only when a catalog rule says so; breaking changes bump
  the major version and require an explicit migration edge).
- An **ADR is required only when a frozen decision changes**
  (`spec/development-workflow.md S9`); evolving this non-frozen draft does not,
  by itself, require an ADR.

## 8. Tests / harness

- `tests/unit/schema_foundation_tests.cpp` (separate CTest test
  `schema_foundation_tests`): strict parsing/ordering and exact overflow
  taxonomy (`std::out_of_range`); compile-time nominal separation from
  `RevisionId`/`EntityId`/`RationalTime`; catalog instance/schema identity
  binding; real catalog loading + deterministic iteration + structural and
  file-backed validation (six pre-existing schemas registered; on-disk
  `$id`/`version` match; local `$ref` resolution); strict catalog parser
  rejections (duplicate/unknown fields, wrong types, unsupported/missing
  format, overflow -> `schema-version-out-of-range`); structured diagnostics
  for duplicates/traversal/absolute/missing/on-disk mismatch/unknown/future;
  direct `validate_catalog_files` containment (no prior `validate_catalog`);
  rule validation (duplicate/conflicting/cross-schema) with compatibility and
  planning gated on catalog validity; `inspect_schema_version` exact stable
  codes; explicit compatibility and deterministic migration planning; and the
  golden fixture applied end to end (every case kind validated against the
  collection-schema enum and executed - unknown or skipped kinds fail).
- Harness scope: `dev.ps1 build|test|verify schema-foundation`
  (build dir `build/dev/schema-foundation`, target `schema_foundation_tests`,
  CTest regex `^schema_foundation_tests$`, exactly-one guard). `verify` runs
  the build + test gate, then the non-recursive control-plane dispatch
  checks, and on success emits scoped language such as "M0-004 Schema
  foundation verification passed" while stating the full M0 milestone gate
  remains open. `rational-time` and `entity-revision` scopes are unaffected.
- Prerequisites/toolchain readiness: identical to the M0-002/M0-003 scopes
  (`cmake` >= 3.20, `ctest`, local MSVC Build Tools bootstrap when its cache
  selects MSVC, no downloads/installs).

## 9. M0 open gates

Full M0 harness, full ScoreIR/revision coverage beyond the minimal ScoreIR fixture/schema harness, CI, project SQLite
migration/revision primitives, WinUI host gates, and the
correspondence/lineage and migration-execution engines remain open. This slice
provides the versioning/catalog foundation and the migration-planning contract
only.
