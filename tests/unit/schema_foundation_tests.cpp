// ============================================================================
// tests/unit/schema_foundation_tests.cpp - unit tests for M0-004 Schema /
// Versioning Foundation.
//
// Self-contained, dependency-free runner. When built through CMake,
// CHOIRLOOM_TEST_SOURCE_DIR is an absolute repository root so the catalog,
// schema files, and golden fixture resolve from the build tree; when compiled
// directly, run from the repository root.
//
// Coverage: strict version/id/document-id parsing + ordering, compile-time
// nominal separation from RevisionId/EntityId/RationalTime, real
// source-of-truth catalog loading + deterministic iteration + structural and
// file-backed validation (six pre-existing schemas registered; on-disk
// $id/version match; local $ref resolution), structured diagnostics for
// duplicates/traversal/absolute/missing/mismatch/future/unknown, explicit
// directional compatibility, deterministic migration planning, and the golden
// fixture (parse / validate / apply every case).
// ============================================================================

#include "entity_revision.h"
#include "rational_time.h"
#include "schema_foundation.h"
#include "test_json.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef CHOIRLOOM_TEST_SOURCE_DIR
#define CHOIRLOOM_TEST_SOURCE_DIR "."
#endif

using choirloom::score::CatalogLookupResult;
using choirloom::score::Compatibility;
using choirloom::score::CompatibilityResult;
using choirloom::score::DiagnosticCode;
using choirloom::score::EntityId;
using choirloom::score::inspect_schema_version;
using choirloom::score::lookup_schema;
using choirloom::score::MigrationPlan;
using choirloom::score::plan_migration;
using choirloom::score::RationalTime;
using choirloom::score::RevisionId;
using choirloom::score::schema_compatibility;
using choirloom::score::SchemaCatalog;
using choirloom::score::SchemaCatalogBuilder;
using choirloom::score::SchemaCatalogEntry;
using choirloom::score::SchemaCatalogLoadResult;
using choirloom::score::SchemaCatalogRule;
using choirloom::score::SchemaDocumentId;
using choirloom::score::SchemaId;
using choirloom::score::SchemaRuleKind;
using choirloom::score::SchemaStatus;
using choirloom::score::SchemaVersion;
using choirloom::score::validate_catalog;
using choirloom::score::validate_catalog_files;
using choirloom::score::ValidationDiagnostic;
using choirloom::score::VersionInspectionResult;

// ---------------------------------------------------------------------------
// Compile-time nominal separation.
// ---------------------------------------------------------------------------
namespace {
template <typename T, typename = void>
struct has_less : std::false_type {};
template <typename T>
struct has_less<T, std::void_t<decltype(std::declval<T>() < std::declval<T>())>>
    : std::true_type {};

template <typename A, typename B, typename = void>
struct has_eq : std::false_type {};
template <typename A, typename B>
struct has_eq<A, B, std::void_t<decltype(std::declval<A>() == std::declval<B>())>>
    : std::true_type {};
}  // namespace

static_assert(!std::is_same_v<SchemaId, SchemaVersion>);
static_assert(!std::is_same_v<SchemaVersion, SchemaDocumentId>);
static_assert(!std::is_same_v<SchemaId, SchemaDocumentId>);
// SchemaVersion must not construct/convert/compare with RevisionId.
static_assert(!std::is_constructible_v<SchemaVersion, RevisionId>);
static_assert(!std::is_constructible_v<RevisionId, SchemaVersion>);
static_assert(!std::is_convertible_v<SchemaVersion, RevisionId>);
static_assert(!std::is_convertible_v<RevisionId, SchemaVersion>);
static_assert(!has_eq<SchemaVersion, RevisionId>::value);
static_assert(!has_eq<SchemaId, RevisionId>::value);
static_assert(!has_eq<SchemaDocumentId, RevisionId>::value);
// Also separate from EntityId and RationalTime.
static_assert(!has_eq<SchemaVersion, EntityId>::value);
static_assert(!has_eq<SchemaVersion, RationalTime>::value);
static_assert(!has_eq<SchemaId, RationalTime>::value);

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

bool has_code(std::vector<ValidationDiagnostic> const& diags, DiagnosticCode code)
{
    for (auto const& d : diags) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

bool version_throws(std::string const& s)
{
    try {
        (void)SchemaVersion::from_string(s);
        return false;
    } catch (std::exception const&) {
        return true;
    }
}

SchemaCatalogEntry make_entry(std::string const& schema_id,
                              std::string const& version,
                              std::string const& path)
{
    const SchemaId id = SchemaId::from_string(schema_id);
    const SchemaVersion v = SchemaVersion::from_string(version);
    return SchemaCatalogEntry{
        id, v,
        SchemaDocumentId::from_parts(id, v),
        path, SchemaStatus::Draft};
}

// ---------------------------------------------------------------------------
// Strict parsing / ordering
// ---------------------------------------------------------------------------
void test_version()
{
    const char* g = "version";

    const struct {
        const char* wire;
        std::uint32_t major;
        std::uint32_t minor;
        std::uint32_t patch;
    } kValid[] = {
        {"0.0.0", 0, 0, 0},
        {"0.1.0", 0, 1, 0},
        {"1.2.3", 1, 2, 3},
        {"4294967295.0.0", 4294967295u, 0, 0},
    };
    for (auto const& c : kValid) {
        const SchemaVersion v = SchemaVersion::from_string(c.wire);
        CHECK(g, v.to_string() == std::string(c.wire));
        CHECK(g, v.major() == c.major && v.minor() == c.minor && v.patch() == c.patch);
        CHECK(g, SchemaVersion::from_string(v.to_string()) == v);
    }

    // Ordering is component-wise.
    const SchemaVersion v0 = SchemaVersion::from_string("0.1.0");
    const SchemaVersion v1 = SchemaVersion::from_string("0.2.0");
    const SchemaVersion v2 = SchemaVersion::from_string("1.0.0");
    CHECK(g, v0 < v1 && v1 < v2);
    CHECK(g, SchemaVersion::from_string("0.1.1") > v0 && SchemaVersion::from_string("0.1.1") < v1);
    CHECK(g, SchemaVersion::from_string("10.0.0") > v2);
    CHECK(g, v0 == SchemaVersion::from_components(0, 1, 0));

    // Malformed / non-canonical rejections (no silent normalization).
    // Grammar violations throw std::invalid_argument; uint32 overflow throws
    // std::out_of_range.
    const char* kBad[] = {"01.0.0", "1.2", "1.2.3.4", "1.2.3-beta", "1.2.3+build",
                          "-1.0.0", "", "1.2.3-beta.1", "0.0"};
    for (char const* s : kBad) {
        CHECK(g, version_throws(s));
    }
    CHECK_THROWS(g, SchemaVersion::from_string("01.0.0"), std::invalid_argument);
    CHECK_THROWS(g, SchemaVersion::from_string("1.2.3-beta"), std::invalid_argument);
    CHECK_THROWS(g, SchemaVersion::from_string("4294967296.0.0"), std::out_of_range);  // overflow
    CHECK_THROWS(g, SchemaVersion::from_string("0.4294967296.0"), std::out_of_range);
    // Boundary: exactly uint32 max is accepted.
    CHECK(g, SchemaVersion::from_string("4294967295.0.0").major() == 4294967295u);
}

void test_ids()
{
    const char* g = "ids";

    const SchemaId rt = SchemaId::from_string("choirloom:score/rational-time");
    CHECK(g, rt.to_string() == "choirloom:score/rational-time");
    CHECK(g, SchemaId::from_string(rt.to_string()) == rt);
    CHECK(g, SchemaId::from_string("choirloom:score/a1-b2").to_string() ==
                  "choirloom:score/a1-b2");

    const char* kBadId[] = {"rational-time", "choirloom:score/", "choirloom:score/UPPER",
                            "choirloom:score/a--b", "choirloom:score/-a", ""};
    for (char const* s : kBadId) {
        CHECK_THROWS(g, SchemaId::from_string(s), std::invalid_argument);
    }

    const SchemaDocumentId doc = SchemaDocumentId::from_string("choirloom:score/rational-time/0.1.0");
    CHECK(g, doc.to_string() == "choirloom:score/rational-time/0.1.0");
    CHECK(g, doc.schema_id() == rt);
    CHECK(g, doc.schema_version() == SchemaVersion::from_string("0.1.0"));
    CHECK(g, SchemaDocumentId::from_string(doc.to_string()) == doc);
    CHECK(g, SchemaDocumentId::from_parts(rt, SchemaVersion::from_string("0.2.0")).to_string() ==
                  "choirloom:score/rational-time/0.2.0");

    const char* kBadDoc[] = {"choirloom:score/rational-time", "choirloom:score/rational-time/0.1",
                             "choirloom:score/rational-time/01.0.0", "choirloom:score/rational-time/1.2.3.4",
                             "choirloom:score//0.1.0", ""};
    for (char const* s : kBadDoc) {
        CHECK_THROWS(g, SchemaDocumentId::from_string(s), std::invalid_argument);
    }
}

// ---------------------------------------------------------------------------
// Real source-of-truth catalog: load, iterate deterministically, validate
// structurally and file-backed.
// ---------------------------------------------------------------------------
void test_real_catalog()
{
    const char* g = "catalog";
    const std::string root = test_root();
    const std::string catalog_path = root + "/schemas/schema-catalog.json";

    bool ok = false;
    const std::string catalog_text = read_text_file(catalog_path, ok);
    CHECK(g, ok && !catalog_text.empty());

    const SchemaCatalogLoadResult loaded = SchemaCatalog::from_json(catalog_text);
    CHECK(g, loaded.catalog.has_value());
    CHECK(g, loaded.diagnostics.empty());
    if (!loaded.catalog.has_value()) {
        return;
    }
    const SchemaCatalog& catalog = *loaded.catalog;

    // Catalog INSTANCE identity: the instance's $schema binds it to the
    // catalog schema document (choirloom:score/schema-catalog/0.1.0), and the
    // instance carries no $id of its own, so there is no document-identity
    // conflict with schema-catalog.schema.json.
    {
        const test_json::Node catalog_root = test_json::parse(catalog_text);
        const test_json::Node* schema_binding = catalog_root.find("$schema");
        CHECK(g, schema_binding != nullptr && schema_binding->is_string() &&
                     schema_binding->string == "choirloom:score/schema-catalog/0.1.0");
        CHECK(g, catalog_root.find("$id") == nullptr);  // no instance $id

        const std::string schema_doc_path = root + "/schemas/score/schema-catalog.schema.json";
        bool schema_ok = false;
        const std::string schema_doc_text = read_text_file(schema_doc_path, schema_ok);
        CHECK(g, schema_ok && !schema_doc_text.empty());
        const test_json::Node schema_doc = test_json::parse(schema_doc_text);
        const test_json::Node* doc_id = schema_doc.find("$id");
        CHECK(g, doc_id != nullptr && doc_id->is_string() &&
                     doc_id->string == "choirloom:score/schema-catalog/0.1.0");
        // The catalog schema must declare exactly the fields the parser
        // requires: $schema, catalogFormatVersion, entries.
        const test_json::Node* required = schema_doc.find("required");
        CHECK(g, required != nullptr && required->is_array() && required->array.size() == 3);
        bool req_schema = false, req_fmt = false, req_entries = false;
        if (required != nullptr && required->is_array()) {
            for (auto const& r : required->array) {
                if (r.is_string() && r.string == "$schema") {
                    req_schema = true;
                }
                if (r.is_string() && r.string == "catalogFormatVersion") {
                    req_fmt = true;
                }
                if (r.is_string() && r.string == "entries") {
                    req_entries = true;
                }
            }
        }
        CHECK(g, req_schema && req_fmt && req_entries);
    }

    // Deterministic iteration/order: sorted by schemaId then version; repeatable.
    std::vector<std::string> first_order;
    for (auto const& e : catalog.entries()) {
        first_order.push_back(e.document_id.to_string());
    }
    std::vector<std::string> second_order;
    for (auto const& e : catalog.entries()) {
        second_order.push_back(e.document_id.to_string());
    }
    CHECK(g, first_order == second_order);
    CHECK(g, first_order.size() == 11);
    // Deterministic order is by (schemaId, then schemaVersion) - not by
    // documentId string order ('/' vs '-' separators differ).
    {
        bool ordered = true;
        for (std::size_t i = 1; i < catalog.entries().size(); ++i) {
            auto const& a = catalog.entries()[i - 1];
            auto const& b = catalog.entries()[i];
            if (a.schema_id > b.schema_id ||
                (a.schema_id == b.schema_id && a.schema_version > b.schema_version)) {
                ordered = false;
            }
        }
        CHECK(g, ordered);
    }

    // All six pre-existing schemas are registered.
    const char* kSix[] = {
        "choirloom:score/rational-time/0.1.0",
        "choirloom:score/rational-time-collection/0.1.0",
        "choirloom:score/entity-id/0.1.0",
        "choirloom:score/revision-id/0.1.0",
        "choirloom:score/project-revision-metadata/0.1.0",
        "choirloom:score/entity-revision-collection/0.1.0",
    };
    for (char const* doc : kSix) {
        const SchemaDocumentId d = SchemaDocumentId::from_string(doc);
        const CatalogLookupResult r = lookup_schema(catalog, d.schema_id(), d.schema_version());
        CHECK(g, r.entry.has_value());
        CHECK(g, r.diagnostics.empty());
    }

    // Exact-tuple lookup; unknown/future produce distinct diagnostics.
    {
        const CatalogLookupResult r = lookup_schema(
            catalog, SchemaId::from_string("choirloom:score/rational-time"),
            SchemaVersion::from_string("0.1.0"));
        CHECK(g, r.entry.has_value() && r.diagnostics.empty());
    }
    {
        const CatalogLookupResult r = lookup_schema(
            catalog, SchemaId::from_string("choirloom:score/does-not-exist"),
            SchemaVersion::from_string("0.1.0"));
        CHECK(g, !r.entry.has_value());
        CHECK(g, !r.diagnostics.empty() &&
                     r.diagnostics[0].code == DiagnosticCode::UnknownSchema);
    }
    {
        const CatalogLookupResult r = lookup_schema(
            catalog, SchemaId::from_string("choirloom:score/rational-time"),
            SchemaVersion::from_string("99.0.0"));
        CHECK(g, !r.entry.has_value());
        CHECK(g, !r.diagnostics.empty() &&
                     r.diagnostics[0].code == DiagnosticCode::UnsupportedFutureVersion);
    }

    // Structural validation is clean for the source of truth.
    CHECK(g, validate_catalog(catalog).empty());

    // File-backed validation is clean: files exist, on-disk $id/version match,
    // local $refs resolve.
    const std::vector<ValidationDiagnostic> file_diags =
        validate_catalog_files(catalog, root);
    for (auto const& d : file_diags) {
        std::printf("  catalog file diag: %s :: %s\n",
                    std::string(choirloom::score::to_string(d.code)).c_str(),
                    d.message.c_str());
    }
    CHECK(g, file_diags.empty());
}

// ---------------------------------------------------------------------------
// Structured diagnostics for malformed catalogs (duplicates, traversal,
// absolute paths, mismatched documentId, unknown rule target).
// ---------------------------------------------------------------------------
void test_catalog_diagnostics()
{
    const char* g = "diags";

    SchemaCatalogBuilder b;
    b.catalog_format_version(1)
        .add_entry(make_entry("choirloom:score/a", "0.1.0", "schemas/score/a.schema.json"))
        .add_entry(make_entry("choirloom:score/a", "0.1.0", "schemas/score/a.schema.json"))  // dup tuple + path + docId
        .add_entry(make_entry("choirloom:score/b", "0.1.0", "C:/absolute/b.schema.json"))    // absolute
        .add_entry(make_entry("choirloom:score/c", "0.1.0", "schemas/../escape.schema.json")) // traversal
        .add_entry(SchemaCatalogEntry{
            SchemaId::from_string("choirloom:score/d"),
            SchemaVersion::from_string("0.1.0"),
            SchemaDocumentId::from_string("choirloom:score/d/0.2.0"),  // version mismatch
            "schemas/score/d.schema.json", SchemaStatus::Draft})
        .add_rule(SchemaCatalogRule{
            SchemaDocumentId::from_string("choirloom:score/zzz/0.1.0"),
            SchemaDocumentId::from_string("choirloom:score/aaa/0.1.0"),
            SchemaRuleKind::Migration});
    const SchemaCatalog bad = b.build();

    const std::vector<ValidationDiagnostic> diags = validate_catalog(bad);
    CHECK(g, has_code(diags, DiagnosticCode::DuplicateSchemaEntry));
    CHECK(g, has_code(diags, DiagnosticCode::DuplicateDocumentId));
    CHECK(g, has_code(diags, DiagnosticCode::DuplicateSchemaPath));
    CHECK(g, has_code(diags, DiagnosticCode::AbsoluteSchemaPath));
    CHECK(g, has_code(diags, DiagnosticCode::PathTraversal));
    CHECK(g, has_code(diags, DiagnosticCode::DocumentIdVersionMismatch));
    CHECK(g, has_code(diags, DiagnosticCode::RuleTargetUnknown));
    // The valid entry has no mismatch diagnostics for itself.
    CHECK(g, !has_code(diags, DiagnosticCode::DocumentIdSchemaIdMismatch));

    // Missing file + on-disk mismatch (file-backed).
    namespace fs = std::filesystem;
    const fs::path temp = fs::temp_directory_path() / "choirloom_m0004_filecheck";
    std::error_code ec;
    fs::remove_all(temp, ec);
    fs::create_directories(temp / "schemas" / "score", ec);

    {
        std::ofstream out(temp / "schemas" / "score" / "missing-id.schema.json");
        out << "{\"$id\":\"choirloom:score/wrong/0.1.0\",\"version\":\"0.1.0\"}";
    }
    SchemaCatalogBuilder missing;
    missing.catalog_format_version(1)
        .add_entry(make_entry("choirloom:score/nofile", "0.1.0", "schemas/score/nofile.schema.json"))
        .add_entry(make_entry("choirloom:score/other", "0.1.0", "schemas/score/missing-id.schema.json"))
        .add_entry(make_entry("choirloom:score/ref", "0.1.0", "schemas/score/badref.schema.json"));
    {
        std::ofstream out(temp / "schemas" / "score" / "badref.schema.json");
        out << "{\"$id\":\"choirloom:score/ref/0.1.0\",\"version\":\"0.1.0\","
               "\"properties\":{\"x\":{\"$ref\":\"choirloom:score/not-registered/0.1.0\"}}}";
    }
    const SchemaCatalog missing_catalog = missing.build();
    const std::vector<ValidationDiagnostic> mdiags =
        validate_catalog_files(missing_catalog, temp.string());
    CHECK(g, has_code(mdiags, DiagnosticCode::MissingSchemaFile));
    CHECK(g, has_code(mdiags, DiagnosticCode::OnDiskIdMismatch));
    CHECK(g, has_code(mdiags, DiagnosticCode::UnregisteredRef));
    fs::remove_all(temp, ec);

    // Determinism: repeated inspection gives identical results.
    CHECK(g, validate_catalog(bad) == validate_catalog(bad));

    // validate_catalog_files enforces repo-root containment BEFORE any file
    // I/O, even when invoked directly (no prior validate_catalog call).
    {
        SchemaCatalogBuilder f;
        f.catalog_format_version(1)
            .add_entry(make_entry("choirloom:score/trav", "0.1.0", "schemas/../escape.schema.json"))
            .add_entry(make_entry("choirloom:score/abs", "0.1.0", "C:/absolute/x.schema.json"))
            .add_entry(make_entry("choirloom:score/back", "0.1.0", "schemas\\score\\back.schema.json"));
        const SchemaCatalog fc = f.build();
        const std::vector<ValidationDiagnostic> fd = validate_catalog_files(fc, test_root());
        CHECK(g, has_code(fd, DiagnosticCode::AbsoluteSchemaPath));
        int traversal_count = 0;
        for (auto const& d : fd) {
            if (d.code == DiagnosticCode::PathTraversal) {
                ++traversal_count;
            }
        }
        CHECK(g, traversal_count >= 2);  // '..' traversal + backslash
    }

    // Symlink/junction escape: the canonical containment check must reject a
    // target that resolves OUTSIDE the repo root. Creating a directory
    // symlink/junction requires platform permissions; when unavailable the
    // test is skipped with a clear reason (the lexical and canonical
    // containment helpers are still exercised by the tests above).
    {
        namespace fs = std::filesystem;
        const fs::path t2 = fs::temp_directory_path() / "choirloom_m0004_symlink";
        std::error_code sec;
        fs::remove_all(t2, sec);
        fs::create_directories(t2 / "root" / "schemas" / "score", sec);
        const fs::path outside_dir = t2 / "outside";
        fs::create_directories(outside_dir, sec);
        std::ofstream out(outside_dir / "x.schema.json");
        out << "{}";
        std::error_code link_ec;
        fs::create_directory_symlink(outside_dir, t2 / "root" / "schemas" / "score" / "escape", link_ec);
        if (link_ec) {
            std::printf("  note: directory symlink creation unsupported here (%s); symlink escape test skipped with clear reason.\n",
                        link_ec.message().c_str());
        } else {
            SchemaCatalogBuilder sb;
            sb.catalog_format_version(1).add_entry(
                make_entry("choirloom:score/e", "0.1.0", "schemas/score/escape/x.schema.json"));
            const SchemaCatalog sc = sb.build();
            const std::vector<ValidationDiagnostic> sd =
                validate_catalog_files(sc, (t2 / "root").string());
            CHECK(g, has_code(sd, DiagnosticCode::PathTraversal));
        }
        fs::remove_all(t2, sec);
    }
}

// ---------------------------------------------------------------------------
// Strict catalog parser: duplicate fields, unknown fields, wrong types,
// malformed/unsupported format are all rejected before a catalog is returned.
// ---------------------------------------------------------------------------
void test_catalog_parse_strict()
{
    const char* g = "parse";
    // A catalog INSTANCE must bind its schema document via "$schema".
    const std::string schema_prefix = "\"$schema\":\"choirloom:score/schema-catalog/0.1.0\",";
    const std::string entry_json =
        "\"schemaId\":\"choirloom:score/a\",\"schemaVersion\":\"0.1.0\","
        "\"documentId\":\"choirloom:score/a/0.1.0\","
        "\"path\":\"schemas/score/a.schema.json\",\"status\":\"Draft\"";
    const std::string valid = "{" + schema_prefix + "\"catalogFormatVersion\":1,\"entries\":[{" + entry_json + "}]}";

    {
        const SchemaCatalogLoadResult r = SchemaCatalog::from_json(valid);
        CHECK(g, r.catalog.has_value() && r.diagnostics.empty());
    }
    // Unsupported catalog format -> no catalog.
    {
        const SchemaCatalogLoadResult r =
            SchemaCatalog::from_json("{" + schema_prefix + "\"catalogFormatVersion\":2,\"entries\":[{" + entry_json + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::CatalogFormatUnsupported));
    }
    // Missing catalogFormatVersion -> no catalog.
    {
        const SchemaCatalogLoadResult r = SchemaCatalog::from_json("{" + schema_prefix + "\"entries\":[{" + entry_json + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::MalformedCatalogJson));
    }
    // Duplicate JSON field -> rejected by the strict parser.
    {
        const SchemaCatalogLoadResult r = SchemaCatalog::from_json(
            "{" + schema_prefix + "\"catalogFormatVersion\":1,\"catalogFormatVersion\":1,\"entries\":[{" + entry_json + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::MalformedCatalogJson));
    }
    // Unknown top-level field -> rejected.
    {
        const SchemaCatalogLoadResult r = SchemaCatalog::from_json(
            "{" + schema_prefix + "\"catalogFormatVersion\":1,\"bogus\":true,\"entries\":[{" + entry_json + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::MalformedCatalogJson));
    }
    // Unknown entry field -> rejected.
    {
        const std::string e = entry_json + ",\"extra\":1";
        const SchemaCatalogLoadResult r =
            SchemaCatalog::from_json("{" + schema_prefix + "\"catalogFormatVersion\":1,\"entries\":[{" + e + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::MalformedCatalogJson));
    }
    // Wrong field type -> rejected.
    {
        const std::string e =
            "\"schemaId\":123,\"schemaVersion\":\"0.1.0\",\"documentId\":\"choirloom:score/a/0.1.0\","
            "\"path\":\"schemas/score/a.schema.json\",\"status\":\"Draft\"";
        const SchemaCatalogLoadResult r =
            SchemaCatalog::from_json("{" + schema_prefix + "\"catalogFormatVersion\":1,\"entries\":[{" + e + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::MalformedCatalogJson));
    }
    // Malformed entry schemaId -> SchemaIdFormat, no catalog.
    {
        const std::string e =
            "\"schemaId\":\"bogus\",\"schemaVersion\":\"0.1.0\",\"documentId\":\"choirloom:score/a/0.1.0\","
            "\"path\":\"schemas/score/a.schema.json\",\"status\":\"Draft\"";
        const SchemaCatalogLoadResult r =
            SchemaCatalog::from_json("{" + schema_prefix + "\"catalogFormatVersion\":1,\"entries\":[{" + e + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::SchemaIdFormat));
    }
    // Malformed entry schemaVersion -> SchemaVersionFormat.
    {
        const std::string e =
            "\"schemaId\":\"choirloom:score/a\",\"schemaVersion\":\"1.2\",\"documentId\":\"choirloom:score/a/0.1.0\","
            "\"path\":\"schemas/score/a.schema.json\",\"status\":\"Draft\"";
        const SchemaCatalogLoadResult r =
            SchemaCatalog::from_json("{" + schema_prefix + "\"catalogFormatVersion\":1,\"entries\":[{" + e + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::SchemaVersionFormat));
    }
    // Overflowing entry schemaVersion -> SchemaVersionOutOfRange.
    {
        const std::string e =
            "\"schemaId\":\"choirloom:score/a\",\"schemaVersion\":\"4294967296.0.0\","
            "\"documentId\":\"choirloom:score/a/0.1.0\","
            "\"path\":\"schemas/score/a.schema.json\",\"status\":\"Draft\"";
        const SchemaCatalogLoadResult r =
            SchemaCatalog::from_json("{" + schema_prefix + "\"catalogFormatVersion\":1,\"entries\":[{" + e + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::SchemaVersionOutOfRange));
    }
    // Malformed entry documentId -> SchemaDocumentIdFormat.
    {
        const std::string e =
            "\"schemaId\":\"choirloom:score/a\",\"schemaVersion\":\"0.1.0\",\"documentId\":\"choirloom:score/a\","
            "\"path\":\"schemas/score/a.schema.json\",\"status\":\"Draft\"";
        const SchemaCatalogLoadResult r =
            SchemaCatalog::from_json("{" + schema_prefix + "\"catalogFormatVersion\":1,\"entries\":[{" + e + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::SchemaDocumentIdFormat));
    }
    // Unknown rule field -> rejected.
    {
        const std::string rules =
            "[{\"from\":\"choirloom:score/a/0.1.0\",\"to\":\"choirloom:score/a/0.1.0\","
            "\"kind\":\"readable\",\"extra\":1}]";
        const SchemaCatalogLoadResult r = SchemaCatalog::from_json(
            "{" + schema_prefix + "\"catalogFormatVersion\":1,\"entries\":[{" + entry_json + "}],\"rules\":" + rules + "}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::MalformedCatalogJson));
    }
    // Rule with an unknown kind -> rejected.
    {
        const std::string rules =
            "[{\"from\":\"choirloom:score/a/0.1.0\",\"to\":\"choirloom:score/a/0.1.0\",\"kind\":\"bogus\"}]";
        const SchemaCatalogLoadResult r = SchemaCatalog::from_json(
            "{\"catalogFormatVersion\":1,\"entries\":[{" + entry_json + "}],\"rules\":" + rules + "}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::MalformedCatalogJson));
    }
    // Missing $schema binding -> rejected.
    {
        const SchemaCatalogLoadResult r = SchemaCatalog::from_json(
            "{\"catalogFormatVersion\":1,\"entries\":[{" + entry_json + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::MalformedCatalogJson));
    }
    // Non-string $schema -> rejected.
    {
        const SchemaCatalogLoadResult r = SchemaCatalog::from_json(
            "{\"$schema\":42,\"catalogFormatVersion\":1,\"entries\":[{" + entry_json + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::MalformedCatalogJson));
    }
    // Mismatched $schema -> rejected.
    {
        const SchemaCatalogLoadResult r = SchemaCatalog::from_json(
            "{\"$schema\":\"choirloom:score/other/0.1.0\",\"catalogFormatVersion\":1,\"entries\":[{" +
                entry_json + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::MalformedCatalogJson));
    }
    // Strict JSON number grammar: "01" (leading zero) must not be accepted as
    // catalogFormatVersion 1.
    {
        const SchemaCatalogLoadResult r = SchemaCatalog::from_json(
            "{\"$schema\":\"choirloom:score/schema-catalog/0.1.0\",\"catalogFormatVersion\":01,\"entries\":[{" +
                entry_json + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::MalformedCatalogJson));
    }
    // Malformed numeric forms per JSON grammar: "1." (no fraction digits).
    {
        const SchemaCatalogLoadResult r = SchemaCatalog::from_json(
            "{\"$schema\":\"choirloom:score/schema-catalog/0.1.0\",\"catalogFormatVersion\":1.,\"entries\":[{" +
                entry_json + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::MalformedCatalogJson));
    }
    // Malformed numeric form "1e" (no exponent digits).
    {
        const SchemaCatalogLoadResult r = SchemaCatalog::from_json(
            "{\"$schema\":\"choirloom:score/schema-catalog/0.1.0\",\"catalogFormatVersion\":1e,\"entries\":[{" +
                entry_json + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::MalformedCatalogJson));
    }
    // Overflowing documentId schemaVersion (entry path) -> SchemaVersionOutOfRange.
    {
        const std::string e =
            "\"schemaId\":\"choirloom:score/a\",\"schemaVersion\":\"0.1.0\","
            "\"documentId\":\"choirloom:score/a/4294967296.0.0\","
            "\"path\":\"schemas/score/a.schema.json\",\"status\":\"Draft\"";
        const SchemaCatalogLoadResult r = SchemaCatalog::from_json(
            "{" + schema_prefix + "\"catalogFormatVersion\":1,\"entries\":[{" + e + "}]}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::SchemaVersionOutOfRange));
    }
    // Overflowing documentId schemaVersion (rule path) -> SchemaVersionOutOfRange.
    {
        const std::string rules =
            "[{\"from\":\"choirloom:score/a/4294967296.0.0\",\"to\":\"choirloom:score/a/0.1.0\",\"kind\":\"readable\"}]";
        const SchemaCatalogLoadResult r = SchemaCatalog::from_json(
            "{" + schema_prefix + "\"catalogFormatVersion\":1,\"entries\":[{" + entry_json + "}],\"rules\":" + rules + "}");
        CHECK(g, !r.catalog.has_value());
        CHECK(g, has_code(r.diagnostics, DiagnosticCode::SchemaVersionOutOfRange));
    }
}

// ---------------------------------------------------------------------------
// Structured raw-version inspection: exact stable codes, no caller-side parse.
// ---------------------------------------------------------------------------
void test_inspect()
{
    const char* g = "inspect";

    SchemaCatalogBuilder b;
    b.catalog_format_version(1)
        .add_entry(make_entry("choirloom:score/rational-time", "0.1.0", "schemas/score/synthetic-rt-0.1.0.schema.json"))
        .add_entry(make_entry("choirloom:score/rational-time", "0.2.0", "schemas/score/synthetic-rt-0.2.0.schema.json"))
        .add_entry(make_entry("choirloom:score/rational-time", "1.0.0", "schemas/score/synthetic-rt-1.0.0.schema.json"))
        .add_entry(make_entry("choirloom:score/rational-time", "1.1.0", "schemas/score/synthetic-rt-1.1.0.schema.json"));
    const SchemaCatalog catalog = b.build();
    const SchemaId rt = SchemaId::from_string("choirloom:score/rational-time");

    const VersionInspectionResult ok = inspect_schema_version(
        catalog, rt, std::optional<std::string_view>("0.1.0"));
    CHECK(g, ok.entry.has_value() && ok.diagnostics.empty());

    const VersionInspectionResult missing = inspect_schema_version(catalog, rt, std::nullopt);
    CHECK(g, !missing.entry.has_value());
    CHECK(g, missing.diagnostics.size() == 1 &&
                 missing.diagnostics[0].code == DiagnosticCode::MissingSchemaVersion);

    const VersionInspectionResult malformed = inspect_schema_version(
        catalog, rt, std::optional<std::string_view>("1.2"));
    CHECK(g, !malformed.entry.has_value());
    CHECK(g, malformed.diagnostics.size() == 1 &&
                 malformed.diagnostics[0].code == DiagnosticCode::MalformedSchemaVersion);

    const VersionInspectionResult overflow = inspect_schema_version(
        catalog, rt, std::optional<std::string_view>("4294967296.0.0"));
    CHECK(g, overflow.diagnostics.size() == 1 &&
                 overflow.diagnostics[0].code == DiagnosticCode::SchemaVersionOutOfRange);

    const VersionInspectionResult unknown_schema = inspect_schema_version(
        catalog, SchemaId::from_string("choirloom:score/unknown-thing"),
        std::optional<std::string_view>("0.1.0"));
    CHECK(g, unknown_schema.diagnostics.size() == 1 &&
                 unknown_schema.diagnostics[0].code == DiagnosticCode::UnknownSchema);

    const VersionInspectionResult future = inspect_schema_version(
        catalog, rt, std::optional<std::string_view>("99.0.0"));
    CHECK(g, future.diagnostics.size() == 1 &&
                 future.diagnostics[0].code == DiagnosticCode::UnsupportedFutureVersion);

    const VersionInspectionResult unknown_version = inspect_schema_version(
        catalog, rt, std::optional<std::string_view>("0.3.0"));
    CHECK(g, unknown_version.diagnostics.size() == 1 &&
                 unknown_version.diagnostics[0].code == DiagnosticCode::UnknownSchemaVersion);
}

// ---------------------------------------------------------------------------
// Rule validation: duplicates, conflicts, cross-SchemaId edges are rejected;
// compatibility/planning must not succeed on an invalid catalog.
// ---------------------------------------------------------------------------
void test_rule_validation()
{
    const char* g = "rules";

    const SchemaDocumentId d010 = SchemaDocumentId::from_string("choirloom:score/rt/0.1.0");
    const SchemaDocumentId d020 = SchemaDocumentId::from_string("choirloom:score/rt/0.2.0");
    const SchemaDocumentId other = SchemaDocumentId::from_string("choirloom:score/other/0.1.0");

    SchemaCatalogBuilder b;
    b.catalog_format_version(1)
        .add_entry(make_entry("choirloom:score/rt", "0.1.0", "schemas/score/r.schema.json"))
        .add_entry(make_entry("choirloom:score/rt", "0.2.0", "schemas/score/r2.schema.json"))
        .add_entry(make_entry("choirloom:score/other", "0.1.0", "schemas/score/o.schema.json"))
        .add_rule(SchemaCatalogRule{d010, d020, SchemaRuleKind::Migration})
        .add_rule(SchemaCatalogRule{d010, d020, SchemaRuleKind::Migration})  // duplicate
        .add_rule(SchemaCatalogRule{d010, d020, SchemaRuleKind::Readable})   // conflicting
        .add_rule(SchemaCatalogRule{d020, d010, SchemaRuleKind::Migration})  // reverse is fine
        .add_rule(SchemaCatalogRule{d010, other, SchemaRuleKind::Readable}); // cross-schema
    const SchemaCatalog catalog = b.build();

    const std::vector<ValidationDiagnostic> diags = validate_catalog(catalog);
    CHECK(g, has_code(diags, DiagnosticCode::DuplicateRule));
    CHECK(g, has_code(diags, DiagnosticCode::ConflictingRule));
    CHECK(g, has_code(diags, DiagnosticCode::CrossSchemaEdge));

    // Compatibility must not succeed on an invalid catalog.
    const CompatibilityResult compat = schema_compatibility(
        catalog, SchemaId::from_string("choirloom:score/rt"),
        SchemaVersion::from_string("0.1.0"), SchemaVersion::from_string("0.2.0"));
    CHECK(g, compat.value == Compatibility::Unsupported);
    CHECK(g, has_code(compat.diagnostics, DiagnosticCode::ConflictingRule));

    // Planning must not succeed on an invalid catalog.
    const MigrationPlan plan = plan_migration(catalog, d010, d020);
    CHECK(g, plan.steps.empty());
    CHECK(g, has_code(plan.diagnostics, DiagnosticCode::ConflictingRule));

    // A clean catalog (reverse-only migration edge) still plans correctly.
    SchemaCatalogBuilder clean;
    clean.catalog_format_version(1)
        .add_entry(make_entry("choirloom:score/rt", "0.1.0", "schemas/score/r.schema.json"))
        .add_entry(make_entry("choirloom:score/rt", "0.2.0", "schemas/score/r2.schema.json"))
        .add_rule(SchemaCatalogRule{d020, d010, SchemaRuleKind::Migration});
    const SchemaCatalog clean_catalog = clean.build();
    CHECK(g, validate_catalog(clean_catalog).empty());
    const MigrationPlan forward_plan = plan_migration(clean_catalog, d020, d010);
    CHECK(g, forward_plan.steps.size() == 1 && forward_plan.diagnostics.empty());
}

// ---------------------------------------------------------------------------
// Explicit compatibility + deterministic migration planning (synthetic catalog
// built from the golden fixture entries/edges).
// ---------------------------------------------------------------------------
void test_compat_plan()
{
    const char* g = "compat";

    SchemaCatalogBuilder b;
    b.catalog_format_version(1)
        .add_entry(make_entry("choirloom:score/rational-time", "0.1.0", "schemas/score/synthetic-rt-0.1.0.schema.json"))
        .add_entry(make_entry("choirloom:score/rational-time", "0.2.0", "schemas/score/synthetic-rt-0.2.0.schema.json"))
        .add_entry(make_entry("choirloom:score/rational-time", "1.0.0", "schemas/score/synthetic-rt-1.0.0.schema.json"))
        .add_entry(make_entry("choirloom:score/rational-time", "1.1.0", "schemas/score/synthetic-rt-1.1.0.schema.json"))
        .add_rule(SchemaCatalogRule{
            SchemaDocumentId::from_string("choirloom:score/rational-time/0.1.0"),
            SchemaDocumentId::from_string("choirloom:score/rational-time/0.2.0"),
            SchemaRuleKind::Readable})
        .add_rule(SchemaCatalogRule{
            SchemaDocumentId::from_string("choirloom:score/rational-time/0.2.0"),
            SchemaDocumentId::from_string("choirloom:score/rational-time/1.0.0"),
            SchemaRuleKind::Migration})
        .add_rule(SchemaCatalogRule{
            SchemaDocumentId::from_string("choirloom:score/rational-time/1.0.0"),
            SchemaDocumentId::from_string("choirloom:score/rational-time/1.1.0"),
            SchemaRuleKind::Migration});
    const SchemaCatalog catalog = b.build();
    CHECK(g, validate_catalog(catalog).empty());

    const SchemaId rt = SchemaId::from_string("choirloom:score/rational-time");

    const CompatibilityResult exact = schema_compatibility(
        catalog, rt, SchemaVersion::from_string("0.1.0"), SchemaVersion::from_string("0.1.0"));
    CHECK(g, exact.value == Compatibility::Exact && exact.diagnostics.empty());

    const CompatibilityResult readable = schema_compatibility(
        catalog, rt, SchemaVersion::from_string("0.1.0"), SchemaVersion::from_string("0.2.0"));
    CHECK(g, readable.value == Compatibility::Readable && readable.diagnostics.empty());

    const CompatibilityResult migration = schema_compatibility(
        catalog, rt, SchemaVersion::from_string("0.2.0"), SchemaVersion::from_string("1.0.0"));
    CHECK(g, migration.value == Compatibility::RequiresMigration && migration.diagnostics.empty());

    const CompatibilityResult unsupported = schema_compatibility(
        catalog, rt, SchemaVersion::from_string("0.1.0"), SchemaVersion::from_string("1.1.0"));
    CHECK(g, unsupported.value == Compatibility::Unsupported);
    CHECK(g, !unsupported.diagnostics.empty() &&
                 unsupported.diagnostics[0].code == DiagnosticCode::UnsupportedDirection);

    const CompatibilityResult future = schema_compatibility(
        catalog, rt, SchemaVersion::from_string("0.1.0"), SchemaVersion::from_string("99.0.0"));
    CHECK(g, future.value == Compatibility::Unsupported);
    CHECK(g, !future.diagnostics.empty() &&
                 has_code(future.diagnostics, DiagnosticCode::UnsupportedFutureVersion));

    // Determinism of compatibility results.
    CHECK(g, schema_compatibility(catalog, rt, SchemaVersion::from_string("0.1.0"),
                                  SchemaVersion::from_string("1.1.0")).diagnostics ==
                 unsupported.diagnostics);

    // Migration planning (contract only).
    const SchemaDocumentId d010 = SchemaDocumentId::from_string("choirloom:score/rational-time/0.1.0");
    const SchemaDocumentId d020 = SchemaDocumentId::from_string("choirloom:score/rational-time/0.2.0");
    const SchemaDocumentId d110 = SchemaDocumentId::from_string("choirloom:score/rational-time/1.1.0");

    const MigrationPlan same = plan_migration(catalog, d010, d010);
    CHECK(g, same.steps.empty() && same.diagnostics.empty());

    const MigrationPlan chain = plan_migration(catalog, d020, d110);
    CHECK(g, chain.steps.size() == 2 && chain.diagnostics.empty());
    CHECK(g, chain.steps[0].from == d020 && chain.steps[0].to ==
                 SchemaDocumentId::from_string("choirloom:score/rational-time/1.0.0"));
    CHECK(g, chain.steps[1].to == d110);

    const MigrationPlan readable_only = plan_migration(catalog, d010, d020);
    CHECK(g, readable_only.steps.empty());
    CHECK(g, !readable_only.diagnostics.empty() &&
                 readable_only.diagnostics[0].code == DiagnosticCode::MissingMigrationEdge);

    const MigrationPlan no_edge = plan_migration(
        catalog, d010, SchemaDocumentId::from_string("choirloom:score/rational-time/1.0.0"));
    CHECK(g, no_edge.steps.empty() &&
                 has_code(no_edge.diagnostics, DiagnosticCode::MissingMigrationEdge));

    const MigrationPlan plan_future = plan_migration(
        catalog, d010, SchemaDocumentId::from_string("choirloom:score/rational-time/5.0.0"));
    CHECK(g, plan_future.steps.empty() &&
                 has_code(plan_future.diagnostics, DiagnosticCode::UnsupportedFutureVersion));

    // Determinism of planning.
    const MigrationPlan again = plan_migration(catalog, d020, d110);
    CHECK(g, chain.steps == again.steps && chain.diagnostics == again.diagnostics);
}

// ---------------------------------------------------------------------------
// Golden fixture: read, validate envelope, apply every case.
// ---------------------------------------------------------------------------
void test_golden_fixture()
{
    const char* g = "golden";
    const std::string root = test_root();
    const std::string fixture_path = root + "/tests/golden/schema_foundation.samples.json";
    const std::string real_catalog_path = root + "/schemas/schema-catalog.json";
    const std::string collection_schema_path = root + "/schemas/score/schema-foundation-collection.schema.json";

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

    const test_json::Node* schema_ref = fixture.find("$schema");
    CHECK(g, schema_ref != nullptr && schema_ref->is_string() &&
              schema_ref->string.find("schema-foundation-collection.schema.json") != std::string::npos);
    const test_json::Node* cases = fixture.find("cases");
    CHECK(g, cases != nullptr && cases->is_array() && !cases->array.empty());

    // The collection schema declares the allowed 'kind' enum; every fixture
    // case kind must be one of them.
    const std::string collection_schema_text = read_text_file(collection_schema_path, ok);
    CHECK(g, ok && !collection_schema_text.empty());
    std::vector<std::string> allowed_kinds;
    try {
        const test_json::Node cs = test_json::parse(collection_schema_text);
        const test_json::Node* cs_props = cs.find("properties");
        const test_json::Node* cases_p =
            (cs_props != nullptr && cs_props->is_object()) ? cs_props->find("cases") : nullptr;
        const test_json::Node* items = (cases_p != nullptr) ? cases_p->find("items") : nullptr;
        const test_json::Node* props = (items != nullptr && items->is_object()) ? items->find("properties") : nullptr;
        const test_json::Node* kind_p = (props != nullptr && props->is_object()) ? props->find("kind") : nullptr;
        const test_json::Node* kind_enum = (kind_p != nullptr && kind_p->is_object()) ? kind_p->find("enum") : nullptr;
        if (kind_enum != nullptr && kind_enum->is_array()) {
            for (auto const& v : kind_enum->array) {
                if (v.is_string()) {
                    allowed_kinds.push_back(v.string);
                }
            }
        }
    } catch (test_json::ParseError const&) {
    }
    CHECK(g, allowed_kinds.size() >= 14);

    // Load the real catalog (used by registered-document-id cases).
    const std::string real_catalog_text = read_text_file(real_catalog_path, ok);
    CHECK(g, ok && !real_catalog_text.empty());
    const SchemaCatalogLoadResult real_loaded = SchemaCatalog::from_json(real_catalog_text);
    CHECK(g, real_loaded.catalog.has_value());

    // Collect synthetic entries/edges from the fixture. Every case kind must be
    // declared by the collection schema and EXECUTED by the harness; a case
    // that is unknown or silently skipped is a failure.
    SchemaCatalogBuilder synth_builder;
    synth_builder.catalog_format_version(1);
    std::vector<bool> handled(cases->array.size(), false);
    std::size_t count = 0;

    for (std::size_t idx = 0; idx < cases->array.size(); ++idx) {
        auto const& c = cases->array[idx];
        ++count;
        CHECK(g, c.is_object());
        const test_json::Node* name = c.find("name");
        const test_json::Node* kind = c.find("kind");
        const test_json::Node* value = c.find("value");
        CHECK(g, name != nullptr && name->is_string() && !name->string.empty());
        CHECK(g, kind != nullptr && kind->is_string());
        CHECK(g, value != nullptr);
        if (kind == nullptr || !kind->is_string() || value == nullptr) {
            continue;
        }
        const std::string k = kind->string;
        const bool kind_known =
            std::find(allowed_kinds.begin(), allowed_kinds.end(), k) != allowed_kinds.end();
        CHECK(g, kind_known);

        if (k == "schema-version" && value->is_string()) {
            handled[idx] = true;
            const SchemaVersion v = SchemaVersion::from_string(value->string);
            CHECK(g, v.to_string() == value->string);
        } else if (k == "malformed-version" && value->is_string()) {
            handled[idx] = true;
            CHECK(g, version_throws(value->string));
        } else if (k == "version-order" && value->is_array()) {
            handled[idx] = true;
            bool ordered = true;
            for (std::size_t i = 1; i < value->array.size(); ++i) {
                if (value->array[i - 1].is_string() && value->array[i].is_string()) {
                    const SchemaVersion a = SchemaVersion::from_string(value->array[i - 1].string);
                    const SchemaVersion b = SchemaVersion::from_string(value->array[i].string);
                    if (!(a < b)) {
                        ordered = false;
                    }
                }
            }
            CHECK(g, ordered);
        } else if (k == "schema-id" && value->is_string()) {
            handled[idx] = true;
            const SchemaId id = SchemaId::from_string(value->string);
            CHECK(g, id.to_string() == value->string);
        } else if (k == "malformed-schema-id" && value->is_string()) {
            handled[idx] = true;
            CHECK_THROWS(g, SchemaId::from_string(value->string), std::invalid_argument);
        } else if (k == "schema-document-id" && value->is_string()) {
            handled[idx] = true;
            const SchemaDocumentId d = SchemaDocumentId::from_string(value->string);
            CHECK(g, d.to_string() == value->string);
        } else if (k == "malformed-document-id" && value->is_string()) {
            handled[idx] = true;
            CHECK_THROWS(g, SchemaDocumentId::from_string(value->string), std::invalid_argument);
        } else if (k == "registered-document-id" && value->is_string()) {
            handled[idx] = true;
            const SchemaDocumentId d = SchemaDocumentId::from_string(value->string);
            const CatalogLookupResult r = lookup_schema(*real_loaded.catalog,
                                                        d.schema_id(), d.schema_version());
            CHECK(g, r.entry.has_value() && r.diagnostics.empty());
        } else if (k == "entry" && value->is_object()) {
            const test_json::Node* id = value->find("schemaId");
            const test_json::Node* ver = value->find("schemaVersion");
            const test_json::Node* doc = value->find("documentId");
            const test_json::Node* path = value->find("path");
            const test_json::Node* status = value->find("status");
            CHECK(g, id != nullptr && id->is_string());
            CHECK(g, ver != nullptr && ver->is_string());
            CHECK(g, doc != nullptr && doc->is_string());
            CHECK(g, path != nullptr && path->is_string());
            CHECK(g, status != nullptr && status->is_string());
            if (id != nullptr && id->is_string() && ver != nullptr && ver->is_string() &&
                doc != nullptr && doc->is_string() && path != nullptr && path->is_string() &&
                status != nullptr && status->is_string()) {
                handled[idx] = true;
                const SchemaId sid = SchemaId::from_string(id->string);
                const SchemaVersion sv = SchemaVersion::from_string(ver->string);
                const SchemaDocumentId sd = SchemaDocumentId::from_string(doc->string);
                SchemaStatus st = SchemaStatus::Draft;
                if (status->string == "Stable") {
                    st = SchemaStatus::Stable;
                } else if (status->string == "Deprecated") {
                    st = SchemaStatus::Deprecated;
                }
                synth_builder.add_entry(SchemaCatalogEntry{sid, sv, sd, path->string, st});
            }
        } else if (k == "edge" && value->is_object()) {
            const test_json::Node* from = value->find("from");
            const test_json::Node* to = value->find("to");
            const test_json::Node* kind_n = value->find("kind");
            if (from != nullptr && from->is_string() && to != nullptr && to->is_string() &&
                kind_n != nullptr && kind_n->is_string()) {
                handled[idx] = true;
                const SchemaRuleKind rk = (kind_n->string == "readable")
                                              ? SchemaRuleKind::Readable
                                              : SchemaRuleKind::Migration;
                synth_builder.add_rule(SchemaCatalogRule{
                    SchemaDocumentId::from_string(from->string),
                    SchemaDocumentId::from_string(to->string), rk});
            }
        }
        // Pass-2 kinds (compat/lookup/plan/inspect) are handled below; unknown
        // kinds already failed the kind_known check.
    }

    const SchemaCatalog synth = synth_builder.build();
    CHECK(g, validate_catalog(synth).empty());

    // Second pass: compat/lookup/plan/inspect cases against the synthetic
    // catalog. Any case that is not executed (unknown kind / wrong shape)
    // fails.
    for (std::size_t idx = 0; idx < cases->array.size(); ++idx) {
        auto const& c = cases->array[idx];
        const test_json::Node* kind = c.find("kind");
        const test_json::Node* value = c.find("value");
        if (kind == nullptr || !kind->is_string() || value == nullptr) {
            continue;
        }
        const std::string k = kind->string;
        if (k == "compat" && value->is_object()) {
            const test_json::Node* id = value->find("schemaId");
            const test_json::Node* from = value->find("from");
            const test_json::Node* to = value->find("to");
            const test_json::Node* expected = value->find("expected");
            CHECK(g, id != nullptr && id->is_string() && from != nullptr && from->is_string() &&
                         to != nullptr && to->is_string() && expected != nullptr &&
                         expected->is_string());
            if (id != nullptr && id->is_string() && from != nullptr && from->is_string() &&
                to != nullptr && to->is_string() && expected != nullptr && expected->is_string()) {
                handled[idx] = true;
                const CompatibilityResult r = schema_compatibility(
                    synth, SchemaId::from_string(id->string),
                    SchemaVersion::from_string(from->string),
                    SchemaVersion::from_string(to->string));
                CHECK(g, std::string(choirloom::score::to_string(r.value)) == expected->string);
            }
        } else if (k == "lookup" && value->is_object()) {
            const test_json::Node* id = value->find("schemaId");
            const test_json::Node* ver = value->find("version");
            const test_json::Node* expected = value->find("expectedDiagnostic");
            CHECK(g, id != nullptr && id->is_string() && ver != nullptr && ver->is_string() &&
                         expected != nullptr && expected->is_string());
            if (id != nullptr && id->is_string() && ver != nullptr && ver->is_string() &&
                expected != nullptr && expected->is_string()) {
                handled[idx] = true;
                const CatalogLookupResult r = lookup_schema(
                    synth, SchemaId::from_string(id->string),
                    SchemaVersion::from_string(ver->string));
                if (expected->string.empty()) {
                    CHECK(g, r.entry.has_value() && r.diagnostics.empty());
                } else {
                    CHECK(g, !r.entry.has_value());
                    CHECK(g, !r.diagnostics.empty() &&
                                 std::string(choirloom::score::to_string(r.diagnostics[0].code)) ==
                                     expected->string);
                }
            }
        } else if (k == "plan" && value->is_object()) {
            const test_json::Node* from = value->find("from");
            const test_json::Node* to = value->find("to");
            const test_json::Node* steps = value->find("expectedSteps");
            const test_json::Node* expected = value->find("expectedDiagnostic");
            CHECK(g, from != nullptr && from->is_string() && to != nullptr && to->is_string() &&
                         steps != nullptr && steps->kind == test_json::Node::Kind::Number &&
                         expected != nullptr && expected->is_string());
            if (from != nullptr && from->is_string() && to != nullptr && to->is_string() &&
                steps != nullptr && steps->kind == test_json::Node::Kind::Number &&
                expected != nullptr && expected->is_string()) {
                handled[idx] = true;
                const MigrationPlan p = plan_migration(
                    synth, SchemaDocumentId::from_string(from->string),
                    SchemaDocumentId::from_string(to->string));
                CHECK(g, p.steps.size() == static_cast<std::size_t>(steps->number));
                if (expected->string.empty()) {
                    CHECK(g, p.diagnostics.empty());
                } else {
                    CHECK(g, !p.diagnostics.empty() &&
                                 std::string(choirloom::score::to_string(p.diagnostics[0].code)) ==
                                     expected->string);
                }
            }
        } else if (k == "inspect" && value->is_object()) {
            const test_json::Node* id = value->find("schemaId");
            const test_json::Node* ver = value->find("version");
            const test_json::Node* expected = value->find("expectedDiagnostic");
            CHECK(g, id != nullptr && id->is_string() && expected != nullptr &&
                         expected->is_string());
            if (id != nullptr && id->is_string() && expected != nullptr &&
                expected->is_string()) {
                handled[idx] = true;
                std::optional<std::string_view> raw;
                if (ver != nullptr && ver->is_string()) {
                    raw = std::string_view(ver->string);
                }
                const VersionInspectionResult r = inspect_schema_version(
                    synth, SchemaId::from_string(id->string), raw);
                if (expected->string.empty()) {
                    CHECK(g, r.entry.has_value() && r.diagnostics.empty());
                } else {
                    CHECK(g, !r.entry.has_value());
                    CHECK(g, !r.diagnostics.empty() &&
                                 std::string(choirloom::score::to_string(r.diagnostics[0].code)) ==
                                     expected->string);
                }
            }
        } else if (std::find(allowed_kinds.begin(), allowed_kinds.end(), k) ==
                   allowed_kinds.end()) {
            CHECK(g, false);  // unknown kind must fail (never silently skip)
        }
    }

    // Every case was executed: no unknown or unexecuted kind silently skipped.
    for (std::size_t i = 0; i < handled.size(); ++i) {
        CHECK(g, handled[i]);
    }
    CHECK(g, count == cases->array.size());
    CHECK(g, count >= 40);
}

}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        test_version();
        test_ids();
        test_real_catalog();
        test_catalog_diagnostics();
        test_catalog_parse_strict();
        test_inspect();
        test_rule_validation();
        test_compat_plan();
        test_golden_fixture();
    } catch (std::exception const& e) {
        std::printf("UNCAUGHT EXCEPTION: %s\n", e.what());
        return 2;
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
