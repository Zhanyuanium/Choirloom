// ============================================================================
// core/score/entity_revision.h - stable EntityId + Revision primitives.
//
// Lane: M0-003 minimal vertical slice. DRAFT implementation, NON-FROZEN.
// Identity contract (see docs/entity-revision-notes.md):
//   * EntityId and RevisionId are distinct, nominal, opaque 128-bit values.
//     Canonical wire form is the lower-case UUIDv4-compatible string
//       xxxxxxxx-xxxx-4xxx-[89ab]xxx-xxxxxxxxxxxx
//     and nil/all-zero values are invalid.
//   * Persistent identity only: NO ordering implication, NO content hash, NO
//     container index/address/rowid, NO MusicXML/OMR/WinUI coupling. Neither
//     type defines ordering operators.
//   * The core never silently generates random IDs. Values are constructed
//     from validated 16 bytes or from the canonical string parser; a generator
//     helper is deliberately NOT provided here. Identity allocation and
//     collision rejection are the repository boundary's responsibility, and
//     the bytes must not encode user/device/path/source content.
//   * RevisionId is equally opaque/non-ordering. ProjectRevisionMetadata is a
//     minimum project-snapshot metadata primitive (revisionId, optional
//     parentRevisionId, RevisionOrigin{ManualEdit,Recognition,Migration},
//     non-empty human-readable summary). It is NOT a correction log, undo
//     engine, recognition solver, provenance store, or HumanVerified
//     protection.
//   * Entity identity and revision identity are distinct: a normal edit
//     changes the project revision while preserving existing entity IDs;
//     reflow/cache-rebuild/serialize/reopen preserve identity and create no
//     revision. Correspondence between revisions/candidates and prior
//     entities is a future explicit relationship, never ID equality.
//   * EntityId is generic persistent identity. ScoreIR and GeometryGraph each
//     own SEPARATE identity spaces; the relation between them is explicit, and
//     equal IDs never imply cross-layer correspondence. EntityId/RevisionId
//     are NOT RationalTime values, measure offsets, RhythmicAnchors, or
//     VisualAnchors, and neither type constructs, converts to, or compares
//     with RationalTime.
//   * Re-recognition candidates get a FRESH EntityId pending resolution. An
//     accepted unambiguous one-to-one correspondence resumes/preserves the
//     prior logical EntityId; new/split/merge/uncertain cases retain explicit
//     new IDs with lineage/conflict handling. There is NO silent HumanVerified
//     overwrite. The correspondence/lineage solver itself stays out of scope.
//   * EntityId/RevisionId and future correspondence are durable project facts:
//     they survive migration, cache deletion, and reopen, and are never
//     regenerated. A legacy migration needs a deterministic recorded mapping.
//     (Persistence in SQLite remains out of scope.)
//   * compare_project_revisions is a standalone deterministic helper that
//     compares only RevisionId/metadata fields in a defined order; it does NOT
//     perform ScoreIR revision comparison.
//
// Two-level wire validation: JSON Schema enforces the syntactic grammar;
// this core enforces the semantic rules (non-nil, version/variant, 16-byte
// identity, self-parent/empty-summary/unknown-origin rejection).
//
// C++20 header + source. No external dependencies.
// ============================================================================

#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace choirloom::score {

namespace detail {

// Shared 128-bit opaque identity payload for EntityId and RevisionId.
// Canonical wire form: xxxxxxxx-xxxx-4xxx-[89ab]xxx-xxxxxxxxxxxx (lower-case).
class Uuid128 {
public:
    using Bytes = std::array<std::uint8_t, 16>;
    static constexpr std::size_t kWireLength = 36;

    // Validates non-nil and UUIDv4 version (4) / variant (8,9,a,b) bytes.
    // Throws std::invalid_argument on any violation.
    static constexpr Uuid128 from_bytes(Bytes const& b)
    {
        bool nil = true;
        for (std::uint8_t x : b) {
            if (x != 0) {
                nil = false;
                break;
            }
        }
        if (nil) {
            throw std::invalid_argument("identity: nil (all-zero) value is not allowed");
        }
        if ((b[6] >> 4) != 0x4) {
            throw std::invalid_argument("identity: bytes are not UUIDv4 (version nibble must be 4)");
        }
        const std::uint8_t variant = static_cast<std::uint8_t>(b[8] >> 4);
        if (variant != 0x8 && variant != 0x9 && variant != 0xa && variant != 0xb) {
            throw std::invalid_argument("identity: bytes are not UUIDv4 (variant nibble must be 8, 9, a, or b)");
        }
        return Uuid128(b);
    }

    // Strict canonical parser. Rejects noncanonical case (uppercase),
    // braces, missing/misplaced hyphens, wrong version/variant, nil, and
    // truncation or extra bytes. Throws std::invalid_argument.
    static constexpr Uuid128 from_string(std::string_view s)
    {
        if (s.size() != kWireLength) {
            throw std::invalid_argument("identity: expected exactly 36 characters (canonical UUIDv4 form)");
        }
        Bytes bytes{};
        std::size_t charIdx = 0;
        for (std::size_t byteIdx = 0; byteIdx < 16; ++byteIdx) {
            if (byteIdx == 4 || byteIdx == 6 || byteIdx == 8 || byteIdx == 10) {
                if (s[charIdx] != '-') {
                    throw std::invalid_argument("identity: expected '-' at a canonical hyphen position");
                }
                ++charIdx;
            }
            const int hi = hex_nibble(s[charIdx]);
            const int lo = hex_nibble(s[charIdx + 1]);
            bytes[byteIdx] = static_cast<std::uint8_t>((hi << 4) | lo);
            charIdx += 2;
        }
        if ((bytes[6] >> 4) != 0x4) {
            throw std::invalid_argument("identity: not a UUIDv4 string (version nibble must be 4)");
        }
        const std::uint8_t variant = static_cast<std::uint8_t>(bytes[8] >> 4);
        if (variant != 0x8 && variant != 0x9 && variant != 0xa && variant != 0xb) {
            throw std::invalid_argument("identity: not a UUIDv4 string (variant nibble must be 8, 9, a, or b)");
        }
        bool nil = true;
        for (std::uint8_t x : bytes) {
            if (x != 0) {
                nil = false;
                break;
            }
        }
        if (nil) {
            throw std::invalid_argument("identity: nil (all-zero) value is not allowed");
        }
        return Uuid128(bytes);
    }

    // Canonical lower-case UUIDv4-compatible string.
    constexpr std::string to_string() const
    {
        std::string out;
        out.reserve(kWireLength);
        for (std::size_t i = 0; i < bytes_.size(); ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) {
                out.push_back('-');
            }
            out.push_back("0123456789abcdef"[bytes_[i] >> 4]);
            out.push_back("0123456789abcdef"[bytes_[i] & 0x0f]);
        }
        return out;
    }

    constexpr Bytes bytes() const noexcept { return bytes_; }

    friend constexpr bool operator==(Uuid128 const& a, Uuid128 const& b) noexcept
    {
        return a.bytes_ == b.bytes_;
    }
    friend constexpr bool operator!=(Uuid128 const& a, Uuid128 const& b) noexcept
    {
        return !(a == b);
    }

private:
    explicit constexpr Uuid128(Bytes b) noexcept : bytes_(b) {}

    static constexpr int hex_nibble(char c)
    {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return 10 + (c - 'a');
        }
        if (c >= 'A' && c <= 'F') {
            throw std::invalid_argument("identity: uppercase hex is not canonical");
        }
        throw std::invalid_argument("identity: invalid character in canonical string");
    }

    Bytes bytes_;
};

}  // namespace detail

// Nominal opaque 128-bit generic persistent entity identity. ScoreIR and
// GeometryGraph each own SEPARATE identity spaces; cross-layer links are
// explicit, and equal IDs never imply cross-layer correspondence. See the
// header comment for the full identity contract. Constructed from validated bytes or canonical string.
class EntityId {
public:
    using Bytes = detail::Uuid128::Bytes;

    static constexpr EntityId from_bytes(Bytes const& b)
    {
        return EntityId(detail::Uuid128::from_bytes(b));
    }
    static constexpr EntityId from_string(std::string_view s)
    {
        return EntityId(detail::Uuid128::from_string(s));
    }

    constexpr std::string to_string() const { return value_.to_string(); }
    constexpr Bytes bytes() const noexcept { return value_.bytes(); }

    friend constexpr bool operator==(EntityId a, EntityId b) noexcept
    {
        return a.value_ == b.value_;
    }
    friend constexpr bool operator!=(EntityId a, EntityId b) noexcept
    {
        return !(a == b);
    }

private:
    explicit constexpr EntityId(detail::Uuid128 v) noexcept : value_(v) {}
    detail::Uuid128 value_;
};

// Nominal opaque 128-bit identity for project revisions. Equally
// non-ordering; intentionally a distinct type from EntityId.
class RevisionId {
public:
    using Bytes = detail::Uuid128::Bytes;

    static constexpr RevisionId from_bytes(Bytes const& b)
    {
        return RevisionId(detail::Uuid128::from_bytes(b));
    }
    static constexpr RevisionId from_string(std::string_view s)
    {
        return RevisionId(detail::Uuid128::from_string(s));
    }

    constexpr std::string to_string() const { return value_.to_string(); }
    constexpr Bytes bytes() const noexcept { return value_.bytes(); }

    friend constexpr bool operator==(RevisionId a, RevisionId b) noexcept
    {
        return a.value_ == b.value_;
    }
    friend constexpr bool operator!=(RevisionId a, RevisionId b) noexcept
    {
        return !(a == b);
    }

private:
    explicit constexpr RevisionId(detail::Uuid128 v) noexcept : value_(v) {}
    detail::Uuid128 value_;
};

// Narrow origin taxonomy for a project snapshot revision. Wire strings are the
// enum spellings exactly ("ManualEdit", "Recognition", "Migration").
enum class RevisionOrigin { ManualEdit, Recognition, Migration };

constexpr std::string_view to_string(RevisionOrigin origin)
{
    switch (origin) {
        case RevisionOrigin::ManualEdit:
            return "ManualEdit";
        case RevisionOrigin::Recognition:
            return "Recognition";
        case RevisionOrigin::Migration:
            return "Migration";
    }
    throw std::invalid_argument("unknown RevisionOrigin value");
}

// Exact, case-sensitive parse of the canonical origin string. Throws
// std::invalid_argument for unknown/malformed origins.
constexpr RevisionOrigin revision_origin_from_string(std::string_view s)
{
    if (s == "ManualEdit") {
        return RevisionOrigin::ManualEdit;
    }
    if (s == "Recognition") {
        return RevisionOrigin::Recognition;
    }
    if (s == "Migration") {
        return RevisionOrigin::Migration;
    }
    throw std::invalid_argument("unknown RevisionOrigin: \"" + std::string(s) + "\"");
}

// Minimum project-snapshot metadata primitive. NOT a correction log / undo
// engine / recognition solver / provenance store / HumanVerified protection.
// Validation rejects self-parent, empty (or whitespace-only) summary,
// malformed fields, and unknown origins.
class ProjectRevisionMetadata {
public:
    // Validated factory. Rejects self-parent, empty (or whitespace-only)
    // summary, and invalid RevisionOrigin values (e.g. from an out-of-range
    // static_cast). Throws std::invalid_argument.
    static ProjectRevisionMetadata from_values(RevisionId revision,
                                               std::optional<RevisionId> parent,
                                               RevisionOrigin origin,
                                               std::string summary);

    RevisionId revision_id() const noexcept { return revision_; }
    std::optional<RevisionId> parent_revision_id() const noexcept { return parent_; }
    RevisionOrigin origin() const noexcept { return origin_; }
    std::string const& summary() const noexcept { return summary_; }

    // Canonical JSON wire form:
    //   {"revisionId":"...","parentRevisionId":"...","origin":"...","summary":"..."}
    // (parentRevisionId present only when a parent exists; no spaces).
    std::string to_json() const;

    // Strict parse of the canonical JSON object. Rejects unknown/duplicate
    // fields, non-string values, unknown origins, self-parent, empty summary,
    // and malformed revision ids. Throws std::invalid_argument.
    static ProjectRevisionMetadata from_json(std::string_view json);

    friend bool operator==(ProjectRevisionMetadata const& a,
                           ProjectRevisionMetadata const& b) = default;

private:
    ProjectRevisionMetadata(RevisionId revision, std::optional<RevisionId> parent,
                            RevisionOrigin origin, std::string summary)
        : revision_(revision), parent_(parent), origin_(origin),
          summary_(std::move(summary)) {}
    RevisionId revision_;
    std::optional<RevisionId> parent_;
    RevisionOrigin origin_ = RevisionOrigin::ManualEdit;
    std::string summary_;
};

// Standalone deterministic metadata comparison. Compares ONLY RevisionId and
// metadata fields in this defined order: revisionId (byte order), then
// parentRevisionId (absent < present, then byte order), then origin (enum
// declaration order), then summary (lexicographic). This is a metadata
// ordering for deterministic tooling; it does NOT perform ScoreIR revision
// comparison and does not give RevisionId an ordering semantic elsewhere.
std::strong_ordering compare_project_revisions(ProjectRevisionMetadata const& a,
                                               ProjectRevisionMetadata const& b);

}  // namespace choirloom::score

namespace std {
template <>
struct hash<choirloom::score::EntityId> {
    size_t operator()(choirloom::score::EntityId const& id) const noexcept
    {
        // FNV-1a over the 16 identity bytes (deterministic, no allocation).
        size_t h = 14695981039346656037ULL;
        for (std::uint8_t b : id.bytes()) {
            h ^= static_cast<size_t>(b);
            h *= 1099511628211ULL;
        }
        return h;
    }
};
template <>
struct hash<choirloom::score::RevisionId> {
    size_t operator()(choirloom::score::RevisionId const& id) const noexcept
    {
        size_t h = 14695981039346656037ULL;
        for (std::uint8_t b : id.bytes()) {
            h ^= static_cast<size_t>(b);
            h *= 1099511628211ULL;
        }
        return h;
    }
};
}  // namespace std
