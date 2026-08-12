// ============================================================================
// core/score/schema_foundation.h - M0-004 Schema / Versioning Foundation.
//
// Lane: M0-004 minimal vertical slice. DRAFT implementation, NON-FROZEN.
// Authoritative contract (see docs/schema-foundation-notes.md):
//   * SchemaId, SchemaVersion, SchemaDocumentId are DISTINCT nominal concepts,
//     never interchangeable.
//       - SchemaId: stable format-family string WITHOUT version, canonical
//         "choirloom:score/<kebab-name>"; it is neither a file path nor a
//         complete $id.
//       - SchemaVersion: strict canonical "major.minor.patch" with non-negative
//         uint32-like components, no leading zeros (except "0"), exactly three
//         segments, no prerelease/build metadata; strict parser, no silent
//         normalization. Comparisons are meaningful ONLY within the same
//         SchemaId.
//       - SchemaDocumentId: the complete existing JSON Schema $id, canonical
//         "<SchemaId>/<SchemaVersion>"; parsing/roundtrip validates that
//         relation.
//   * ProjectRevision/RevisionId, ModelVersion, ApplicationVersion, Git SHA and
//     the SQLite store-schema version are SEPARATE concepts and must not enter
//     these types or the catalog as substitutes.
//   * Validation APIs return structured ValidationDiagnostic collections, not
//     only booleans. Construction parsing may throw std::invalid_argument /
//     std::out_of_range; catalog inspection/compatibility use diagnostics.
//   * SchemaCatalog is an immutable declarative registry: exact
//     (SchemaId, SchemaVersion) lookup only; NO latest/nearest fallback;
//     deterministic iteration/order. Entries carry schemaId, schemaVersion,
//     documentId, repo-relative path, status {Draft, Stable, Deprecated}. The
//     catalog itself has a separate catalogFormatVersion (not an entry
//     SchemaVersion).
//   * Compatibility distinguishes Exact, Readable, RequiresMigration,
//     Unsupported - derived ONLY from explicit declared directional rules
//     (catalog "rules"), never inferred from semver. Unknown future version is
//     UnsupportedFutureVersion, not malformed; missing/malformed get distinct
//     stable diagnostics.
//   * Migration planning is a CONTRACT ONLY: MigrationPlan with source/target
//     identities and ordered declared edges; plan_migration is deterministic,
//     performs no execution or mutation. There is NO SQLite/project migration
//     engine and NO historical schema conversion.
//   * Local-only: catalog resolution permits only registered entries and the
//     declared JSON Schema dialect identifier; no network, file watcher,
//     plugin, or script execution.
//
// C++20 portable, no external library/platform dependency.
// ============================================================================

#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace choirloom::score {

// ---------------------------------------------------------------------------
// SchemaVersion - strict canonical "major.minor.patch".
// ---------------------------------------------------------------------------
class SchemaVersion {
public:
    using Component = std::uint32_t;

    // Strict canonical parser. Grammar violations (leading zeros, wrong
    // segment count, prerelease/build metadata, non-digits) throw
    // std::invalid_argument; a component exceeding the uint32 range throws
    // std::out_of_range. There is no silent normalization.
    static SchemaVersion from_string(std::string_view s);
    static SchemaVersion from_components(Component major, Component minor,
                                         Component patch) noexcept;

    std::string to_string() const;
    Component major() const noexcept { return major_; }
    Component minor() const noexcept { return minor_; }
    Component patch() const noexcept { return patch_; }

    friend bool operator==(SchemaVersion a, SchemaVersion b) noexcept
    {
        return a.major_ == b.major_ && a.minor_ == b.minor_ && a.patch_ == b.patch_;
    }
    friend bool operator!=(SchemaVersion a, SchemaVersion b) noexcept
    {
        return !(a == b);
    }
    // Component-wise ordering; meaningful only within the same SchemaId.
    friend std::strong_ordering operator<=>(SchemaVersion a,
                                            SchemaVersion b) noexcept
    {
        if (a.major_ != b.major_) {
            return a.major_ < b.major_ ? std::strong_ordering::less
                                       : std::strong_ordering::greater;
        }
        if (a.minor_ != b.minor_) {
            return a.minor_ < b.minor_ ? std::strong_ordering::less
                                       : std::strong_ordering::greater;
        }
        if (a.patch_ != b.patch_) {
            return a.patch_ < b.patch_ ? std::strong_ordering::less
                                       : std::strong_ordering::greater;
        }
        return std::strong_ordering::equal;
    }

private:
    constexpr SchemaVersion(Component major, Component minor, Component patch) noexcept
        : major_(major), minor_(minor), patch_(patch) {}
    Component major_;
    Component minor_;
    Component patch_;
};

// ---------------------------------------------------------------------------
// SchemaId - stable format-family identity WITHOUT version.
// ---------------------------------------------------------------------------
class SchemaId {
public:
    static SchemaId from_string(std::string_view s);

    std::string to_string() const;

    friend bool operator==(SchemaId const& a, SchemaId const& b) noexcept
    {
        return a.value_ == b.value_;
    }
    friend bool operator!=(SchemaId const& a, SchemaId const& b) noexcept
    {
        return !(a == b);
    }
    friend std::strong_ordering operator<=>(SchemaId const& a,
                                            SchemaId const& b) noexcept
    {
        return a.value_ <=> b.value_;
    }

private:
    explicit SchemaId(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

// ---------------------------------------------------------------------------
// SchemaDocumentId - complete existing JSON Schema $id "<SchemaId>/<Version>".
// ---------------------------------------------------------------------------
class SchemaDocumentId {
public:
    static SchemaDocumentId from_string(std::string_view s);
    static SchemaDocumentId from_parts(SchemaId id, SchemaVersion version);

    SchemaId schema_id() const { return id_; }
    SchemaVersion schema_version() const { return version_; }
    std::string to_string() const;

    friend bool operator==(SchemaDocumentId const& a,
                           SchemaDocumentId const& b) noexcept
    {
        return a.id_ == b.id_ && a.version_ == b.version_;
    }
    friend bool operator!=(SchemaDocumentId const& a,
                           SchemaDocumentId const& b) noexcept
    {
        return !(a == b);
    }
    friend std::strong_ordering operator<=>(SchemaDocumentId const& a,
                                            SchemaDocumentId const& b) noexcept
    {
        return a.to_string() <=> b.to_string();
    }

private:
    SchemaDocumentId(SchemaId id, SchemaVersion version)
        : id_(std::move(id)), version_(version) {}
    SchemaId id_;
    SchemaVersion version_;
};

// ---------------------------------------------------------------------------
// Diagnostics - structured validation results (never only bool).
// ---------------------------------------------------------------------------
enum class DiagnosticCode {
    DuplicateSchemaEntry,        // duplicate (schemaId, schemaVersion) tuple
    DuplicateDocumentId,
    DuplicateSchemaPath,
    AbsoluteSchemaPath,          // path is absolute, not repo-relative
    PathTraversal,               // ".." segment in repo-relative path
    MissingSchemaFile,
    DocumentIdSchemaIdMismatch,  // entry documentId.schemaId != entry schemaId
    DocumentIdVersionMismatch,   // entry documentId.version != entry schemaVersion
    OnDiskIdMismatch,            // schema file "$id" != catalog documentId
    OnDiskVersionMismatch,       // schema file "version" != catalog schemaVersion
    RuleTargetUnknown,           // a catalog rule references an unregistered docId
    UnregisteredRef,             // local $ref not registered nor the dialect id
    SchemaIdFormat,
    SchemaVersionFormat,
    SchemaVersionOutOfRange,
    SchemaDocumentIdFormat,
    CatalogFormatUnsupported,
    MissingCatalogFile,
    MalformedCatalogJson,
    MissingSchemaVersion,        // inspection: no version string provided
    MalformedSchemaVersion,      // inspection: version string malformed
    DuplicateRule,               // same (from, to, kind) declared twice
    ConflictingRule,             // same (from, to) declared with different kinds
    CrossSchemaEdge,             // rule edges across different SchemaIds
    UnknownSchema,               // lookup: unknown SchemaId
    UnknownSchemaVersion,        // lookup: known schema, unregistered (non-future) version
    UnsupportedFutureVersion,    // lookup: version newer than any registered
    UnsupportedDirection,        // compatibility: no declared edge
    MissingMigrationEdge,        // plan: no migration path
    Internal,
};

constexpr std::string_view to_string(DiagnosticCode code)
{
    switch (code) {
        case DiagnosticCode::DuplicateSchemaEntry: return "duplicate-schema-entry";
        case DiagnosticCode::DuplicateDocumentId: return "duplicate-document-id";
        case DiagnosticCode::DuplicateSchemaPath: return "duplicate-schema-path";
        case DiagnosticCode::AbsoluteSchemaPath: return "absolute-schema-path";
        case DiagnosticCode::PathTraversal: return "path-traversal";
        case DiagnosticCode::MissingSchemaFile: return "missing-schema-file";
        case DiagnosticCode::DocumentIdSchemaIdMismatch: return "document-id-schema-id-mismatch";
        case DiagnosticCode::DocumentIdVersionMismatch: return "document-id-version-mismatch";
        case DiagnosticCode::OnDiskIdMismatch: return "on-disk-id-mismatch";
        case DiagnosticCode::OnDiskVersionMismatch: return "on-disk-version-mismatch";
        case DiagnosticCode::RuleTargetUnknown: return "rule-target-unknown";
        case DiagnosticCode::UnregisteredRef: return "unregistered-ref";
        case DiagnosticCode::SchemaIdFormat: return "schema-id-format";
        case DiagnosticCode::SchemaVersionFormat: return "schema-version-format";
        case DiagnosticCode::SchemaVersionOutOfRange: return "schema-version-out-of-range";
        case DiagnosticCode::SchemaDocumentIdFormat: return "schema-document-id-format";
        case DiagnosticCode::CatalogFormatUnsupported: return "catalog-format-unsupported";
        case DiagnosticCode::MissingCatalogFile: return "missing-catalog-file";
        case DiagnosticCode::MalformedCatalogJson: return "malformed-catalog-json";
        case DiagnosticCode::MissingSchemaVersion: return "missing-schema-version";
        case DiagnosticCode::MalformedSchemaVersion: return "malformed-schema-version";
        case DiagnosticCode::DuplicateRule: return "duplicate-rule";
        case DiagnosticCode::ConflictingRule: return "conflicting-rule";
        case DiagnosticCode::CrossSchemaEdge: return "cross-schema-edge";
        case DiagnosticCode::UnknownSchema: return "unknown-schema";
        case DiagnosticCode::UnknownSchemaVersion: return "unknown-schema-version";
        case DiagnosticCode::UnsupportedFutureVersion: return "unsupported-future-version";
        case DiagnosticCode::UnsupportedDirection: return "unsupported-direction";
        case DiagnosticCode::MissingMigrationEdge: return "missing-migration-edge";
        case DiagnosticCode::Internal: return "internal";
    }
    return "internal";
}

struct ValidationDiagnostic {
    DiagnosticCode code = DiagnosticCode::Internal;
    std::optional<SchemaId> schema_id;
    std::optional<SchemaVersion> schema_version;
    std::optional<std::string> path;  // repo-relative logical document path only
    std::string message;

    friend bool operator==(ValidationDiagnostic const& a,
                           ValidationDiagnostic const& b) = default;
};

// ---------------------------------------------------------------------------
// Catalog primitives.
// ---------------------------------------------------------------------------
enum class SchemaStatus { Draft, Stable, Deprecated };
constexpr std::string_view to_string(SchemaStatus status)
{
    switch (status) {
        case SchemaStatus::Draft: return "Draft";
        case SchemaStatus::Stable: return "Stable";
        case SchemaStatus::Deprecated: return "Deprecated";
    }
    throw std::invalid_argument("unknown SchemaStatus value");
}
SchemaStatus schema_status_from_string(std::string_view s);

enum class SchemaRuleKind { Readable, Migration };
constexpr std::string_view to_string(SchemaRuleKind kind)
{
    switch (kind) {
        case SchemaRuleKind::Readable: return "readable";
        case SchemaRuleKind::Migration: return "migration";
    }
    throw std::invalid_argument("unknown SchemaRuleKind value");
}
SchemaRuleKind schema_rule_kind_from_string(std::string_view s);

struct SchemaCatalogEntry {
    SchemaId schema_id;
    SchemaVersion schema_version;
    SchemaDocumentId document_id;
    std::string path;  // repo-relative, forward slashes, source tree only
    SchemaStatus status = SchemaStatus::Draft;

    friend bool operator==(SchemaCatalogEntry const& a,
                           SchemaCatalogEntry const& b) = default;
};

struct SchemaCatalogRule {
    SchemaDocumentId from;
    SchemaDocumentId to;
    SchemaRuleKind kind = SchemaRuleKind::Migration;

    friend bool operator==(SchemaCatalogRule const& a,
                           SchemaCatalogRule const& b) = default;
};

// Immutable declarative registry. Exact-tuple lookup only; deterministic order.
struct SchemaCatalogLoadResult;  // defined after SchemaCatalog (needs it complete)

class SchemaCatalog {
public:
    std::uint32_t catalog_format_version() const noexcept { return format_; }
    std::vector<SchemaCatalogEntry> const& entries() const noexcept { return entries_; }
    std::vector<SchemaCatalogRule> const& rules() const noexcept { return rules_; }

    // Parse the declarative catalog JSON. Parse problems surface as
    // diagnostics (catalog absent only when the root is unusable).
    static SchemaCatalogLoadResult from_json(std::string_view json);

private:
    friend class SchemaCatalogBuilder;
    SchemaCatalog() = default;
    std::uint32_t format_ = 1;
    std::vector<SchemaCatalogEntry> entries_;
    std::vector<SchemaCatalogRule> rules_;
};

struct SchemaCatalogLoadResult {
    std::optional<SchemaCatalog> catalog;
    std::vector<ValidationDiagnostic> diagnostics;
};

class SchemaCatalogBuilder {
public:
    SchemaCatalogBuilder& catalog_format_version(std::uint32_t v)
    {
        format_ = v;
        return *this;
    }
    SchemaCatalogBuilder& add_entry(SchemaCatalogEntry e)
    {
        entries_.push_back(std::move(e));
        return *this;
    }
    SchemaCatalogBuilder& add_rule(SchemaCatalogRule r)
    {
        rules_.push_back(std::move(r));
        return *this;
    }
    SchemaCatalog build() const;

private:
    std::uint32_t format_ = 1;
    std::vector<SchemaCatalogEntry> entries_;
    std::vector<SchemaCatalogRule> rules_;
};

// ---------------------------------------------------------------------------
// Inspection / compatibility / migration-planning (structured diagnostics).
// ---------------------------------------------------------------------------
struct CatalogLookupResult {
    std::optional<SchemaCatalogEntry> entry;
    std::vector<ValidationDiagnostic> diagnostics;
};

enum class Compatibility { Exact, Readable, RequiresMigration, Unsupported };
constexpr std::string_view to_string(Compatibility c)
{
    switch (c) {
        case Compatibility::Exact: return "Exact";
        case Compatibility::Readable: return "Readable";
        case Compatibility::RequiresMigration: return "RequiresMigration";
        case Compatibility::Unsupported: return "Unsupported";
    }
    return "Unsupported";
}

struct CompatibilityResult {
    Compatibility value = Compatibility::Unsupported;
    std::vector<ValidationDiagnostic> diagnostics;
};

struct MigrationStep {
    SchemaDocumentId from;
    SchemaDocumentId to;
    SchemaRuleKind kind = SchemaRuleKind::Migration;

    friend bool operator==(MigrationStep const& a, MigrationStep const& b) = default;
};

struct MigrationPlan {
    SchemaDocumentId source;
    SchemaDocumentId target;
    std::vector<MigrationStep> steps;  // ordered declared migration edges
    std::vector<ValidationDiagnostic> diagnostics;

    friend bool operator==(MigrationPlan const& a, MigrationPlan const& b) = default;
};

// Structural validation of the catalog data (no file IO).
std::vector<ValidationDiagnostic> validate_catalog(SchemaCatalog const& catalog);

// File-backed validation against a repository root: file existence, on-disk
// "$id"/"version" match, and local "$ref" resolution (registered entries or
// the JSON Schema dialect identifier only).
std::vector<ValidationDiagnostic> validate_catalog_files(
    SchemaCatalog const& catalog, std::string_view repo_root);

// Exact-tuple lookup; no latest/nearest fallback. Future version (newer than
// any registered for the schema) -> UnsupportedFutureVersion.
CatalogLookupResult lookup_schema(SchemaCatalog const& catalog,
                                  SchemaId const& id,
                                  SchemaVersion const& version);

// Structured raw-version inspection: callers do NOT have to parse first. The
// optional raw version string is interpreted strictly and mapped to exact
// stable codes: MissingSchemaVersion (absent), MalformedSchemaVersion
// (grammar), SchemaVersionOutOfRange (uint32 overflow), UnknownSchema,
// UnknownSchemaVersion, UnsupportedFutureVersion. A future version is
// UnsupportedFutureVersion, never malformed. Returns the registered entry
// with no diagnostics on success.
struct VersionInspectionResult {
    std::optional<SchemaCatalogEntry> entry;
    std::vector<ValidationDiagnostic> diagnostics;
};
VersionInspectionResult inspect_schema_version(SchemaCatalog const& catalog,
                                               SchemaId const& id,
                                               std::optional<std::string_view> raw_version);

// Explicit directional compatibility. Exact when equal; Readable/RequiresMigration
// only when a declared catalog rule covers the (from,to) direction; otherwise
// Unsupported with a structured diagnostic. No semver inference.
CompatibilityResult schema_compatibility(SchemaCatalog const& catalog,
                                         SchemaId const& id,
                                         SchemaVersion const& from,
                                         SchemaVersion const& to);

// Deterministic migration planning CONTRACT ONLY: follows declared migration
// edges in deterministic order; no execution or mutation. Missing path or
// unsupported direction returns structured diagnostics. There is NO SQLite/
// project migration engine and no historical schema conversion.
MigrationPlan plan_migration(SchemaCatalog const& catalog,
                             SchemaDocumentId const& source,
                             SchemaDocumentId const& target);

}  // namespace choirloom::score

namespace std {
template <>
struct hash<choirloom::score::SchemaId> {
    size_t operator()(choirloom::score::SchemaId const& id) const noexcept
    {
        return std::hash<std::string>()(id.to_string());
    }
};
template <>
struct hash<choirloom::score::SchemaVersion> {
    size_t operator()(choirloom::score::SchemaVersion const& v) const noexcept
    {
        size_t h = 14695981039346656037ULL;
        for (std::uint32_t c : {v.major(), v.minor(), v.patch()}) {
            h ^= static_cast<size_t>(c);
            h *= 1099511628211ULL;
        }
        return h;
    }
};
template <>
struct hash<choirloom::score::SchemaDocumentId> {
    size_t operator()(choirloom::score::SchemaDocumentId const& d) const noexcept
    {
        return std::hash<std::string>()(d.to_string());
    }
};
}  // namespace std
