// ============================================================================
// core/score/scoreir.h - M0-005 minimal ScoreIR schema parser/validator.
//
// Lane: M0-005 minimal vertical slice. DRAFT, NON-FROZEN. Public draft
// ScoreIR schema (schemas/score/scoreir.schema.json), registered in the
// source-of-truth catalog. Scope is explicitly limited: ScoreIR source facts
// ONLY. No Geometry/anchor, Performance/transform, Jianpu, model/UI/storage
// truth, float/seconds/ticks.
//
// Contract (see docs/scoreir-schema-notes.md):
//   * The document carries a stable EntityId for the score and for every
//     referencable entity; Part, Staff, Voice, PerformerRole, Measure and
//     Event are DISTINCT collections with explicit typed ID references.
//     PerformerRole is extensible (never an SATB enum) and an event's role
//     association permits multiple roles.
//   * Event source musical time uses existing RationalTime ONLY: a measure
//     reference, a non-negative offset, and a positive duration. No
//     floats/seconds/ticks.
//   * Written pitch distinguishes written spelling, display accidental, and
//     sounding pitch (e.g. F# vs Gb are distinct spellings).
//   * Opaque/Unsupported source items are preserved losslessly.
//   * The parser/validator provides structured ScoreIRDiagnostic results (not
//     only booleans) and rejects: duplicate/malformed IDs, dangling/mistyped
//     references, duplicate JSON keys, missing required/unknown closed
//     fields, unsupported schema versions, invalid/noncanonical rational
//     times, negative offsets, nonpositive normal event durations, and
//     derived-layer fields.
//   * Canonical parse -> serialize -> parse -> serialize is byte stable and is
//     NOT a raw echo of the input bytes.
//
// C++20 portable; depends only on the existing portable primitives
// (EntityId, RationalTime). No external library.
// ============================================================================

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "entity_revision.h"

namespace choirloom::score {

// ---------------------------------------------------------------------------
// Structured diagnostics (never only bool).
// ---------------------------------------------------------------------------
enum class ScoreIRDiagnosticCode {
    MalformedScoreIRJson,     // JSON parse error or duplicate JSON key
    MissingSchemaBinding,     // missing "$schema"
    SchemaBindingMismatch,    // "$schema" not canonical, or instance carries "$id"
    UnsupportedScoreIRVersion,  // "version" not 0.1.0
    MissingRequiredField,
    UnknownField,
    DerivedLayerField,        // field belongs to a derived layer, not ScoreIR
    ReservedField,            // out-of-scope reserved metadata (e.g. humanVerified)
    UnsupportedPartType,      // part type outside the supported choral set
    MalformedEntityId,
    DuplicateEntityId,
    DanglingReference,        // referenced id does not exist
    MistypedReference,        // referenced id exists with a different type
    MalformedRationalTime,    // noncanonical / invalid RationalTime wire
    RationalTimeOutOfRange,   // rational time component out of int64 range
    NegativeOffset,
    NonPositiveDuration,
    EmptyRoleRefs,
    DuplicateRoleRef,
    MalformedPitch,
    UnknownEventKind,
    Internal,
};

constexpr std::string_view to_string(ScoreIRDiagnosticCode code)
{
    switch (code) {
        case ScoreIRDiagnosticCode::MalformedScoreIRJson: return "malformed-scoreir-json";
        case ScoreIRDiagnosticCode::MissingSchemaBinding: return "missing-schema-binding";
        case ScoreIRDiagnosticCode::SchemaBindingMismatch: return "schema-binding-mismatch";
        case ScoreIRDiagnosticCode::UnsupportedScoreIRVersion: return "unsupported-scoreir-version";
        case ScoreIRDiagnosticCode::MissingRequiredField: return "missing-required-field";
        case ScoreIRDiagnosticCode::UnknownField: return "unknown-field";
        case ScoreIRDiagnosticCode::DerivedLayerField: return "derived-layer-field";
        case ScoreIRDiagnosticCode::ReservedField: return "reserved-field";
        case ScoreIRDiagnosticCode::UnsupportedPartType: return "unsupported-part-type";
        case ScoreIRDiagnosticCode::MalformedEntityId: return "malformed-entity-id";
        case ScoreIRDiagnosticCode::DuplicateEntityId: return "duplicate-entity-id";
        case ScoreIRDiagnosticCode::DanglingReference: return "dangling-reference";
        case ScoreIRDiagnosticCode::MistypedReference: return "mistyped-reference";
        case ScoreIRDiagnosticCode::MalformedRationalTime: return "malformed-rational-time";
        case ScoreIRDiagnosticCode::RationalTimeOutOfRange: return "rational-time-out-of-range";
        case ScoreIRDiagnosticCode::NegativeOffset: return "negative-offset";
        case ScoreIRDiagnosticCode::NonPositiveDuration: return "non-positive-duration";
        case ScoreIRDiagnosticCode::EmptyRoleRefs: return "empty-role-refs";
        case ScoreIRDiagnosticCode::DuplicateRoleRef: return "duplicate-role-ref";
        case ScoreIRDiagnosticCode::MalformedPitch: return "malformed-pitch";
        case ScoreIRDiagnosticCode::UnknownEventKind: return "unknown-event-kind";
        case ScoreIRDiagnosticCode::Internal: return "internal";
    }
    return "internal";
}

struct ScoreIRDiagnostic {
    ScoreIRDiagnosticCode code = ScoreIRDiagnosticCode::Internal;
    std::optional<EntityId> entity_id;   // the entity/event the diagnostic is about
    std::optional<std::string> field;    // JSON-path-ish location, e.g. "events[1]"
    std::string message;

    friend bool operator==(ScoreIRDiagnostic const& a,
                           ScoreIRDiagnostic const& b) = default;
};

// ---------------------------------------------------------------------------
// Parsed, validated ScoreIR document.
// ---------------------------------------------------------------------------
struct ScoreIRParseResult;  // defined after ScoreIRDocument (needs it complete)

class ScoreIRDocument {
public:
    // Strict parse + validate. Returns the canonical document only when there
    // are no diagnostics; otherwise only diagnostics (structured).
    static ScoreIRParseResult from_json(std::string_view json);

    EntityId score_id() const noexcept { return score_id_; }
    std::size_t part_count() const noexcept { return parts_; }
    std::size_t staff_count() const noexcept { return staves_; }
    std::size_t voice_count() const noexcept { return voices_; }
    std::size_t performer_role_count() const noexcept { return roles_; }
    std::size_t measure_count() const noexcept { return measures_; }
    std::size_t event_count() const noexcept { return events_; }
    std::size_t pickup_measure_count() const noexcept { return pickups_; }

    // Canonical serialization; byte-stable across round trips. Not an echo.
    std::string to_json() const;

private:
    ScoreIRDocument()
        : score_id_(EntityId::from_string("1a2b3c4d-5e6f-4a7b-8c9d-0e1f2a3b4c5d")) {}
    static bool validate_document(std::string_view json, ScoreIRDocument& out,
                                  std::vector<ScoreIRDiagnostic>& diags);
    EntityId score_id_;
    std::string canonical_json_;
    std::size_t parts_ = 0;
    std::size_t staves_ = 0;
    std::size_t voices_ = 0;
    std::size_t roles_ = 0;
    std::size_t measures_ = 0;
    std::size_t events_ = 0;
    std::size_t pickups_ = 0;
};

struct ScoreIRParseResult {
    std::optional<ScoreIRDocument> document;
    std::vector<ScoreIRDiagnostic> diagnostics;
};

struct ScoreIRValidationResult {
    std::vector<ScoreIRDiagnostic> diagnostics;
};

// Validate-only entry point (no document built). Same diagnostics as from_json.
ScoreIRValidationResult validate_scoreir(std::string_view json);

}  // namespace choirloom::score
