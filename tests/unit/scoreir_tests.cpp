// ============================================================================
// tests/unit/scoreir_tests.cpp - unit tests for M0-005 minimal ScoreIR schema.
//
// Self-contained, dependency-free runner. When built through CMake,
// CHOIRLOOM_TEST_SOURCE_DIR is an absolute repository root so the golden
// fixture, catalog, and schema files resolve from the build tree; when
// compiled directly, run from the repository root.
//
// Coverage: golden fixture parse/validate, canonical byte-stable round trip
// (including deeply reordered equivalent inputs), counts and invariants
// (tagged event variants, shared-role note, rest, genuine opaqueNotation,
// pickup actualDuration, RationalTime offsets/durations 0/1 1/4 1/12, F# vs
// Gb written-pitch distinction with octave, Opaque preservation), catalog
// registration + on-disk validation, a LOCAL Draft-2020-12-subset schema
// validator loaded from the public schema (schema/core divergence guard), and
// structured diagnostics for all required negative inputs.
// ============================================================================

#include "entity_revision.h"
#include "rational_time.h"
#include "schema_foundation.h"
#include "scoreir.h"
#include "test_json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifndef CHOIRLOOM_TEST_SOURCE_DIR
#define CHOIRLOOM_TEST_SOURCE_DIR "."
#endif

using choirloom::score::EntityId;
using choirloom::score::SchemaCatalog;
using choirloom::score::SchemaCatalogLoadResult;
using choirloom::score::ScoreIRDiagnostic;
using choirloom::score::ScoreIRDiagnosticCode;
using choirloom::score::ScoreIRDocument;
using choirloom::score::ScoreIRParseResult;
using choirloom::score::ScoreIRValidationResult;
using choirloom::score::lookup_schema;
using choirloom::score::validate_catalog;
using choirloom::score::validate_catalog_files;
using choirloom::score::validate_scoreir;

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

bool has_code(std::vector<ScoreIRDiagnostic> const& diags, ScoreIRDiagnosticCode code)
{
    for (auto const& d : diags) {
        if (d.code == code) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Minimal JSON serializer for test_json::Node.
// ---------------------------------------------------------------------------
void json_escape_into(std::string& out, std::string const& s)
{
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out.push_back(c);
        }
    }
    out.push_back('"');
}

void json_serialize_into(std::string& out, test_json::Node const& n)
{
    using Kind = test_json::Node::Kind;
    switch (n.kind) {
        case Kind::Null:
            out += "null";
            break;
        case Kind::Bool:
            out += n.boolean ? "true" : "false";
            break;
        case Kind::Number: {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.17g", n.number);
            out += buf;
            break;
        }
        case Kind::String:
            json_escape_into(out, n.string);
            break;
        case Kind::Array: {
            out.push_back('[');
            for (std::size_t i = 0; i < n.array.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                json_serialize_into(out, n.array[i]);
            }
            out.push_back(']');
            break;
        }
        case Kind::Object: {
            out.push_back('{');
            for (std::size_t i = 0; i < n.object.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                json_escape_into(out, n.object[i].first);
                out.push_back(':');
                json_serialize_into(out, n.object[i].second);
            }
            out.push_back('}');
            break;
        }
    }
}

std::string json_serialize(test_json::Node const& n)
{
    std::string out;
    json_serialize_into(out, n);
    return out;
}

void reverse_keys(test_json::Node& n)
{
    if (n.is_object()) {
        std::reverse(n.object.begin(), n.object.end());
        for (auto& kv : n.object) {
            reverse_keys(kv.second);
        }
    } else if (n.is_array()) {
        for (auto& item : n.array) {
            reverse_keys(item);
        }
    }
}

// A compact valid ScoreIR document used as the base for malformed variants.
std::string base_doc()
{
    return "{\"$schema\":\"choirloom:score/scoreir/0.1.0\","
           "\"version\":\"0.1.0\","
           "\"scoreId\":\"1a2b3c4d-5e6f-4a7b-8c9d-0e1f2a3b4c5d\","
           "\"parts\":[{\"id\":\"f1e2d3c4-b5a6-4c7d-8e9f-0a1b2c3d4e5f\",\"type\":\"choral\",\"name\":\"Soprano\"}],"
           "\"staves\":[{\"id\":\"a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d\","
           "\"partRef\":\"f1e2d3c4-b5a6-4c7d-8e9f-0a1b2c3d4e5f\",\"label\":\"st\"}],"
           "\"voices\":[{\"id\":\"9a8b7c6d-5e4f-4a3b-8c9d-0e1f2a3b4c5d\","
           "\"staffRef\":\"a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d\",\"label\":\"v\"}],"
           "\"performerRoles\":[{\"id\":\"6d5c4b3a-2f1e-4d0c-8b9a-1e2f3a4b5c6d\",\"label\":\"S1\"}],"
           "\"measures\":[{\"id\":\"b2c3d4e5-f6a7-4b8c-9d0e-1f2a3b4c5d6e\","
           "\"index\":0,\"number\":\"0\",\"pickup\":false,"
           "\"actualDuration\":{\"numerator\":\"1\",\"denominator\":\"4\"}}],"
           "\"events\":[{\"id\":\"d4e5f6a7-b8c9-4d0e-9f1a-2b3c4d5e6f70\","
           "\"kind\":\"note\","
           "\"measureRef\":\"b2c3d4e5-f6a7-4b8c-9d0e-1f2a3b4c5d6e\","
           "\"offset\":{\"numerator\":\"0\",\"denominator\":\"1\"},"
           "\"duration\":{\"numerator\":\"1\",\"denominator\":\"4\"},"
           "\"pitch\":{\"written\":{\"letter\":\"C\",\"accidental\":\"natural\",\"octave\":5},"
           "\"displayAccidental\":\"natural\","
           "\"sounding\":{\"letter\":\"C\",\"accidental\":\"natural\",\"octave\":5}},"
           "\"roleRefs\":[\"6d5c4b3a-2f1e-4d0c-8b9a-1e2f3a4b5c6d\"],"
           "\"voiceRef\":\"9a8b7c6d-5e4f-4a3b-8c9d-0e1f2a3b4c5d\"}]}";
}

std::string replace_all(std::string s, std::string const& from, std::string const& to)
{
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

// ---------------------------------------------------------------------------
// LOCAL JSON Schema (Draft 2020-12 subset) validator for exactly the features
// used by the public ScoreIR schema and its $ref targets: type, required,
// properties, additionalProperties, items, minItems, enum, const, pattern,
// minimum, maximum, uniqueItems, oneOf, $ref. Unsupported validation keywords
// are flagged loudly so schema/core divergence is detected.
// ---------------------------------------------------------------------------
class SchemaValidator {
public:
    explicit SchemaValidator(std::map<std::string, test_json::Node> const& refs)
        : refs_(refs) {}

    std::vector<std::string> validate(test_json::Node const& inst,
                                      test_json::Node const& schema) const
    {
        std::vector<std::string> errors;
        check(inst, schema, std::string("$"), errors);
        return errors;
    }

private:
    std::map<std::string, test_json::Node> refs_;

    static bool is_number(test_json::Node const& n)
    {
        return n.kind == test_json::Node::Kind::Number;
    }
    static bool is_integer(test_json::Node const& n)
    {
        return is_number(n) && std::floor(n.number) == n.number;
    }

    void check(test_json::Node const& inst, test_json::Node const& sch,
               std::string const& path, std::vector<std::string>& errors) const
    {
        if (!sch.is_object()) {
            return;
        }
        // Divergence guard: unsupported validation keywords fail loudly.
        static const char* const kUnsupported[] = {
            "not", "allOf", "anyOf", "if", "then", "else", "patternProperties",
            "prefixItems", "contains", "minProperties", "maxProperties", "maxItems",
            "minLength", "maxLength", "multipleOf", "dependentRequired",
            "dependentSchemas", "unevaluatedProperties"};
        for (char const* kw : kUnsupported) {
            if (sch.find(kw) != nullptr) {
                errors.push_back(path + ": validator does not support '" + std::string(kw) + "'");
            }
        }
        if (const test_json::Node* ref = sch.find("$ref"); ref != nullptr) {
            if (ref->is_string()) {
                auto it = refs_.find(ref->string);
                if (it == refs_.end()) {
                    errors.push_back(path + ": unresolved $ref '" + ref->string + "'");
                    return;
                }
                check(inst, it->second, path, errors);
                return;
            }
        }
        if (const test_json::Node* one = sch.find("oneOf"); one != nullptr && one->is_array()) {
            int matches = 0;
            for (auto const& sub : one->array) {
                std::vector<std::string> sub_errors;
                check(inst, sub, path, sub_errors);
                if (sub_errors.empty()) {
                    ++matches;
                }
            }
            if (matches != 1) {
                errors.push_back(path + ": oneOf matched " + std::to_string(matches) +
                                 " subschemas (expected exactly 1)");
            }
            return;  // oneOf is exclusive; no other keywords combine with it here
        }
        if (const test_json::Node* t = sch.find("type"); t != nullptr && t->is_string()) {
            const std::string want = t->string;
            bool ok = false;
            if (want == "object") ok = inst.is_object();
            else if (want == "array") ok = inst.is_array();
            else if (want == "string") ok = inst.is_string();
            else if (want == "integer") ok = is_integer(inst);
            else if (want == "number") ok = is_number(inst);
            else if (want == "boolean") ok = inst.kind == test_json::Node::Kind::Bool;
            else if (want == "null") ok = inst.kind == test_json::Node::Kind::Null;
            if (!ok) {
                errors.push_back(path + ": expected type '" + want + "'");
            }
        }
        if (const test_json::Node* c = sch.find("const"); c != nullptr) {
            if (!node_equal(inst, *c)) {
                errors.push_back(path + ": const mismatch");
            }
        }
        if (const test_json::Node* en = sch.find("enum"); en != nullptr && en->is_array()) {
            bool found = false;
            for (auto const& v : en->array) {
                if (node_equal(inst, v)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                errors.push_back(path + ": value not in enum");
            }
        }
        if (const test_json::Node* pat = sch.find("pattern");
            pat != nullptr && pat->is_string() && inst.is_string()) {
            try {
                if (!std::regex_match(inst.string, std::regex(pat->string))) {
                    errors.push_back(path + ": pattern mismatch");
                }
            } catch (std::regex_error const&) {
                errors.push_back(path + ": invalid schema pattern");
            }
        }
        if (is_number(inst)) {
            if (const test_json::Node* mn = sch.find("minimum"); mn != nullptr && is_number(*mn)) {
                if (inst.number < mn->number) {
                    errors.push_back(path + ": below minimum");
                }
            }
            if (const test_json::Node* mx = sch.find("maximum"); mx != nullptr && is_number(*mx)) {
                if (inst.number > mx->number) {
                    errors.push_back(path + ": above maximum");
                }
            }
        }
        if (inst.is_object()) {
            if (const test_json::Node* req = sch.find("required"); req != nullptr && req->is_array()) {
                for (auto const& k : req->array) {
                    if (k.is_string() && inst.find(k.string) == nullptr) {
                        errors.push_back(path + ": missing required '" + k.string + "'");
                    }
                }
            }
            if (const test_json::Node* props = sch.find("properties");
                props != nullptr && props->is_object()) {
                for (auto const& kv : props->object) {
                    if (const test_json::Node* v = inst.find(kv.first); v != nullptr) {
                        check(*v, kv.second, path + "." + kv.first, errors);
                    }
                }
            }
            if (const test_json::Node* ap = sch.find("additionalProperties");
                ap != nullptr && ap->kind == test_json::Node::Kind::Bool && !ap->boolean) {
                const test_json::Node* props = sch.find("properties");
                for (auto const& kv : inst.object) {
                    const bool known =
                        (props != nullptr && props->is_object() && props->find(kv.first) != nullptr);
                    if (!known) {
                        errors.push_back(path + ": additional property '" + kv.first +
                                         "' not allowed");
                    }
                }
            }
        }
        if (inst.is_array()) {
            if (const test_json::Node* items = sch.find("items"); items != nullptr && items->is_object()) {
                for (std::size_t i = 0; i < inst.array.size(); ++i) {
                    check(inst.array[i], *items, path + "[" + std::to_string(i) + "]", errors);
                }
            }
            if (const test_json::Node* min = sch.find("minItems"); min != nullptr && is_integer(*min)) {
                if (inst.array.size() < static_cast<std::size_t>(min->number)) {
                    errors.push_back(path + ": fewer than minItems");
                }
            }
            if (const test_json::Node* uniq = sch.find("uniqueItems");
                uniq != nullptr && uniq->kind == test_json::Node::Kind::Bool && uniq->boolean) {
                for (std::size_t i = 0; i < inst.array.size(); ++i) {
                    for (std::size_t j = i + 1; j < inst.array.size(); ++j) {
                        if (node_equal(inst.array[i], inst.array[j])) {
                            errors.push_back(path + ": duplicate items violate uniqueItems");
                        }
                    }
                }
            }
        }
    }

    static bool node_equal(test_json::Node const& a, test_json::Node const& b)
    {
        return json_serialize(a) == json_serialize(b);
    }
};

// ---------------------------------------------------------------------------
// Golden fixture: parse, validate, canonical byte-stable round trip.
// ---------------------------------------------------------------------------
void test_fixture()
{
    const char* g = "fixture";
    const std::string root = test_root();
    const std::string fixture_path = root + "/tests/golden/scoreir.samples.json";
    const std::string collection_schema_path = root + "/schemas/score/scoreir-collection.schema.json";

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
              schema_ref->string.find("scoreir-collection.schema.json") != std::string::npos);
    const test_json::Node* cases = fixture.find("cases");
    CHECK(g, cases != nullptr && cases->is_array() && cases->array.size() == 1);

    const std::string coll_text = read_text_file(collection_schema_path, ok);
    CHECK(g, ok && !coll_text.empty());
    try {
        const test_json::Node cs = test_json::parse(coll_text);
        const test_json::Node* props = cs.find("properties");
        const test_json::Node* cases_p =
            (props != nullptr && props->is_object()) ? props->find("cases") : nullptr;
        const test_json::Node* items =
            (cases_p != nullptr && cases_p->is_object()) ? cases_p->find("items") : nullptr;
        const test_json::Node* iprops =
            (items != nullptr && items->is_object()) ? items->find("properties") : nullptr;
        const test_json::Node* kind_p =
            (iprops != nullptr && iprops->is_object()) ? iprops->find("kind") : nullptr;
        const test_json::Node* kind_enum =
            (kind_p != nullptr && kind_p->is_object()) ? kind_p->find("enum") : nullptr;
        bool has_scoreir_kind = false;
        if (kind_enum != nullptr && kind_enum->is_array()) {
            for (auto const& v : kind_enum->array) {
                if (v.is_string() && v.string == "scoreir") {
                    has_scoreir_kind = true;
                }
            }
        }
        CHECK(g, has_scoreir_kind);
    } catch (test_json::ParseError const&) {
        CHECK(g, false);
    }

    const test_json::Node* first = cases->array.size() > 0 ? &cases->array[0] : nullptr;
    const test_json::Node* kind = first != nullptr ? first->find("kind") : nullptr;
    const test_json::Node* value = first != nullptr ? first->find("value") : nullptr;
    CHECK(g, kind != nullptr && kind->is_string() && kind->string == "scoreir");
    CHECK(g, value != nullptr && value->is_object());
    if (value == nullptr || !value->is_object()) {
        return;
    }
    const std::string doc_text = json_serialize(*value);

    const ScoreIRParseResult r = ScoreIRDocument::from_json(doc_text);
    CHECK(g, r.document.has_value());
    CHECK(g, r.diagnostics.empty());
    if (!r.document.has_value()) {
        return;
    }
    const ScoreIRDocument& doc = *r.document;

    CHECK(g, doc.score_id() == EntityId::from_string("1a2b3c4d-5e6f-4a7b-8c9d-0e1f2a3b4c5d"));
    CHECK(g, doc.part_count() == 1 && doc.staff_count() == 1 && doc.voice_count() == 2);
    CHECK(g, doc.performer_role_count() == 2);
    CHECK(g, doc.measure_count() == 2 && doc.pickup_measure_count() == 1);
    CHECK(g, doc.event_count() == 4);

    // Canonical round trip is byte stable and regenerated (not an echo): a
    // whitespace-messy equivalent input yields the identical canonical output.
    const std::string canonical = doc.to_json();
    const ScoreIRParseResult again = ScoreIRDocument::from_json(canonical);
    CHECK(g, again.document.has_value() && again.diagnostics.empty());
    CHECK(g, again.document->to_json() == canonical);

    const std::string messy = replace_all(doc_text, ",", " , ");
    CHECK(g, messy != canonical);
    const ScoreIRParseResult messy_result = ScoreIRDocument::from_json(messy);
    CHECK(g, messy_result.document.has_value() && messy_result.diagnostics.empty());
    CHECK(g, messy_result.document->to_json() == canonical);

    // Inspect the canonical output structurally.
    const test_json::Node cdoc = test_json::parse(canonical);
    const test_json::Node* events = cdoc.find("events");
    const test_json::Node* measures = cdoc.find("measures");
    const test_json::Node* roles = cdoc.find("performerRoles");
    CHECK(g, events != nullptr && events->is_array() && events->array.size() == 4);
    CHECK(g, measures != nullptr && measures->is_array() && measures->array.size() == 2);
    CHECK(g, roles != nullptr && roles->is_array() && roles->array.size() == 2);

    // Tagged variants: note / note (shared roles) / rest / UnknownSymbol.
    auto kind_of = [](test_json::Node const& e) {
        const test_json::Node* k = e.find("kind");
        return (k != nullptr && k->is_string()) ? k->string : std::string();
    };
    CHECK(g, kind_of(events->array[0]) == "note");
    CHECK(g, kind_of(events->array[1]) == "note");
    CHECK(g, kind_of(events->array[2]) == "rest");
    CHECK(g, kind_of(events->array[3]) == "UnknownSymbol");
    // Shared-role note associates BOTH roles.
    {
        const test_json::Node* rr = events->array[1].find("roleRefs");
        CHECK(g, rr != nullptr && rr->is_array() && rr->array.size() == 2);
    }
    // Pickup measure flagged with canonical exact (incomplete) actualDuration.
    {
        const test_json::Node* m0 = &measures->array[0];
        const test_json::Node* pk = m0->find("pickup");
        CHECK(g, pk != nullptr && pk->kind == test_json::Node::Kind::Bool && pk->boolean);
        const test_json::Node* ad = m0->find("actualDuration");
        const test_json::Node* n = ad != nullptr ? ad->find("numerator") : nullptr;
        const test_json::Node* d = ad != nullptr ? ad->find("denominator") : nullptr;
        CHECK(g, n != nullptr && n->is_string() && n->string == "1" && d != nullptr &&
                     d->is_string() && d->string == "3");
    }
    // Exact canonical RationalTime offsets/durations: 0/1, 1/4, 1/12.
    {
        auto time_of = [](test_json::Node const& e, char const* key) {
            const test_json::Node* t = e.find(key);
            if (t == nullptr || !t->is_object()) {
                return std::string();
            }
            const test_json::Node* n = t->find("numerator");
            const test_json::Node* d = t->find("denominator");
            return (n != nullptr && n->is_string() && d != nullptr && d->is_string())
                       ? n->string + "/" + d->string
                       : std::string();
        };
        CHECK(g, time_of(events->array[0], "offset") == "0/1");
        CHECK(g, time_of(events->array[0], "duration") == "1/4");
        CHECK(g, time_of(events->array[1], "offset") == "1/4");
        CHECK(g, time_of(events->array[1], "duration") == "1/12");
    }
    // F# vs Gb: distinct written spelling (with octave), separate display
    // accidental and sounding pitch; all fields round-trip.
    {
        auto spelling = [](test_json::Node const& e, char const* sub) {
            const test_json::Node* p = e.find("pitch");
            const test_json::Node* s = (p != nullptr && p->is_object()) ? p->find(sub) : nullptr;
            const test_json::Node* l = (s != nullptr && s->is_object()) ? s->find("letter") : nullptr;
            const test_json::Node* a = (s != nullptr && s->is_object()) ? s->find("accidental") : nullptr;
            const test_json::Node* o = (s != nullptr && s->is_object()) ? s->find("octave") : nullptr;
            return (l != nullptr && l->is_string() && a != nullptr && a->is_string() &&
                    o != nullptr && o->kind == test_json::Node::Kind::Number)
                       ? l->string + a->string + std::to_string(static_cast<int>(o->number))
                       : std::string();
        };
        auto display = [](test_json::Node const& e) {
            const test_json::Node* p = e.find("pitch");
            const test_json::Node* d = (p != nullptr && p->is_object()) ? p->find("displayAccidental") : nullptr;
            return (d != nullptr && d->is_string()) ? d->string : std::string();
        };
        const test_json::Node& e0 = events->array[0];  // F#5
        const test_json::Node& e1 = events->array[1];  // Gb5
        CHECK(g, spelling(e0, "written") == "Fsharp5");
        CHECK(g, spelling(e0, "sounding") == "Fsharp5");
        CHECK(g, display(e0) == "sharp");
        CHECK(g, spelling(e1, "written") == "Gflat5");
        CHECK(g, spelling(e1, "sounding") == "Gflat5");
        CHECK(g, display(e1) == "flat");
        // Same register; distinct written spelling.
        CHECK(g, spelling(e0, "written") != spelling(e1, "written"));
        const test_json::Node* s0 = e0.find("pitch")->find("sounding");
        const test_json::Node* s1 = e1.find("pitch")->find("sounding");
        CHECK(g, s0->find("octave")->number == s1->find("octave")->number);
    }
    // Genuine UnknownSymbol variant: raw preserved, no fabricated pitch/duration.
    {
        const test_json::Node& e3 = events->array[3];
        const test_json::Node* raw = e3.find("raw");
        CHECK(g, raw != nullptr && raw->is_string() &&
                     raw->string == "unrecognized glyph above staff (kept verbatim)");
        CHECK(g, e3.find("pitch") == nullptr && e3.find("duration") == nullptr &&
                     e3.find("voiceRef") == nullptr && e3.find("roleRefs") == nullptr);
    }
}

// ---------------------------------------------------------------------------
// Real schema validation: the fixture and negative inputs are validated
// against the PUBLIC schema by a local Draft-2020-12-subset validator loaded
// from the catalog (offline, no dependencies); schema/core divergence fails.
// ---------------------------------------------------------------------------
void test_schema_validation()
{
    const char* g = "schema";
    const std::string root = test_root();
    auto load = [&](std::string const& rel, bool& okv) {
        return read_text_file(root + "/" + rel, okv);
    };

    bool ok = false;
    const std::string scoreir_text = load("schemas/score/scoreir.schema.json", ok);
    CHECK(g, ok);
    const std::string entity_id_text = load("schemas/score/entity-id.schema.json", ok);
    CHECK(g, ok);
    const std::string rational_time_text = load("schemas/score/rational-time.schema.json", ok);
    CHECK(g, ok);

    test_json::Node scoreir_schema, entity_id_schema, rational_time_schema;
    try {
        scoreir_schema = test_json::parse(scoreir_text);
        entity_id_schema = test_json::parse(entity_id_text);
        rational_time_schema = test_json::parse(rational_time_text);
    } catch (test_json::ParseError const&) {
        CHECK(g, false);
        return;
    }
    std::map<std::string, test_json::Node> refs;
    refs["choirloom:score/entity-id/0.1.0"] = entity_id_schema;
    refs["choirloom:score/rational-time/0.1.0"] = rational_time_schema;
    const SchemaValidator validator(refs);

    // Fixture (from disk) is valid per the public schema AND clean per the core.
    const std::string fixture_text = read_text_file(root + "/tests/golden/scoreir.samples.json", ok);
    CHECK(g, ok);
    const test_json::Node fixture = test_json::parse(fixture_text);
    const test_json::Node* value = fixture.find("cases")->array[0].find("value");
    CHECK(g, validator.validate(*value, scoreir_schema).empty());
    CHECK(g, ScoreIRDocument::from_json(json_serialize(*value)).diagnostics.empty());

    // Negative inputs must fail BOTH the schema and the core.
    const std::string score_prefix = "\"$schema\":\"choirloom:score/scoreir/0.1.0\",";
    const struct {
        const char* name;
        std::string text;
    } kNegatives[] = {
        // Instance identity: an instance must not carry "$id".
        {"instance-id", replace_all(base_doc(), "\"scoreId\":",
                                    "\"$id\":\"choirloom:score/scoreir/0.1.0\",\"scoreId\":")},
        // Missing $schema / wrong binding / wrong version.
        {"missing-schema", replace_all(base_doc(), "\"$schema\":\"choirloom:score/scoreir/0.1.0\",", "")},
        {"wrong-version", replace_all(base_doc(), "\"version\":\"0.1.0\"", "\"version\":\"0.2.0\"")},
        {"missing-version", replace_all(base_doc(), "\"version\":\"0.1.0\",", "")},
        // Fractional measure index.
        {"fractional-index", replace_all(base_doc(), "\"index\":0", "\"index\":0.5")},
        // Fractional / negative / out-of-range octave.
        {"fractional-octave", replace_all(base_doc(), "\"octave\":5", "\"octave\":5.5")},
        {"negative-octave", replace_all(base_doc(), "\"octave\":5", "\"octave\":-1")},
        {"out-of-range-octave", replace_all(base_doc(), "\"octave\":5", "\"octave\":10")},
        // Empty and duplicate roleRefs.
        {"empty-role-refs", replace_all(base_doc(),
                                        "\"roleRefs\":[\"6d5c4b3a-2f1e-4d0c-8b9a-1e2f3a4b5c6d\"]",
                                        "\"roleRefs\":[]")},
        {"duplicate-role-refs", replace_all(base_doc(),
                                            "\"roleRefs\":[\"6d5c4b3a-2f1e-4d0c-8b9a-1e2f3a4b5c6d\"]",
                                            "\"roleRefs\":[\"6d5c4b3a-2f1e-4d0c-8b9a-1e2f3a4b5c6d\",\"6d5c4b3a-2f1e-4d0c-8b9a-1e2f3a4b5c6d\"]")},
        // Unknown nested fields at written / sounding / opaque.
        {"unknown-written-field", replace_all(base_doc(), "\"written\":{\"letter\":\"C\"",
                                              "\"written\":{\"foo\":1,\"letter\":\"C\"")},
        {"unknown-sounding-field", replace_all(base_doc(), "\"sounding\":{\"letter\":\"C\"",
                                               "\"sounding\":{\"foo\":1,\"letter\":\"C\"")},
        // Unknown event kind (no coercion).
        {"unknown-event-kind", replace_all(base_doc(), "\"kind\":\"note\"", "\"kind\":\"somedecoration\"")},
        // UnknownSymbol is first-class but closed: pitch is not allowed on it.
        {"unknownsymbol-with-pitch", replace_all(base_doc(), "\"kind\":\"note\"", "\"kind\":\"UnknownSymbol\"")},
        // Accompaniment part type rejected (draft is choral-only).
        {"accompaniment-part", replace_all(base_doc(), "\"type\":\"choral\"", "\"type\":\"accompaniment\"")},
        // Rest carrying a pitch.
        {"rest-with-pitch", replace_all(base_doc(),
                                        "\"kind\":\"note\",\"measureRef\":\"b2c3d4e5-f6a7-4b8c-9d0e-1f2a3b4c5d6e\",\"offset\":{\"numerator\":\"0\",\"denominator\":\"1\"},",
                                        "\"kind\":\"rest\",\"measureRef\":\"b2c3d4e5-f6a7-4b8c-9d0e-1f2a3b4c5d6e\",\"offset\":{\"numerator\":\"0\",\"denominator\":\"1\"},")},
        // Rest roleRefs must be strings (parity: core and schema both reject).
        {"rest-nonstring-role-refs", replace_all(replace_all(base_doc(), "\"kind\":\"note\"", "\"kind\":\"rest\""),
                                                 "\"roleRefs\":[\"6d5c4b3a-2f1e-4d0c-8b9a-1e2f3a4b5c6d\"]",
                                                 "\"roleRefs\":[123]")},
        // Rest carrying raw is not allowed by the rest variant.
        {"rest-with-raw", replace_all(replace_all(base_doc(), "\"kind\":\"note\"", "\"kind\":\"rest\""),
                                      "\"voiceRef\":\"9a8b7c6d-5e4f-4a3b-8c9d-0e1f2a3b4c5d\"",
                                      "\"voiceRef\":\"9a8b7c6d-5e4f-4a3b-8c9d-0e1f2a3b4c5d\",\"raw\":\"x\"")},
    };
    for (auto const& c : kNegatives) {
        test_json::Node doc;
        bool doc_ok = true;
        try {
            doc = test_json::parse(c.text);
        } catch (test_json::ParseError const&) {
            doc_ok = false;
        }
        CHECK(g, doc_ok);
        if (doc_ok) {
            CHECK(g, !validator.validate(doc, scoreir_schema).empty());
            CHECK(g, !ScoreIRDocument::from_json(c.text).diagnostics.empty());
        }
    }
}

// ---------------------------------------------------------------------------
// Catalog registration + on-disk validation.
// ---------------------------------------------------------------------------
void test_catalog()
{
    const char* g = "catalog";
    const std::string root = test_root();
    const std::string catalog_path = root + "/schemas/schema-catalog.json";
    bool ok = false;
    const std::string catalog_text = read_text_file(catalog_path, ok);
    CHECK(g, ok && !catalog_text.empty());

    const SchemaCatalogLoadResult loaded = SchemaCatalog::from_json(catalog_text);
    CHECK(g, loaded.catalog.has_value() && loaded.diagnostics.empty());
    if (!loaded.catalog.has_value()) {
        return;
    }
    const SchemaCatalog& catalog = *loaded.catalog;

    for (char const* doc : {"choirloom:score/scoreir/0.1.0",
                            "choirloom:score/scoreir-collection/0.1.0"}) {
        const choirloom::score::SchemaDocumentId d =
            choirloom::score::SchemaDocumentId::from_string(doc);
        const auto r = lookup_schema(catalog, d.schema_id(), d.schema_version());
        CHECK(g, r.entry.has_value() && r.diagnostics.empty());
    }
    CHECK(g, validate_catalog(catalog).empty());
    const std::vector<choirloom::score::ValidationDiagnostic> file_diags =
        validate_catalog_files(catalog, root);
    CHECK(g, file_diags.empty());
}

// ---------------------------------------------------------------------------
// Structured diagnostics for malformed documents.
// ---------------------------------------------------------------------------
void test_malformed()
{
    const char* g = "malformed";

    const struct {
        const char* name;
        std::string doc;
        ScoreIRDiagnosticCode code;
    } kCases[] = {
        {"unsupported-version", replace_all(base_doc(), "\"version\":\"0.1.0\"", "\"version\":\"0.2.0\""),
         ScoreIRDiagnosticCode::UnsupportedScoreIRVersion},
        {"instance-id", replace_all(base_doc(), "\"scoreId\":",
                                    "\"$id\":\"choirloom:score/scoreir/0.1.0\",\"scoreId\":"),
         ScoreIRDiagnosticCode::SchemaBindingMismatch},
        {"missing-required", replace_all(base_doc(), "\"version\":\"0.1.0\",", ""),
         ScoreIRDiagnosticCode::MissingRequiredField},
        {"unknown-field", replace_all(base_doc(), "\"scoreId\":", "\"foo\":1,\"scoreId\":"),
         ScoreIRDiagnosticCode::UnknownField},
        {"derived-layer", replace_all(base_doc(), "\"scoreId\":", "\"playbackTime\":1,\"scoreId\":"),
         ScoreIRDiagnosticCode::DerivedLayerField},
        {"reserved-humanVerified", replace_all(base_doc(), "\"scoreId\":",
                                               "\"humanVerified\":true,\"scoreId\":"),
         ScoreIRDiagnosticCode::ReservedField},
        {"duplicate-key", replace_all(base_doc(), "\"parts\":[", "\"parts\":[],\"parts\":["),
         ScoreIRDiagnosticCode::MalformedScoreIRJson},
        {"malformed-id", replace_all(base_doc(), "\"id\":\"f1e2d3c4-b5a6-4c7d-8e9f-0a1b2c3d4e5f\"",
                                     "\"id\":\"bogus\""),
         ScoreIRDiagnosticCode::MalformedEntityId},
        {"duplicate-id", replace_all(base_doc(), "\"id\":\"a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d\"",
                                     "\"id\":\"f1e2d3c4-b5a6-4c7d-8e9f-0a1b2c3d4e5f\""),
         ScoreIRDiagnosticCode::DuplicateEntityId},
        {"dangling-ref", replace_all(base_doc(), "\"measureRef\":\"b2c3d4e5-f6a7-4b8c-9d0e-1f2a3b4c5d6e\"",
                                     "\"measureRef\":\"00000000-0000-4000-8000-000000000000\""),
         ScoreIRDiagnosticCode::DanglingReference},
        {"mistyped-ref", replace_all(base_doc(), "\"partRef\":\"f1e2d3c4-b5a6-4c7d-8e9f-0a1b2c3d4e5f\"",
                                     "\"partRef\":\"a1b2c3d4-e5f6-4a7b-8c9d-0e1f2a3b4c5d\""),
         ScoreIRDiagnosticCode::MistypedReference},
        // staff.partRef == scoreId is NOT a part -> mistyped, never resolves.
        {"mistyped-score-ref", replace_all(base_doc(), "\"partRef\":\"f1e2d3c4-b5a6-4c7d-8e9f-0a1b2c3d4e5f\"",
                                           "\"partRef\":\"1a2b3c4d-5e6f-4a7b-8c9d-0e1f2a3b4c5d\""),
         ScoreIRDiagnosticCode::MistypedReference},
        {"noncanonical-time", replace_all(base_doc(), "\"duration\":{\"numerator\":\"1\",\"denominator\":\"4\"}",
                                          "\"duration\":{\"numerator\":\"2\",\"denominator\":\"4\"}"),
         ScoreIRDiagnosticCode::MalformedRationalTime},
        {"negative-offset", replace_all(base_doc(), "\"offset\":{\"numerator\":\"0\",\"denominator\":\"1\"}",
                                        "\"offset\":{\"numerator\":\"-1\",\"denominator\":\"4\"}"),
         ScoreIRDiagnosticCode::NegativeOffset},
        {"nonpositive-duration", replace_all(base_doc(), "\"duration\":{\"numerator\":\"1\",\"denominator\":\"4\"}",
                                             "\"duration\":{\"numerator\":\"0\",\"denominator\":\"1\"}"),
         ScoreIRDiagnosticCode::NonPositiveDuration},
        {"nonpositive-actualDuration", replace_all(base_doc(), "\"actualDuration\":{\"numerator\":\"1\",\"denominator\":\"4\"}",
                                                   "\"actualDuration\":{\"numerator\":\"0\",\"denominator\":\"1\"}"),
         ScoreIRDiagnosticCode::NonPositiveDuration},
        {"out-of-range-time", replace_all(base_doc(), "\"duration\":{\"numerator\":\"1\",\"denominator\":\"4\"}",
                                          "\"duration\":{\"numerator\":\"99999999999999999999999999\",\"denominator\":\"1\"}"),
         ScoreIRDiagnosticCode::RationalTimeOutOfRange},
        {"fractional-index", replace_all(base_doc(), "\"index\":0", "\"index\":0.5"),
         ScoreIRDiagnosticCode::MalformedPitch},
        {"bad-octave", replace_all(base_doc(), "\"octave\":5", "\"octave\":10"),
         ScoreIRDiagnosticCode::MalformedPitch},
        {"empty-role-refs", replace_all(base_doc(),
                                        "\"roleRefs\":[\"6d5c4b3a-2f1e-4d0c-8b9a-1e2f3a4b5c6d\"]",
                                        "\"roleRefs\":[]"),
         ScoreIRDiagnosticCode::EmptyRoleRefs},
        {"duplicate-role-refs", replace_all(base_doc(),
                                            "\"roleRefs\":[\"6d5c4b3a-2f1e-4d0c-8b9a-1e2f3a4b5c6d\"]",
                                            "\"roleRefs\":[\"6d5c4b3a-2f1e-4d0c-8b9a-1e2f3a4b5c6d\",\"6d5c4b3a-2f1e-4d0c-8b9a-1e2f3a4b5c6d\"]"),
         ScoreIRDiagnosticCode::DuplicateRoleRef},
        {"unknown-written-field", replace_all(base_doc(), "\"written\":{\"letter\":\"C\"",
                                              "\"written\":{\"foo\":1,\"letter\":\"C\""),
         ScoreIRDiagnosticCode::UnknownField},
        {"accompaniment-part", replace_all(base_doc(), "\"type\":\"choral\"", "\"type\":\"accompaniment\""),
         ScoreIRDiagnosticCode::UnsupportedPartType},
        {"unknown-event-kind", replace_all(base_doc(), "\"kind\":\"note\"", "\"kind\":\"somedecoration\""),
         ScoreIRDiagnosticCode::UnknownEventKind},
        // UnknownSymbol is first-class but closed: it must not carry pitch.
        {"unknownsymbol-with-pitch", replace_all(base_doc(),
                                                 "\"kind\":\"note\"",
                                                 "\"kind\":\"UnknownSymbol\""),
         ScoreIRDiagnosticCode::UnknownField},
        // rest roleRefs must be strings referencing roles (parity with schema).
        {"rest-nonstring-role-refs", replace_all(replace_all(base_doc(), "\"kind\":\"note\"", "\"kind\":\"rest\""),
                                                 "\"roleRefs\":[\"6d5c4b3a-2f1e-4d0c-8b9a-1e2f3a4b5c6d\"]",
                                                 "\"roleRefs\":[123]"),
         ScoreIRDiagnosticCode::MistypedReference},
        {"rest-with-raw", replace_all(replace_all(base_doc(), "\"kind\":\"note\"", "\"kind\":\"rest\""),
                                      "\"voiceRef\":\"9a8b7c6d-5e4f-4a3b-8c9d-0e1f2a3b4c5d\"",
                                      "\"voiceRef\":\"9a8b7c6d-5e4f-4a3b-8c9d-0e1f2a3b4c5d\",\"raw\":\"x\""),
         ScoreIRDiagnosticCode::UnknownField},
        {"malformed-json", "{\"scoreId\":",
         ScoreIRDiagnosticCode::MalformedScoreIRJson},
    };

    for (auto const& c : kCases) {
        const ScoreIRParseResult r = ScoreIRDocument::from_json(c.doc);
        CHECK(g, !r.document.has_value());
        CHECK(g, has_code(r.diagnostics, c.code));
        const ScoreIRValidationResult v = validate_scoreir(c.doc);
        CHECK(g, has_code(v.diagnostics, c.code));
    }
}

// ---------------------------------------------------------------------------
// Canonicalization: parse -> serialize -> parse -> serialize is byte stable
// even when fields are reordered at EVERY nested level.
// ---------------------------------------------------------------------------
void test_canonical_reordered()
{
    const char* g = "reorder";
    const std::string doc = base_doc();
    const ScoreIRParseResult base = ScoreIRDocument::from_json(doc);
    CHECK(g, base.document.has_value() && base.diagnostics.empty());
    const std::string canonical = base.document->to_json();

    // Deeply reorder every object's keys at every nested level.
    test_json::Node reordered = test_json::parse(doc);
    reverse_keys(reordered);
    const std::string reordered_text = json_serialize(reordered);
    const ScoreIRParseResult rr = ScoreIRDocument::from_json(reordered_text);
    CHECK(g, rr.document.has_value() && rr.diagnostics.empty());
    CHECK(g, rr.document->to_json() == canonical);
}

// ---------------------------------------------------------------------------
// Determinism / no input mutation.
// ---------------------------------------------------------------------------
void test_determinism()
{
    const char* g = "determinism";
    const std::string doc = base_doc();
    const ScoreIRParseResult a = ScoreIRDocument::from_json(doc);
    const ScoreIRParseResult b = ScoreIRDocument::from_json(doc);
    CHECK(g, a.document.has_value() && b.document.has_value());
    CHECK(g, a.document->to_json() == b.document->to_json());
    CHECK(g, a.diagnostics == b.diagnostics);
    CHECK(g, doc == base_doc());
}

}  // namespace

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        test_fixture();
        test_schema_validation();
        test_catalog();
        test_malformed();
        test_canonical_reordered();
        test_determinism();
    } catch (std::exception const& e) {
        std::printf("UNCAUGHT EXCEPTION: %s\n", e.what());
        return 2;
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
