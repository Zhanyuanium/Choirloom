// ============================================================================
// tests/unit/entity_revision_tests.cpp - unit tests for M0-003 EntityId +
// Revision primitives.
//
// Self-contained, dependency-free runner (the M0 harness/CI does not exist
// yet). Compile with any C++20 compiler or via the slice-local CMake/CTest
// setup. When built through CMake, CHOIRLOOM_TEST_SOURCE_DIR is an absolute
// repository root so the golden fixture and schema files resolve from the
// build tree; when compiled directly, run from the repository root.
//
// Coverage: nominal compile-time distinctness/non-interchangeability,
// canonical wire round-trips, constructed-byte uniqueness, equality/hash,
// strict parse rejection, ProjectRevisionMetadata validation and JSON,
// compare_project_revisions determinism, and the golden fixture
// (parse / validate / deserialize / canonical reserialize / compare).
// ============================================================================

#include "entity_revision.h"
#include "rational_time.h"
#include "test_json.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef CHOIRLOOM_TEST_SOURCE_DIR
#define CHOIRLOOM_TEST_SOURCE_DIR "."
#endif

using choirloom::score::compare_project_revisions;
using choirloom::score::EntityId;
using choirloom::score::ProjectRevisionMetadata;
using choirloom::score::RationalTime;
using choirloom::score::RevisionId;
using choirloom::score::RevisionOrigin;

// ---------------------------------------------------------------------------
// Compile-time contract: nominal distinct types, no interchangeability, no
// ordering operators, no cross-type equality, constexpr wire round-trips.
// ---------------------------------------------------------------------------
namespace {
template <typename T, typename = void>
struct has_less : std::false_type {};
template <typename T>
struct has_less<T, std::void_t<decltype(std::declval<T>() < std::declval<T>())>>
    : std::true_type {};

template <typename T, typename = void>
struct has_spaceship : std::false_type {};
template <typename T>
struct has_spaceship<T, std::void_t<decltype(std::declval<T>() <=> std::declval<T>())>>
    : std::true_type {};

template <typename A, typename B, typename = void>
struct has_eq : std::false_type {};
template <typename A, typename B>
struct has_eq<A, B, std::void_t<decltype(std::declval<A>() == std::declval<B>())>>
    : std::true_type {};
}  // namespace

static_assert(!std::is_same_v<EntityId, RevisionId>);
static_assert(!std::is_convertible_v<EntityId, RevisionId>);
static_assert(!std::is_convertible_v<RevisionId, EntityId>);
static_assert(!std::is_constructible_v<EntityId, RevisionId>);
static_assert(!std::is_constructible_v<RevisionId, EntityId>);
static_assert(sizeof(EntityId) == 16);
static_assert(sizeof(RevisionId) == 16);
static_assert(!has_less<EntityId>::value);         // no ordering operators
static_assert(!has_less<RevisionId>::value);
static_assert(!has_spaceship<EntityId>::value);
static_assert(!has_spaceship<RevisionId>::value);
static_assert(!has_eq<EntityId, RevisionId>::value);  // no cross-type equality
static_assert(!has_eq<RevisionId, EntityId>::value);

// EntityId/RevisionId are identity primitives, NOT RationalTime values, measure
// offsets, RhythmicAnchors, or VisualAnchors: neither type constructs,
// converts to, or compares with RationalTime (no anchor types exist here).
static_assert(!std::is_constructible_v<EntityId, RationalTime>);
static_assert(!std::is_constructible_v<RationalTime, EntityId>);
static_assert(!std::is_constructible_v<RevisionId, RationalTime>);
static_assert(!std::is_constructible_v<RationalTime, RevisionId>);
static_assert(!std::is_convertible_v<EntityId, RationalTime>);
static_assert(!std::is_convertible_v<RationalTime, EntityId>);
static_assert(!std::is_convertible_v<RevisionId, RationalTime>);
static_assert(!std::is_convertible_v<RationalTime, RevisionId>);
static_assert(!has_eq<EntityId, RationalTime>::value);
static_assert(!has_eq<RationalTime, EntityId>::value);
static_assert(!has_eq<RevisionId, RationalTime>::value);
static_assert(!has_eq<RationalTime, RevisionId>::value);

constexpr EntityId kEntityAlpha = EntityId::from_string("8f14e45f-ceea-4a6a-b5e2-9d5c9a1f3b27");
static_assert(kEntityAlpha.to_string() == "8f14e45f-ceea-4a6a-b5e2-9d5c9a1f3b27");
static_assert(kEntityAlpha == EntityId::from_string("8f14e45f-ceea-4a6a-b5e2-9d5c9a1f3b27"));

constexpr EntityId::Bytes kBytesA = {
    0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0x4d, 0xef,
    0x8a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78};
static_assert(EntityId::from_bytes(kBytesA).to_string() == "12345678-9abc-4def-8abc-def012345678");
static_assert(EntityId::from_bytes(kBytesA).bytes() == kBytesA);

constexpr RevisionId kRevInitial = RevisionId::from_string("c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f");
static_assert(kRevInitial.to_string() == "c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f");
static_assert(kRevInitial == RevisionId::from_bytes(kRevInitial.bytes()));

static_assert(static_cast<int>(RevisionOrigin::ManualEdit) == 0);
static_assert(static_cast<int>(RevisionOrigin::Recognition) == 1);
static_assert(static_cast<int>(RevisionOrigin::Migration) == 2);

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* group, const char* what)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL [%-10s] %s\n", group, what);
    }
}

#define CHECK(group, expr) check(static_cast<bool>(expr), group, #expr)
#define CHECK_THROWS(group, expr, ex)                                          \
    do {                                                                       \
        bool threw = false;                                                    \
        try {                                                                  \
            (void)(expr);                                                      \
        } catch (ex const&) {                                                  \
            threw = true;                                                      \
        } catch (...) {                                                        \
        }                                                                      \
        check(threw, group, "throws " #ex ": " #expr);                         \
    } while (false)

std::string test_root() { return std::string(CHOIRLOOM_TEST_SOURCE_DIR); }

std::string read_text_file(std::string const& path, bool& ok)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ok = false;
        return std::string();
    }
    ok = true;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string m_no_parent_json();
std::string m_with_parent_json();

// ---------------------------------------------------------------------------
// Canonical wire round-trips (exact identity preservation)
// ---------------------------------------------------------------------------
void test_wire()
{
    const char* g = "wire";

    const char* const kSamples[] = {
        "8f14e45f-ceea-4a6a-b5e2-9d5c9a1f3b27",
        "a3c9d2e1-4f5a-4b6c-8d7e-9f0a1b2c3d4e",
        "6b1e2d3c-5f4a-4b8c-a9d0-1e2f3a4b5c6d",
        "c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f",
        "f0e1d2c3-b4a5-4e6f-9a8b-7c6d5e4f3a2b",
        "1a2b3c4d-5e6f-4a7b-8c9d-0e1f2a3b4c5d",
    };
    for (char const* s : kSamples) {
        const EntityId e = EntityId::from_string(s);
        CHECK(g, e.to_string() == std::string(s));          // exact canonical
        CHECK(g, EntityId::from_string(e.to_string()) == e); // parse(serialize) == id
        CHECK(g, e.bytes() == EntityId::from_string(s).bytes());
        const RevisionId r = RevisionId::from_string(s);
        CHECK(g, r.to_string() == std::string(s));
        CHECK(g, RevisionId::from_string(r.to_string()) == r);
    }

    // bytes -> string -> bytes -> string stability.
    const EntityId fromBytes = EntityId::from_bytes(kBytesA);
    CHECK(g, fromBytes.to_string() == "12345678-9abc-4def-8abc-def012345678");
    CHECK(g, EntityId::from_string(fromBytes.to_string()).bytes() == kBytesA);
}

// ---------------------------------------------------------------------------
// Constructed-byte uniqueness
// ---------------------------------------------------------------------------
void test_uniqueness()
{
    const char* g = "unique";

    const EntityId::Bytes a = {
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0x4d, 0xef,
        0x8a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78};
    const EntityId::Bytes b = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x4f, 0x07,
        0x9a, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

    const EntityId ea = EntityId::from_bytes(a);
    const EntityId eb = EntityId::from_bytes(b);
    CHECK(g, ea != eb);
    CHECK(g, ea.to_string() != eb.to_string());
    CHECK(g, ea.bytes() != eb.bytes());

    const RevisionId ra = RevisionId::from_bytes(a);
    const RevisionId rb = RevisionId::from_bytes(b);
    CHECK(g, ra != rb);
    CHECK(g, ra.to_string() != rb.to_string());
}

// ---------------------------------------------------------------------------
// Equality / hash
// ---------------------------------------------------------------------------
void test_equality_hash()
{
    const char* g = "eqhash";

    const EntityId e1 = EntityId::from_string("8f14e45f-ceea-4a6a-b5e2-9d5c9a1f3b27");
    const EntityId e2 = EntityId::from_string("8f14e45f-ceea-4a6a-b5e2-9d5c9a1f3b27");
    const EntityId e3 = EntityId::from_string("a3c9d2e1-4f5a-4b6c-8d7e-9f0a1b2c3d4e");
    CHECK(g, e1 == e2);
    CHECK(g, e1 != e3);
    CHECK(g, std::hash<EntityId>()(e1) == std::hash<EntityId>()(e2));

    const RevisionId r1 = RevisionId::from_string("c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f");
    const RevisionId r2 = RevisionId::from_string("c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f");
    CHECK(g, r1 == r2);
    CHECK(g, std::hash<RevisionId>()(r1) == std::hash<RevisionId>()(r2));
}

// ---------------------------------------------------------------------------
// Strict parse rejection (noncanonical case, braces, hyphens, version,
// variant, nil, truncation, extra bytes)
// ---------------------------------------------------------------------------
void test_parse_errors()
{
    const char* g = "parse";

    // Noncanonical case.
    CHECK_THROWS(g, EntityId::from_string("8F14E45F-CEEA-4A6A-B5E2-9D5C9A1F3B27"), std::invalid_argument);
    // Braces.
    CHECK_THROWS(g, EntityId::from_string("{8f14e45f-ceea-4a6a-b5e2-9d5c9a1f3b27}"), std::invalid_argument);
    // Missing hyphens.
    CHECK_THROWS(g, EntityId::from_string("8f14e45fceea4a6ab5e29d5c9a1f3b27"), std::invalid_argument);
    // Truncation / extra bytes.
    CHECK_THROWS(g, EntityId::from_string("8f14e45f-ceea-4a6a-b5e2-9d5c9a1f3b2"), std::invalid_argument);
    CHECK_THROWS(g, EntityId::from_string("8f14e45f-ceea-4a6a-b5e2-9d5c9a1f3b27X"), std::invalid_argument);
    // Wrong version / variant.
    CHECK_THROWS(g, EntityId::from_string("8f14e45f-ceea-3a6a-b5e2-9d5c9a1f3b27"), std::invalid_argument);
    CHECK_THROWS(g, EntityId::from_string("8f14e45f-ceea-4a6a-75e2-9d5c9a1f3b27"), std::invalid_argument);
    // Nil can only be rejected at the byte level: a canonical UUIDv4 string
    // always has a non-zero version/variant nibble, so the string above is a
    // legal (non-nil) v4 value.
    CHECK(g, EntityId::from_string("00000000-0000-4000-8000-000000000000").to_string() ==
                  "00000000-0000-4000-8000-000000000000");
    // Empty / invalid character.
    CHECK_THROWS(g, EntityId::from_string(""), std::invalid_argument);
    CHECK_THROWS(g, EntityId::from_string("8f14e45f-ceea-4a6a-b5e2-9d5c9a1f3b2z"), std::invalid_argument);

    // Byte-level validation.
    EntityId::Bytes nil{};
    CHECK_THROWS(g, EntityId::from_bytes(nil), std::invalid_argument);
    EntityId::Bytes badVersion = kBytesA;
    badVersion[6] = 0x30;  // version 3
    CHECK_THROWS(g, EntityId::from_bytes(badVersion), std::invalid_argument);
    EntityId::Bytes badVariant = kBytesA;
    badVariant[8] = 0x70;  // variant 7
    CHECK_THROWS(g, EntityId::from_bytes(badVariant), std::invalid_argument);

    // Same strictness applies to RevisionId (bytes-level nil; v4 strings are
    // never all-zero).
    CHECK_THROWS(g, RevisionId::from_string("C1D2E3F4-A5B6-4C7D-8E9F-0A1B2C3D4E5F"), std::invalid_argument);
    CHECK_THROWS(g, RevisionId::from_bytes(nil), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// ProjectRevisionMetadata: values, JSON wire, validation
// ---------------------------------------------------------------------------
void test_metadata()
{
    const char* g = "metadata";

    const RevisionId r1 = RevisionId::from_string("c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f");
    const RevisionId r2 = RevisionId::from_string("f0e1d2c3-b4a5-4e6f-9a8b-7c6d5e4f3a2b");

    // Valid: no parent.
    {
        const ProjectRevisionMetadata m = ProjectRevisionMetadata::from_values(
            r1, std::nullopt, RevisionOrigin::ManualEdit,
            std::string("Initial manual correction of the soprano part."));
        CHECK(g, m.revision_id() == r1);
        CHECK(g, !m.parent_revision_id().has_value());
        CHECK(g, m.origin() == RevisionOrigin::ManualEdit);
        CHECK(g, m.summary() == "Initial manual correction of the soprano part.");
    }
    // Valid: with parent.
    {
        const ProjectRevisionMetadata m = ProjectRevisionMetadata::from_values(
            r2, std::optional<RevisionId>(r1), RevisionOrigin::Recognition,
            std::string("Re-recognition of page 2 after a model update."));
        CHECK(g, m.revision_id() == r2);
        CHECK(g, m.parent_revision_id().has_value() && *m.parent_revision_id() == r1);
    }

    // Validation rejects self-parent.
    CHECK_THROWS(g, ProjectRevisionMetadata::from_values(
                        r1, std::optional<RevisionId>(r1), RevisionOrigin::ManualEdit,
                        std::string("summary")),
                 std::invalid_argument);
    // Validation rejects empty and whitespace-only summary.
    CHECK_THROWS(g, ProjectRevisionMetadata::from_values(
                        r1, std::nullopt, RevisionOrigin::ManualEdit, std::string("")),
                 std::invalid_argument);
    CHECK_THROWS(g, ProjectRevisionMetadata::from_values(
                        r1, std::nullopt, RevisionOrigin::ManualEdit, std::string("   \t ")),
                 std::invalid_argument);

    // Origin string round-trip and unknown-origin rejection.
    CHECK(g, choirloom::score::to_string(RevisionOrigin::Recognition) == "Recognition");
    CHECK(g, choirloom::score::revision_origin_from_string("Migration") == RevisionOrigin::Migration);
    CHECK_THROWS(g, choirloom::score::revision_origin_from_string("Manualedit"), std::invalid_argument);
    CHECK_THROWS(g, choirloom::score::revision_origin_from_string("Foo"), std::invalid_argument);

    // from_values rejects invalid RevisionOrigin values (e.g. from an
    // out-of-range static_cast).
    CHECK_THROWS(g, ProjectRevisionMetadata::from_values(
                        r1, std::nullopt, static_cast<RevisionOrigin>(7),
                        std::string("summary")),
                 std::invalid_argument);
    CHECK_THROWS(g, ProjectRevisionMetadata::from_values(
                        r1, std::nullopt, static_cast<RevisionOrigin>(-1),
                        std::string("summary")),
                 std::invalid_argument);

    // Exact JSON inverse for every accepted summary, including control bytes:
    // to_json escapes them (named escapes and \u00XX); from_json must decode
    // them back so from_json(to_json(m)) == m.
    {
        const std::string tricky =
            std::string("line1\nline2\t\"quoted\"\\backslash\x01\x1f") +
            std::string("\xF0\x9F\x8E\xB5");  // raw UTF-8 non-BMP char
        const ProjectRevisionMetadata m = ProjectRevisionMetadata::from_values(
            r1, std::nullopt, RevisionOrigin::ManualEdit, tricky);
        const std::string wire = m.to_json();
        const ProjectRevisionMetadata m2 = ProjectRevisionMetadata::from_json(wire);
        CHECK(g, m2 == m);
        CHECK(g, m2.summary() == tricky);
        CHECK(g, m2.to_json() == wire);
    }
    // Explicit \uXXXX and surrogate-pair decoding on the input side.
    {
        const ProjectRevisionMetadata m = ProjectRevisionMetadata::from_json(
            "{\"revisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\","
            "\"origin\":\"ManualEdit\",\"summary\":\"a\\u0001b\\ud83d\\ude00\"}");
        CHECK(g, m.summary() ==
                     std::string("a") + static_cast<char>(1) +
                         std::string("b\xF0\x9F\x98\x80"));
    }
    // Malformed \u escapes are rejected.
    CHECK_THROWS(g, ProjectRevisionMetadata::from_json(
                        "{\"revisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\","
                        "\"origin\":\"ManualEdit\",\"summary\":\"\\u00zz\"}"),
                 std::invalid_argument);

    // Canonical JSON wire (exact text).
    {
        const ProjectRevisionMetadata m = ProjectRevisionMetadata::from_values(
            r1, std::nullopt, RevisionOrigin::ManualEdit,
            std::string("Initial manual correction of the soprano part."));
        CHECK(g, m.to_json() ==
                     "{\"revisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\","
                     "\"origin\":\"ManualEdit\","
                     "\"summary\":\"Initial manual correction of the soprano part.\"}");
    }
    {
        const ProjectRevisionMetadata m = ProjectRevisionMetadata::from_values(
            r2, std::optional<RevisionId>(r1), RevisionOrigin::Recognition,
            std::string("Re-recognition of page 2 after a model update."));
        CHECK(g, m.to_json() ==
                     "{\"revisionId\":\"f0e1d2c3-b4a5-4e6f-9a8b-7c6d5e4f3a2b\","
                     "\"parentRevisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\","
                     "\"origin\":\"Recognition\","
                     "\"summary\":\"Re-recognition of page 2 after a model update.\"}");
    }

    // JSON parse -> canonical reserialize == exact input (field order and
    // whitespace are accepted; output is canonical).
    {
        const std::string wire = m_no_parent_json();
        const ProjectRevisionMetadata m = ProjectRevisionMetadata::from_json(wire);
        CHECK(g, m.to_json() == wire);
    }
    {
        const std::string wire = m_with_parent_json();
        const ProjectRevisionMetadata m = ProjectRevisionMetadata::from_json(wire);
        CHECK(g, m.to_json() == wire);
    }
    // Field order and whitespace tolerance.
    {
        const ProjectRevisionMetadata m = ProjectRevisionMetadata::from_json(
            " { \"origin\" : \"ManualEdit\" , \"summary\" : \"x\" , \"revisionId\" : "
            "\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\" } ");
        CHECK(g, m.revision_id() == r1 && m.origin() == RevisionOrigin::ManualEdit);
    }

    // JSON rejections.
    CHECK_THROWS(g, ProjectRevisionMetadata::from_json("{\"origin\":\"ManualEdit\",\"summary\":\"x\"}"),
                 std::invalid_argument);  // missing revisionId
    CHECK_THROWS(g, ProjectRevisionMetadata::from_json("{\"revisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\",\"summary\":\"x\"}"),
                 std::invalid_argument);  // missing origin
    CHECK_THROWS(g, ProjectRevisionMetadata::from_json("{\"revisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\",\"origin\":\"ManualEdit\"}"),
                 std::invalid_argument);  // missing summary
    CHECK_THROWS(g, ProjectRevisionMetadata::from_json(
                        "{\"revisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\",\"origin\":\"ManualEdit\",\"summary\":\"x\",\"extra\":1}"),
                 std::invalid_argument);  // unknown field
    CHECK_THROWS(g, ProjectRevisionMetadata::from_json(
                        "{\"revisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\",\"revisionId\":\"f0e1d2c3-b4a5-4e6f-9a8b-7c6d5e4f3a2b\",\"origin\":\"ManualEdit\",\"summary\":\"x\"}"),
                 std::invalid_argument);  // duplicate field
    CHECK_THROWS(g, ProjectRevisionMetadata::from_json(
                        "{\"revisionId\":123,\"origin\":\"ManualEdit\",\"summary\":\"x\"}"),
                 std::invalid_argument);  // non-string value
    CHECK_THROWS(g, ProjectRevisionMetadata::from_json(
                        "{\"revisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\",\"origin\":\"Manualedit\",\"summary\":\"x\"}"),
                 std::invalid_argument);  // unknown origin
    CHECK_THROWS(g, ProjectRevisionMetadata::from_json(
                        "{\"revisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\",\"origin\":\"Foo\",\"summary\":\"x\"}"),
                 std::invalid_argument);  // unknown origin
    CHECK_THROWS(g, ProjectRevisionMetadata::from_json(
                        "{\"revisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\",\"parentRevisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\",\"origin\":\"ManualEdit\",\"summary\":\"x\"}"),
                 std::invalid_argument);  // self-parent
    CHECK_THROWS(g, ProjectRevisionMetadata::from_json(
                        "{\"revisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\",\"origin\":\"ManualEdit\",\"summary\":\"   \"}"),
                 std::invalid_argument);  // whitespace-only summary
    CHECK_THROWS(g, ProjectRevisionMetadata::from_json(
                        "{\"revisionId\":\"not-a-uuid\",\"origin\":\"ManualEdit\",\"summary\":\"x\"}"),
                 std::invalid_argument);  // malformed revisionId
    CHECK_THROWS(g, ProjectRevisionMetadata::from_json(""),
                 std::invalid_argument);  // malformed JSON
}

std::string m_no_parent_json()
{
    return "{\"revisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\","
           "\"origin\":\"ManualEdit\","
           "\"summary\":\"Initial manual correction of the soprano part.\"}";
}

std::string m_with_parent_json()
{
    return "{\"revisionId\":\"f0e1d2c3-b4a5-4e6f-9a8b-7c6d5e4f3a2b\","
           "\"parentRevisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\","
           "\"origin\":\"Recognition\","
           "\"summary\":\"Re-recognition of page 2 after a model update.\"}";
}

// ---------------------------------------------------------------------------
// compare_project_revisions: deterministic metadata comparison
// ---------------------------------------------------------------------------
void test_compare()
{
    const char* g = "compare";

    const RevisionId r1 = RevisionId::from_string("1a2b3c4d-5e6f-4a7b-8c9d-0e1f2a3b4c5d");
    const RevisionId r2 = RevisionId::from_string("c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f");

    const ProjectRevisionMetadata a = ProjectRevisionMetadata::from_values(
        r1, std::nullopt, RevisionOrigin::ManualEdit, std::string("alpha"));
    const ProjectRevisionMetadata b = ProjectRevisionMetadata::from_values(
        r1, std::nullopt, RevisionOrigin::ManualEdit, std::string("alpha"));

    // Identical metadata compare equal; deterministic across repeated calls.
    CHECK(g, compare_project_revisions(a, b) == std::strong_ordering::equal);
    CHECK(g, compare_project_revisions(a, b) == compare_project_revisions(a, b));

    // revisionId order (defined byte order).
    const ProjectRevisionMetadata c = ProjectRevisionMetadata::from_values(
        r2, std::nullopt, RevisionOrigin::ManualEdit, std::string("alpha"));
    CHECK(g, compare_project_revisions(a, c) != std::strong_ordering::equal);
    // Antisymmetry.
    CHECK(g, compare_project_revisions(a, c) == std::strong_ordering::less &&
                 compare_project_revisions(c, a) == std::strong_ordering::greater);

    // parentRevisionId: absent < present; then byte order.
    const ProjectRevisionMetadata d = ProjectRevisionMetadata::from_values(
        r1, std::optional<RevisionId>(r2), RevisionOrigin::ManualEdit,
        std::string("alpha"));
    CHECK(g, compare_project_revisions(a, d) == std::strong_ordering::less);
    CHECK(g, compare_project_revisions(d, a) == std::strong_ordering::greater);

    // origin order: ManualEdit < Recognition < Migration.
    const ProjectRevisionMetadata e = ProjectRevisionMetadata::from_values(
        r1, std::nullopt, RevisionOrigin::Recognition, std::string("alpha"));
    CHECK(g, compare_project_revisions(a, e) == std::strong_ordering::less);
    const ProjectRevisionMetadata f = ProjectRevisionMetadata::from_values(
        r1, std::nullopt, RevisionOrigin::Migration, std::string("alpha"));
    CHECK(g, compare_project_revisions(e, f) == std::strong_ordering::less);

    // summary order: lexicographic.
    const ProjectRevisionMetadata gmeta = ProjectRevisionMetadata::from_values(
        r1, std::nullopt, RevisionOrigin::ManualEdit, std::string("beta"));
    CHECK(g, compare_project_revisions(a, gmeta) == std::strong_ordering::less);
    CHECK(g, compare_project_revisions(gmeta, a) == std::strong_ordering::greater);
}

// ---------------------------------------------------------------------------
// Minimal local durable reopen: serialize canonical EntityId + Revision
// metadata JSON to a temporary test file, destroy/replace the in-memory
// values, reopen/read/parse, and prove exact equality. This proves primitive
// durable serialization/reopen only - NOT SQLite migration or crash-safe
// transactions. Deterministic and offline.
// ---------------------------------------------------------------------------
void test_durable_reopen()
{
    const char* g = "reopen";
    namespace fs = std::filesystem;

    const fs::path temp = fs::temp_directory_path() / "choirloom_m0003_reopen_test.json";
    std::error_code ec;
    fs::remove(temp, ec);  // start clean

    const EntityId entity = EntityId::from_string("8f14e45f-ceea-4a6a-b5e2-9d5c9a1f3b27");
    const RevisionId rev = RevisionId::from_string("c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f");
    const RevisionId parent = RevisionId::from_string("f0e1d2c3-b4a5-4e6f-9a8b-7c6d5e4f3a2b");
    const ProjectRevisionMetadata meta = ProjectRevisionMetadata::from_values(
        rev, std::optional<RevisionId>(parent), RevisionOrigin::Recognition,
        std::string("durable reopen check"));
    const std::string entity_wire = entity.to_string();
    const std::string meta_wire = meta.to_json();

    // Write the canonical serializations to the temporary file.
    {
        std::ofstream out(temp, std::ios::binary);
        CHECK(g, static_cast<bool>(out));
        out << entity_wire << "\n" << meta_wire << "\n";
    }

    // Destroy/replace the in-memory values.
    EntityId reopened_entity = EntityId::from_string("a3c9d2e1-4f5a-4b6c-8d7e-9f0a1b2c3d4e");
    ProjectRevisionMetadata reopened_meta = ProjectRevisionMetadata::from_values(
        rev, std::nullopt, RevisionOrigin::ManualEdit, "placeholder");

    // Reopen / read / parse; identity and metadata must be exactly preserved.
    {
        std::ifstream in(temp, std::ios::binary);
        CHECK(g, static_cast<bool>(in));
        std::string line1, line2;
        std::getline(in, line1);
        std::getline(in, line2);
        reopened_entity = EntityId::from_string(line1);
        reopened_meta = ProjectRevisionMetadata::from_json(line2);
    }
    CHECK(g, reopened_entity == entity);
    CHECK(g, reopened_entity.to_string() == entity_wire);
    CHECK(g, reopened_meta == meta);
    CHECK(g, reopened_meta.to_json() == meta_wire);

    // Malformed / truncated on-disk content must fail to parse.
    {
        std::ofstream out(temp, std::ios::binary);
        out << "8f14e45f-ceea-4a6a";  // truncated entity id
    }
    {
        std::ifstream in(temp, std::ios::binary);
        std::string truncated;
        std::getline(in, truncated);
        CHECK_THROWS(g, EntityId::from_string(truncated), std::invalid_argument);
    }
    {
        std::ofstream out(temp, std::ios::binary);
        out << "{\"revisionId\":\"c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f\",\"origin\":\"ManualEdit\"";
    }
    {
        std::ifstream in(temp, std::ios::binary);
        std::string truncated;
        std::getline(in, truncated);
        CHECK_THROWS(g, ProjectRevisionMetadata::from_json(truncated), std::invalid_argument);
    }

    fs::remove(temp, ec);
}

// ---------------------------------------------------------------------------
// Golden fixture: read, validate envelope/value contracts, deserialize,
// canonical reserialize, exact compare, canonical uniqueness.
// ---------------------------------------------------------------------------
void test_golden_fixture()
{
    const char* g = "golden";
    const std::string root = test_root();
    const std::string fixture_path = root + "/tests/golden/entity_revision.samples.json";
    const std::string entity_schema_path = root + "/schemas/score/entity-id.schema.json";
    const std::string revision_schema_path = root + "/schemas/score/revision-id.schema.json";
    const std::string metadata_schema_path = root + "/schemas/score/project-revision-metadata.schema.json";
    const std::string collection_schema_path = root + "/schemas/score/entity-revision-collection.schema.json";

    bool ok = false;
    const std::string fixture_text = read_text_file(fixture_path, ok);
    CHECK(g, ok && !fixture_text.empty());

    test_json::Node fixture;
    bool parsed = true;
    try {
        fixture = test_json::parse(fixture_text);
    } catch (test_json::ParseError const&) {
        parsed = false;
    }
    CHECK(g, parsed && fixture.is_object());
    if (!parsed || !fixture.is_object()) {
        return;
    }

    // Envelope: $schema points at the collection schema.
    const test_json::Node* schema_ref = fixture.find("$schema");
    CHECK(g, schema_ref != nullptr && schema_ref->is_string() &&
              schema_ref->string.find("entity-revision-collection.schema.json") != std::string::npos);
    CHECK(g, fixture.object.size() <= 3);  // $schema, description, cases
    const test_json::Node* cases = fixture.find("cases");
    CHECK(g, cases != nullptr && cases->is_array() && !cases->array.empty());

    // Load schemas (contract source).
    const std::string entity_schema_text = read_text_file(entity_schema_path, ok);
    CHECK(g, ok && !entity_schema_text.empty());
    const std::string revision_schema_text = read_text_file(revision_schema_path, ok);
    CHECK(g, ok && !revision_schema_text.empty());
    const std::string metadata_schema_text = read_text_file(metadata_schema_path, ok);
    CHECK(g, ok && !metadata_schema_text.empty());
    const std::string collection_schema_text = read_text_file(collection_schema_path, ok);
    CHECK(g, ok && !collection_schema_text.empty());

    test_json::Node entity_schema, revision_schema, metadata_schema, collection_schema;
    try {
        entity_schema = test_json::parse(entity_schema_text);
        revision_schema = test_json::parse(revision_schema_text);
        metadata_schema = test_json::parse(metadata_schema_text);
        collection_schema = test_json::parse(collection_schema_text);
    } catch (test_json::ParseError const&) {
        CHECK(g, false);
        return;
    }

    // Schema contracts.
    const std::string entity_pattern = test_json::child_string(&entity_schema, "pattern");
    const std::string revision_pattern = test_json::child_string(&revision_schema, "pattern");
    CHECK(g, !entity_pattern.empty() && !revision_pattern.empty());
    CHECK(g, entity_pattern == revision_pattern);
    CHECK(g, test_json::child_string(&entity_schema, "type") == "string");

    const test_json::Node* m_req = metadata_schema.find("required");
    CHECK(g, m_req != nullptr && m_req->is_array() && m_req->array.size() == 3);
    CHECK(g, test_json::child_string(&metadata_schema, "type") == "object");
    const test_json::Node* m_props = metadata_schema.find("properties");
    CHECK(g, m_props != nullptr && m_props->is_object());
    const test_json::Node* origin_p = (m_props != nullptr) ? m_props->find("origin") : nullptr;
    const test_json::Node* origin_enum = (origin_p != nullptr && origin_p->is_object()) ? origin_p->find("enum") : nullptr;
    std::vector<std::string> origin_values;
    if (origin_enum != nullptr && origin_enum->is_array()) {
        for (auto const& v : origin_enum->array) {
            if (v.is_string()) {
                origin_values.push_back(v.string);
            }
        }
    }
    CHECK(g, origin_values.size() == 3);  // ManualEdit, Recognition, Migration
    CHECK(g, collection_schema_text.find("entity-revision-collection") != std::string::npos);

    const std::regex id_re(entity_pattern);
    std::vector<std::string> entity_wire;   // entity id space
    std::vector<std::string> revision_wire; // revision id space
    std::size_t count = 0;

    for (auto const& c : cases->array) {
        ++count;
        CHECK(g, c.is_object());
        const test_json::Node* name = c.find("name");
        const test_json::Node* kind = c.find("kind");
        const test_json::Node* note = c.find("note");
        const test_json::Node* value = c.find("value");
        CHECK(g, name != nullptr && name->is_string() && !name->string.empty());
        CHECK(g, note == nullptr || note->is_string());
        CHECK(g, value != nullptr);
        CHECK(g, c.object.size() <= 4);  // name, kind, note, value
        if (kind == nullptr || !kind->is_string() || value == nullptr) {
            continue;
        }

        if (kind->string == "entity-id" || kind->string == "revision-id") {
            CHECK(g, value->is_string());
            if (!value->is_string()) {
                continue;
            }
            // Syntactic: schema pattern.
            CHECK(g, std::regex_match(value->string, id_re));
            if (kind->string == "entity-id") {
                entity_wire.push_back(value->string);
            } else {
                revision_wire.push_back(value->string);
            }
            // Semantic + exact round-trip through the core.
            if (kind->string == "entity-id") {
                EntityId id = EntityId::from_string("8f14e45f-ceea-4a6a-b5e2-9d5c9a1f3b27");
                bool ok_parse = true;
                try {
                    id = EntityId::from_string(value->string);
                } catch (...) {
                    ok_parse = false;
                }
                CHECK(g, ok_parse);
                CHECK(g, id.to_string() == value->string);
            } else {
                RevisionId id = RevisionId::from_string("c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f");
                bool ok_parse = true;
                try {
                    id = RevisionId::from_string(value->string);
                } catch (...) {
                    ok_parse = false;
                }
                CHECK(g, ok_parse);
                CHECK(g, id.to_string() == value->string);
            }
        } else if (kind->string == "project-revision-metadata") {
            CHECK(g, value->is_object());
            if (!value->is_object()) {
                continue;
            }
            const test_json::Node* rev = value->find("revisionId");
            const test_json::Node* parent = value->find("parentRevisionId");
            const test_json::Node* origin = value->find("origin");
            const test_json::Node* summary = value->find("summary");
            CHECK(g, rev != nullptr && rev->is_string());
            CHECK(g, origin != nullptr && origin->is_string());
            CHECK(g, summary != nullptr && summary->is_string() && !summary->string.empty());
            CHECK(g, value->object.size() <= 4);  // strict metadata object
            if (rev == nullptr || origin == nullptr || summary == nullptr ||
                !rev->is_string() || !origin->is_string() || !summary->is_string()) {
                continue;
            }
            CHECK(g, std::regex_match(rev->string, id_re));
            revision_wire.push_back(rev->string);
            if (parent != nullptr) {
                CHECK(g, parent->is_string());
                if (parent->is_string()) {
                    CHECK(g, std::regex_match(parent->string, id_re));
                    revision_wire.push_back(parent->string);
                }
            }
            // Origin must be one of the schema enum values.
            bool origin_known = false;
            for (auto const& ov : origin_values) {
                if (ov == origin->string) {
                    origin_known = true;
                    break;
                }
            }
            CHECK(g, origin_known);

            // Canonical wire reconstruction -> deserialize -> exact reserialize.
            std::string wire = "{\"revisionId\":\"" + rev->string + "\"";
            if (parent != nullptr && parent->is_string()) {
                wire += ",\"parentRevisionId\":\"" + parent->string + "\"";
            }
            wire += ",\"origin\":\"" + origin->string + "\",\"summary\":\"" + summary->string + "\"}";
            ProjectRevisionMetadata meta = ProjectRevisionMetadata::from_values(
                RevisionId::from_string("c1d2e3f4-a5b6-4c7d-8e9f-0a1b2c3d4e5f"),
                std::nullopt, RevisionOrigin::ManualEdit, "placeholder");
            bool decoded = true;
            try {
                meta = ProjectRevisionMetadata::from_json(wire);
            } catch (...) {
                decoded = false;
            }
            CHECK(g, decoded);
            if (decoded) {
                CHECK(g, meta.to_json() == wire);
            }
        } else {
            CHECK(g, false);  // unknown kind
        }
    }
    CHECK(g, count == cases->array.size());
    CHECK(g, count >= 9);

    // Canonical uniqueness: entity ids are all distinct and disjoint from the
    // revision id space. Revision ids are reused by design (metadata cases
    // reference the same identities as the standalone revision-id cases), so
    // the distinct revision id set must equal exactly the 3 intended fixture
    // revision ids.
    {
        auto has_dup = [](std::vector<std::string> const& v) {
            for (std::size_t i = 0; i < v.size(); ++i) {
                for (std::size_t j = i + 1; j < v.size(); ++j) {
                    if (v[i] == v[j]) {
                        return true;
                    }
                }
            }
            return false;
        };
        CHECK(g, !has_dup(entity_wire));
        CHECK(g, entity_wire.size() == 3);
        bool disjoint = true;
        for (auto const& e : entity_wire) {
            for (auto const& r : revision_wire) {
                if (e == r) {
                    disjoint = false;
                }
            }
        }
        CHECK(g, disjoint);
        std::vector<std::string> distinct_revisions;
        for (auto const& r : revision_wire) {
            if (std::find(distinct_revisions.begin(), distinct_revisions.end(), r) ==
                distinct_revisions.end()) {
                distinct_revisions.push_back(r);
            }
        }
        CHECK(g, distinct_revisions.size() == 3);
        CHECK(g, revision_wire.size() >= 5);  // 3 standalone + metadata references
    }
}

}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        test_wire();
        test_uniqueness();
        test_equality_hash();
        test_parse_errors();
        test_metadata();
        test_compare();
        test_durable_reopen();
        test_golden_fixture();
    } catch (std::exception const& e) {
        std::printf("UNCAUGHT EXCEPTION: %s\n", e.what());
        return 2;
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
