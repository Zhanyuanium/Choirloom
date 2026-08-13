// ============================================================================
// core/score/scoreir.cpp - M0-005 minimal ScoreIR schema parser/validator.
// See scoreir.h for the contract.
// ============================================================================

#include "scoreir.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include "rational_time.h"

namespace choirloom::score {
namespace {

// ---------------------------------------------------------------------------
// Strict JSON reader (duplicate keys and noncanonical numbers rejected).
// ---------------------------------------------------------------------------
struct JsonNode {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    std::string number;  // raw numeric token (strict grammar)
    std::string string;
    std::vector<JsonNode> array;
    std::vector<std::pair<std::string, JsonNode>> object;

    const JsonNode* find(std::string const& key) const
    {
        if (kind != Kind::Object) {
            return nullptr;
        }
        for (auto const& kv : object) {
            if (kv.first == key) {
                return &kv.second;
            }
        }
        return nullptr;
    }
};

class JsonParseError : public std::runtime_error {
public:
    explicit JsonParseError(std::string const& msg) : std::runtime_error(msg) {}
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    JsonNode parse()
    {
        const JsonNode n = parse_value();
        skip_ws();
        if (pos_ != text_.size()) {
            throw JsonParseError("trailing content after JSON value");
        }
        return n;
    }

private:
    std::string_view text_;
    std::size_t pos_ = 0;

    static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
    void skip_ws() { while (pos_ < text_.size() && is_ws(text_[pos_])) ++pos_; }

    char peek()
    {
        if (pos_ >= text_.size()) {
            throw JsonParseError("unexpected end of input");
        }
        return text_[pos_];
    }

    JsonNode parse_value()
    {
        skip_ws();
        const char c = peek();
        if (c == '{') {
            return parse_object();
        }
        if (c == '[') {
            return parse_array();
        }
        if (c == '"') {
            JsonNode n;
            n.kind = JsonNode::Kind::String;
            n.string = parse_string();
            return n;
        }
        if (c == 't') {
            expect_literal("true");
            JsonNode n;
            n.kind = JsonNode::Kind::Bool;
            n.boolean = true;
            return n;
        }
        if (c == 'f') {
            expect_literal("false");
            JsonNode n;
            n.kind = JsonNode::Kind::Bool;
            n.boolean = false;
            return n;
        }
        if (c == 'n') {
            expect_literal("null");
            return JsonNode();
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            JsonNode n;
            n.kind = JsonNode::Kind::Number;
            n.number = consume_number();
            return n;
        }
        throw JsonParseError("unexpected character in JSON");
    }

    void expect_literal(char const* lit)
    {
        for (; *lit != '\0'; ++lit) {
            if (pos_ >= text_.size() || text_[pos_] != *lit) {
                throw JsonParseError("invalid literal");
            }
            ++pos_;
        }
    }

    std::string consume_number()
    {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') {
            ++pos_;
        }
        const std::size_t int_start = pos_;
        bool any = false;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            ++pos_;
            any = true;
        }
        if (!any) {
            throw JsonParseError("malformed number");
        }
        if (pos_ - int_start > 1 && text_[int_start] == '0') {
            throw JsonParseError("invalid number: leading zero in integer part");
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            bool frac = false;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
                frac = true;
            }
            if (!frac) {
                throw JsonParseError("malformed number");
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            bool exp = false;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
                exp = true;
            }
            if (!exp) {
                throw JsonParseError("malformed number");
            }
        }
        return std::string(text_.substr(start, pos_ - start));
    }

    JsonNode parse_object()
    {
        ++pos_;
        JsonNode n;
        n.kind = JsonNode::Kind::Object;
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return n;
        }
        while (true) {
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                throw JsonParseError("expected string key");
            }
            std::string key = parse_string();
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                throw JsonParseError("expected ':'");
            }
            ++pos_;
            for (auto const& kv : n.object) {
                if (kv.first == key) {
                    throw JsonParseError("duplicate field in JSON object: \"" + key + "\"");
                }
            }
            n.object.emplace_back(std::move(key), parse_value());
            skip_ws();
            if (pos_ >= text_.size()) {
                throw JsonParseError("unterminated object");
            }
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return n;
            }
            throw JsonParseError("expected ',' or '}'");
        }
    }

    JsonNode parse_array()
    {
        ++pos_;
        JsonNode n;
        n.kind = JsonNode::Kind::Array;
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return n;
        }
        while (true) {
            n.array.push_back(parse_value());
            skip_ws();
            if (pos_ >= text_.size()) {
                throw JsonParseError("unterminated array");
            }
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                return n;
            }
            throw JsonParseError("expected ',' or ']'");
        }
    }

    std::string parse_string()
    {
        ++pos_;
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) {
                throw JsonParseError("unterminated string");
            }
            const char c = text_[pos_];
            if (c == '"') {
                ++pos_;
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                throw JsonParseError("control character in string");
            }
            if (c == '\\') {
                ++pos_;
                if (pos_ >= text_.size()) {
                    throw JsonParseError("unterminated escape");
                }
                const char e = text_[pos_];
                switch (e) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 >= text_.size()) {
                            throw JsonParseError("unterminated \\u escape");
                        }
                        unsigned cp = 0;
                        for (int k = 1; k <= 4; ++k) {
                            const char h = text_[pos_ + k];
                            const int hv = (h >= '0' && h <= '9') ? h - '0'
                                        : (h >= 'a' && h <= 'f') ? 10 + (h - 'a')
                                        : (h >= 'A' && h <= 'F') ? 10 + (h - 'A') : -1;
                            if (hv < 0) {
                                throw JsonParseError("invalid \\u escape");
                            }
                            cp = (cp << 4) | static_cast<unsigned>(hv);
                        }
                        pos_ += 4;
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (pos_ + 6 >= text_.size() || text_[pos_ + 1] != '\\' ||
                                text_[pos_ + 2] != 'u') {
                                throw JsonParseError("unpaired surrogate in \\u escape");
                            }
                            unsigned lo = 0;
                            for (int k = 3; k <= 6; ++k) {
                                const char h = text_[pos_ + k];
                                const int hv = (h >= '0' && h <= '9') ? h - '0'
                                            : (h >= 'a' && h <= 'f') ? 10 + (h - 'a')
                                            : (h >= 'A' && h <= 'F') ? 10 + (h - 'A') : -1;
                                if (hv < 0) {
                                    throw JsonParseError("invalid \\u escape");
                                }
                                lo = (lo << 4) | static_cast<unsigned>(hv);
                            }
                            if (lo < 0xDC00 || lo > 0xDFFF) {
                                throw JsonParseError("unpaired surrogate in \\u escape");
                            }
                            pos_ += 6;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                            throw JsonParseError("lone low surrogate in \\u escape");
                        }
                        if (cp < 0x80) {
                            out.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else if (cp < 0x10000) {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default:
                        throw JsonParseError("unsupported escape sequence in string");
                }
                ++pos_;
            } else {
                out.push_back(c);
                ++pos_;
            }
        }
    }
};

JsonNode json_parse(std::string_view text)
{
    return JsonParser(text).parse();
}

// ---------------------------------------------------------------------------
// Canonical JSON writer (compact; keys are sorted recursively).
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
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void json_write_into(std::string& out, JsonNode const& n)
{
    switch (n.kind) {
        case JsonNode::Kind::Null:
            out += "null";
            break;
        case JsonNode::Kind::Bool:
            out += n.boolean ? "true" : "false";
            break;
        case JsonNode::Kind::Number:
            out += n.number;
            break;
        case JsonNode::Kind::String:
            json_escape_into(out, n.string);
            break;
        case JsonNode::Kind::Array: {
            out.push_back('[');
            for (std::size_t i = 0; i < n.array.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                json_write_into(out, n.array[i]);
            }
            out.push_back(']');
            break;
        }
        case JsonNode::Kind::Object: {
            out.push_back('{');
            for (std::size_t i = 0; i < n.object.size(); ++i) {
                if (i != 0) {
                    out.push_back(',');
                }
                json_escape_into(out, n.object[i].first);
                out.push_back(':');
                json_write_into(out, n.object[i].second);
            }
            out.push_back('}');
            break;
        }
    }
}

// Recursive canonical key ordering at EVERY nested level.
void canonical_sort(JsonNode& n)
{
    if (n.kind == JsonNode::Kind::Object) {
        std::sort(n.object.begin(), n.object.end(),
                  [](auto const& a, auto const& b) { return a.first < b.first; });
        for (auto& kv : n.object) {
            canonical_sort(kv.second);
        }
    } else if (n.kind == JsonNode::Kind::Array) {
        for (auto& item : n.array) {
            canonical_sort(item);
        }
    }
}

std::string json_write(JsonNode n)
{
    canonical_sort(n);
    std::string out;
    json_write_into(out, n);
    return out;
}

// ---------------------------------------------------------------------------
// ScoreIR validation helpers.
// ---------------------------------------------------------------------------
const char* kScoreIRSchemaId = "choirloom:score/scoreir/0.1.0";
const char* kScoreIRVersion = "0.1.0";

bool is_derived_layer_field(std::string const& key)
{
    static const char* const kDerived[] = {
        "playbackTime", "performance", "transform", "geometry",
        "jianpu", "rhythmicAnchor", "visualAnchor", "annotation", "audio"};
    for (char const* d : kDerived) {
        if (key == d) {
            return true;
        }
    }
    return false;
}

bool is_reserved_field(std::string const& key)
{
    // Out-of-scope reserved durable source/project provenance metadata (not a
    // derived layer): rejected distinctly, never DerivedLayerField.
    return key == "humanVerified";
}

struct EntityKind {
    enum Type { Score, Part, Staff, Voice, Role, Measure, Event } type;
    EntityId id;
};

bool known_accidental(std::string const& a)
{
    static const char* const kAcc[] = {"natural", "sharp", "flat",
                                       "double-sharp", "double-flat"};
    for (char const* k : kAcc) {
        if (a == k) {
            return true;
        }
    }
    return false;
}

bool valid_letter(char c)
{
    return c >= 'A' && c <= 'G';
}

// A numeric token is a canonical integer when it is a plain decimal integer
// (no fraction, no exponent, optional leading '-').
bool is_integer_token(std::string const& number)
{
    if (number.empty()) {
        return false;
    }
    std::size_t i = 0;
    if (number[0] == '-') {
        ++i;
    }
    if (i >= number.size()) {
        return false;
    }
    for (; i < number.size(); ++i) {
        if (number[i] < '0' || number[i] > '9') {
            return false;
        }
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Validation core (member; fills the canonical document when clean).
// ---------------------------------------------------------------------------
bool ScoreIRDocument::validate_document(std::string_view json, ScoreIRDocument& out,
                                        std::vector<ScoreIRDiagnostic>& diags)
{
    auto fail = [&](ScoreIRDiagnosticCode code, std::optional<std::string> field,
                    std::string msg) {
        diags.push_back(ScoreIRDiagnostic{code, std::nullopt, std::move(field), std::move(msg)});
    };
    auto reject_field = [&](std::string const& where, std::string const& key) {
        if (is_reserved_field(key)) {
            fail(ScoreIRDiagnosticCode::ReservedField, where,
                 "reserved out-of-scope metadata field at " + where + ": \"" + key + "\"");
        } else if (is_derived_layer_field(key)) {
            fail(ScoreIRDiagnosticCode::DerivedLayerField, where,
                 "derived-layer field at " + where + ": \"" + key + "\"");
        } else {
            fail(ScoreIRDiagnosticCode::UnknownField, where,
                 "unknown field at " + where + ": \"" + key + "\"");
        }
    };

    JsonNode root;
    try {
        root = json_parse(json);
    } catch (JsonParseError const& e) {
        fail(ScoreIRDiagnosticCode::MalformedScoreIRJson, std::nullopt,
             std::string("ScoreIR JSON is malformed: ") + e.what());
        return false;
    }
    if (root.kind != JsonNode::Kind::Object) {
        fail(ScoreIRDiagnosticCode::MalformedScoreIRJson, std::nullopt,
             "ScoreIR document root must be a JSON object");
        return false;
    }

    // Closed schema: unknown top-level fields rejected (reserved/derived-layer
    // fields get their own codes).
    for (auto const& kv : root.object) {
        const bool known = kv.first == "$schema" || kv.first == "version" ||
                           kv.first == "scoreId" || kv.first == "metadata" ||
                           kv.first == "parts" || kv.first == "staves" ||
                           kv.first == "voices" || kv.first == "performerRoles" ||
                           kv.first == "measures" || kv.first == "events";
        if (!known) {
            reject_field("(root)", kv.first);
        }
    }

    // Instance identity: the instance binds via "$schema" and MUST NOT carry
    // "$id" (that is the schema document's identity only).
    const JsonNode* schema_b = root.find("$schema");
    if (schema_b == nullptr) {
        fail(ScoreIRDiagnosticCode::MissingSchemaBinding, std::nullopt,
             "missing '$schema' (expected 'choirloom:score/scoreir/0.1.0')");
    } else if (schema_b->kind != JsonNode::Kind::String ||
               schema_b->string != kScoreIRSchemaId) {
        fail(ScoreIRDiagnosticCode::SchemaBindingMismatch, std::nullopt,
             "'$schema' must be exactly 'choirloom:score/scoreir/0.1.0'");
    }
    if (const JsonNode* id_n = root.find("$id"); id_n != nullptr) {
        fail(ScoreIRDiagnosticCode::SchemaBindingMismatch, std::nullopt,
             "ScoreIR instances must not carry '$id' (only the schema document has $id identity)");
    }
    const JsonNode* ver_n = root.find("version");
    if (ver_n == nullptr) {
        fail(ScoreIRDiagnosticCode::MissingRequiredField, std::nullopt,
             "missing required field 'version'");
    } else if (ver_n->kind != JsonNode::Kind::String || ver_n->string != kScoreIRVersion) {
        fail(ScoreIRDiagnosticCode::UnsupportedScoreIRVersion, std::nullopt,
             "unsupported ScoreIR version '" +
                 (ver_n->kind == JsonNode::Kind::String ? ver_n->string : std::string("<non-string>")) +
                 "' (supported: 0.1.0)");
    }

    // scoreId.
    const JsonNode* score_n = root.find("scoreId");
    if (score_n == nullptr) {
        fail(ScoreIRDiagnosticCode::MissingRequiredField, std::nullopt,
             "missing required field 'scoreId'");
    } else if (score_n->kind != JsonNode::Kind::String) {
        fail(ScoreIRDiagnosticCode::MalformedEntityId, std::nullopt,
             "'scoreId' must be a canonical EntityId string");
    }
    EntityId score_id = EntityId::from_string("1a2b3c4d-5e6f-4a7b-8c9d-0e1f2a3b4c5d");
    bool score_id_ok = false;
    if (score_n != nullptr && score_n->kind == JsonNode::Kind::String) {
        try {
            score_id = EntityId::from_string(score_n->string);
            score_id_ok = true;
        } catch (std::invalid_argument const&) {
            fail(ScoreIRDiagnosticCode::MalformedEntityId, std::nullopt,
                 "malformed scoreId '" + score_n->string + "'");
        }
    }

    // metadata (optional; title only).
    if (const JsonNode* meta = root.find("metadata"); meta != nullptr) {
        if (meta->kind != JsonNode::Kind::Object) {
            fail(ScoreIRDiagnosticCode::UnknownField, std::nullopt,
                 "'metadata' must be an object");
        } else {
            for (auto const& kv : meta->object) {
                if (kv.first != "title") {
                    reject_field("metadata", kv.first);
                }
            }
            if (const JsonNode* title = meta->find("title");
                title != nullptr && title->kind != JsonNode::Kind::String) {
                fail(ScoreIRDiagnosticCode::UnknownField, std::nullopt,
                     "'metadata.title' must be a string");
            }
        }
    }

    // Collections.
    struct CollectionSpec {
        char const* key;
        EntityKind::Type type;
    };
    const CollectionSpec kCollections[] = {
        {"parts", EntityKind::Part},
        {"staves", EntityKind::Staff},
        {"voices", EntityKind::Voice},
        {"performerRoles", EntityKind::Role},
        {"measures", EntityKind::Measure},
        {"events", EntityKind::Event},
    };

    std::vector<EntityKind> all_ids;
    all_ids.reserve(64);
    if (score_id_ok) {
        all_ids.push_back(EntityKind{EntityKind::Score, score_id});
    }

    for (std::size_t c = 0; c < sizeof(kCollections) / sizeof(kCollections[0]); ++c) {
        const JsonNode* coll = root.find(kCollections[c].key);
        if (coll == nullptr) {
            fail(ScoreIRDiagnosticCode::MissingRequiredField, std::nullopt,
                 std::string("missing required field '") + kCollections[c].key + "'");
            continue;
        }
        if (coll->kind != JsonNode::Kind::Array) {
            fail(ScoreIRDiagnosticCode::UnknownField, std::nullopt,
                 std::string("'") + kCollections[c].key + "' must be an array");
            continue;
        }
        for (std::size_t i = 0; i < coll->array.size(); ++i) {
            JsonNode const& item = coll->array[i];
            const std::string where = std::string(kCollections[c].key) + "[" + std::to_string(i) + "]";
            if (item.kind != JsonNode::Kind::Object) {
                fail(ScoreIRDiagnosticCode::UnknownField, where,
                     where + " must be an object");
                continue;
            }
            const JsonNode* id_item = item.find("id");
            if (id_item == nullptr || id_item->kind != JsonNode::Kind::String) {
                fail(ScoreIRDiagnosticCode::MissingRequiredField, where,
                     where + " is missing a string 'id'");
                continue;
            }
            EntityId eid = EntityId::from_string("1a2b3c4d-5e6f-4a7b-8c9d-0e1f2a3b4c5d");
            bool id_ok = true;
            try {
                eid = EntityId::from_string(id_item->string);
            } catch (std::invalid_argument const&) {
                id_ok = false;
            }
            if (!id_ok) {
                fail(ScoreIRDiagnosticCode::MalformedEntityId, std::nullopt,
                     where + " has a malformed entity id '" + id_item->string + "'");
                continue;
            }
            all_ids.push_back(EntityKind{kCollections[c].type, eid});
        }
    }

    // Duplicate detection (scoreId + every entity id).
    for (std::size_t i = 0; i < all_ids.size(); ++i) {
        for (std::size_t j = i + 1; j < all_ids.size(); ++j) {
            if (all_ids[i].id == all_ids[j].id) {
                fail(ScoreIRDiagnosticCode::DuplicateEntityId, std::nullopt,
                     "duplicate entity id '" + all_ids[i].id.to_string() + "'");
            }
        }
    }

    auto kind_of = [&all_ids](EntityId const& id) -> std::optional<EntityKind::Type> {
        for (auto const& k : all_ids) {
            if (k.id == id) {
                return k.type;
            }
        }
        return std::nullopt;
    };
    auto check_ref = [&](std::string const& where, EntityId const& id,
                         EntityKind::Type expected) {
        const std::optional<EntityKind::Type> got = kind_of(id);
        if (!got.has_value()) {
            fail(ScoreIRDiagnosticCode::DanglingReference, where,
                 where + " references unknown id '" + id.to_string() + "'");
        } else if (*got != expected) {
            fail(ScoreIRDiagnosticCode::MistypedReference, where,
                 where + " references id '" + id.to_string() + "' with the wrong entity type");
        }
    };

    auto parse_time = [&](std::string const& where, JsonNode const& node,
                          RationalTime& rt) -> bool {
        if (node.kind != JsonNode::Kind::Object) {
            fail(ScoreIRDiagnosticCode::MalformedRationalTime, where,
                 where + " must be a canonical RationalTime object");
            return false;
        }
        for (auto const& kv : node.object) {
            if (kv.first != "numerator" && kv.first != "denominator") {
                fail(ScoreIRDiagnosticCode::UnknownField, where,
                     "unknown field in rational time at " + where + ": \"" + kv.first + "\"");
            }
        }
        const JsonNode* num = node.find("numerator");
        const JsonNode* den = node.find("denominator");
        if (num == nullptr || den == nullptr || num->kind != JsonNode::Kind::String ||
            den->kind != JsonNode::Kind::String) {
            fail(ScoreIRDiagnosticCode::MalformedRationalTime, where,
                 where + " needs string 'numerator' and 'denominator'");
            return false;
        }
        const std::string wire = "{\"numerator\":\"" + num->string + "\",\"denominator\":\"" +
                                 den->string + "\"}";
        try {
            rt = RationalTime::from_canonical_json(wire);
            return true;
        } catch (std::invalid_argument const&) {
            fail(ScoreIRDiagnosticCode::MalformedRationalTime, where,
                 where + " is not a canonical RationalTime");
        } catch (std::out_of_range const&) {
            fail(ScoreIRDiagnosticCode::RationalTimeOutOfRange, where,
                 where + " exceeds the int64 RationalTime range");
        }
        return false;
    };

    // Pass 2: per-collection typed fields and references.
    auto process_entity = [&](std::string const& where, JsonNode const& item,
                              EntityKind::Type type,
                              std::vector<std::string> const& allowed) {
        for (auto const& kv : item.object) {
            if (std::find(allowed.begin(), allowed.end(), kv.first) == allowed.end()) {
                reject_field(where, kv.first);
            }
        }
        auto need_string = [&](char const* key) -> std::string {
            const JsonNode* v = item.find(key);
            if (v == nullptr || v->kind != JsonNode::Kind::String) {
                fail(ScoreIRDiagnosticCode::MissingRequiredField, where,
                     where + " is missing a string '" + key + "'");
                return std::string();
            }
            return v->string;
        };
        const std::string id_s = need_string("id");
        if (id_s.empty()) {
            return;
        }
        EntityId id = EntityId::from_string("1a2b3c4d-5e6f-4a7b-8c9d-0e1f2a3b4c5d");
        bool id_parsed = true;
        try {
            id = EntityId::from_string(id_s);
        } catch (std::invalid_argument const&) {
            id_parsed = false;  // malformed id already reported in the first pass
        }
        if (!id_parsed) {
            return;
        }

        if (type == EntityKind::Part) {
            const std::string type_s = need_string("type");
            if (!type_s.empty() && type_s != "choral") {
                fail(ScoreIRDiagnosticCode::UnsupportedPartType, where + ".type",
                     "unsupported part type '" + type_s +
                         "' (this draft ScoreIR schema is choral-only; accompaniment parts are a future minimal variant)");
            }
            (void)need_string("name");
        } else if (type == EntityKind::Staff) {
            const std::string ref = need_string("partRef");
            (void)need_string("label");
            if (!ref.empty()) {
                try {
                    check_ref(where + ".partRef", EntityId::from_string(ref), EntityKind::Part);
                } catch (std::invalid_argument const&) {
                    fail(ScoreIRDiagnosticCode::MalformedEntityId, where + ".partRef",
                         "malformed partRef '" + ref + "'");
                }
            }
        } else if (type == EntityKind::Voice) {
            const std::string ref = need_string("staffRef");
            (void)need_string("label");
            if (!ref.empty()) {
                try {
                    check_ref(where + ".staffRef", EntityId::from_string(ref), EntityKind::Staff);
                } catch (std::invalid_argument const&) {
                    fail(ScoreIRDiagnosticCode::MalformedEntityId, where + ".staffRef",
                         "malformed staffRef '" + ref + "'");
                }
            }
        } else if (type == EntityKind::Role) {
            (void)need_string("label");
        } else if (type == EntityKind::Measure) {
            const JsonNode* idx = item.find("index");
            if (idx == nullptr || idx->kind != JsonNode::Kind::Number) {
                fail(ScoreIRDiagnosticCode::MissingRequiredField, where,
                     where + " is missing a numeric 'index'");
            } else if (!is_integer_token(idx->number) || idx->number.find('-') != std::string::npos) {
                fail(ScoreIRDiagnosticCode::MalformedPitch, where + ".index",
                     "measure index must be a non-negative integer");
            }
            const JsonNode* num = item.find("number");
            if (num == nullptr || num->kind != JsonNode::Kind::String) {
                fail(ScoreIRDiagnosticCode::MissingRequiredField, where,
                     where + " is missing a string 'number'");
            }
            const JsonNode* pickup = item.find("pickup");
            if (pickup == nullptr || pickup->kind != JsonNode::Kind::Bool) {
                fail(ScoreIRDiagnosticCode::MissingRequiredField, where,
                     where + " is missing a boolean 'pickup'");
            }
            // Canonical exact actualDuration; must be positive.
            const JsonNode* dur = item.find("actualDuration");
            if (dur == nullptr) {
                fail(ScoreIRDiagnosticCode::MissingRequiredField, where,
                     where + " is missing 'actualDuration'");
            } else {
                RationalTime actual;
                if (parse_time(where + ".actualDuration", *dur, actual) && !actual.is_positive()) {
                    fail(ScoreIRDiagnosticCode::NonPositiveDuration, where + ".actualDuration",
                         where + ".actualDuration must be positive");
                }
            }
        } else if (type == EntityKind::Event) {
            // Tagged variants: note / rest / opaqueNotation / unsupportedRelation.
            const std::string kind_s = need_string("kind");

            // Variant-aware closed-field check: each kind allows only its own
            // fields, matching the public schema's per-variant
            // additionalProperties (a rest carrying 'raw' or 'pitch', a note
            // carrying 'raw', or an opaque variant carrying duration/pitch/
            // voice/roles are all rejected the same way the schema does).
            std::vector<std::string> event_allowed;
            if (kind_s == "note") {
                event_allowed = {"id", "kind", "measureRef", "offset", "duration",
                                 "pitch", "roleRefs", "voiceRef"};
            } else if (kind_s == "rest") {
                event_allowed = {"id", "kind", "measureRef", "offset", "duration",
                                 "voiceRef", "roleRefs"};
            } else if (kind_s == "opaqueNotation" || kind_s == "unsupportedRelation" || kind_s == "UnknownSymbol") {
                event_allowed = {"id", "kind", "measureRef", "offset", "raw"};
            } else {
                event_allowed = {"id", "kind", "measureRef", "offset"};
            }
            for (auto const& kv : item.object) {
                if (std::find(event_allowed.begin(), event_allowed.end(), kv.first) ==
                    event_allowed.end()) {
                    reject_field(where, kv.first);
                }
            }
            if (!kind_s.empty() && kind_s != "note" && kind_s != "rest" &&
                kind_s != "opaqueNotation" && kind_s != "unsupportedRelation" && kind_s != "UnknownSymbol") {
                fail(ScoreIRDiagnosticCode::UnknownEventKind, where + ".kind",
                     "unknown event kind '" + kind_s + "'");
            }
            const std::string measure_ref = need_string("measureRef");
            if (!measure_ref.empty()) {
                try {
                    check_ref(where + ".measureRef", EntityId::from_string(measure_ref),
                              EntityKind::Measure);
                } catch (std::invalid_argument const&) {
                    fail(ScoreIRDiagnosticCode::MalformedEntityId, where + ".measureRef",
                         "malformed measureRef '" + measure_ref + "'");
                }
            }
            const JsonNode* offset_n = item.find("offset");
            if (offset_n == nullptr) {
                fail(ScoreIRDiagnosticCode::MissingRequiredField, where,
                     where + " is missing 'offset'");
            } else {
                RationalTime offset;
                if (parse_time(where + ".offset", *offset_n, offset) && offset.is_negative()) {
                    fail(ScoreIRDiagnosticCode::NegativeOffset, where + ".offset",
                         where + ".offset must be non-negative");
                }
            }

            if (kind_s == "opaqueNotation" || kind_s == "unsupportedRelation" || kind_s == "UnknownSymbol") {
                // Explicit source state WITHOUT fabricated pitch/duration/voice/roles;
                // the variant-aware allowed-field check above already rejects any
                // such fields (parity with the schema's additionalProperties).
                const JsonNode* raw = item.find("raw");
                if (raw == nullptr || raw->kind != JsonNode::Kind::String) {
                    fail(ScoreIRDiagnosticCode::MissingRequiredField, where + ".raw",
                         "opaque/unsupported event needs a string 'raw' (lossless preservation)");
                }
            } else {
                // Standard note/rest variants: positive duration required.
                const JsonNode* duration_n = item.find("duration");
                if (duration_n == nullptr) {
                    fail(ScoreIRDiagnosticCode::MissingRequiredField, where,
                         where + " is missing 'duration'");
                } else {
                    RationalTime duration;
                    if (parse_time(where + ".duration", *duration_n, duration) &&
                        !duration.is_positive()) {
                        fail(ScoreIRDiagnosticCode::NonPositiveDuration, where + ".duration",
                             where + ".duration must be positive");
                    }
                }
                const JsonNode* voice_ref = item.find("voiceRef");
                if (voice_ref == nullptr || voice_ref->kind != JsonNode::Kind::String) {
                    fail(ScoreIRDiagnosticCode::MissingRequiredField, where,
                         where + " is missing a string 'voiceRef'");
                } else {
                    try {
                        check_ref(where + ".voiceRef", EntityId::from_string(voice_ref->string),
                                  EntityKind::Voice);
                    } catch (std::invalid_argument const&) {
                        fail(ScoreIRDiagnosticCode::MalformedEntityId, where + ".voiceRef",
                             "malformed voiceRef '" + voice_ref->string + "'");
                    }
                }
            }

            if (kind_s == "note") {
                const JsonNode* pitch_n = item.find("pitch");
                if (pitch_n == nullptr || pitch_n->kind != JsonNode::Kind::Object) {
                    fail(ScoreIRDiagnosticCode::MissingRequiredField, where,
                         where + " is missing a 'pitch' object");
                } else {
                    for (auto const& kv : pitch_n->object) {
                        if (kv.first != "written" && kv.first != "displayAccidental" &&
                            kv.first != "sounding") {
                            fail(ScoreIRDiagnosticCode::UnknownField, where + ".pitch",
                                 "unknown field at " + where + ".pitch: \"" + kv.first + "\"");
                        }
                    }
                    auto check_spelling = [&](std::string const& field, JsonNode const& node) {
                        if (node.kind != JsonNode::Kind::Object) {
                            fail(ScoreIRDiagnosticCode::MalformedPitch, field,
                                 field + " must be an object");
                            return;
                        }
                        for (auto const& kv : node.object) {
                            if (kv.first != "letter" && kv.first != "accidental" &&
                                kv.first != "octave") {
                                fail(ScoreIRDiagnosticCode::UnknownField, field,
                                     "unknown field at " + field + ": \"" + kv.first + "\"");
                            }
                        }
                        const JsonNode* letter = node.find("letter");
                        const JsonNode* acc = node.find("accidental");
                        const JsonNode* oct = node.find("octave");
                        if (letter == nullptr || letter->kind != JsonNode::Kind::String ||
                            letter->string.size() != 1 || !valid_letter(letter->string[0])) {
                            fail(ScoreIRDiagnosticCode::MalformedPitch, field,
                                 field + " needs a letter in A-G");
                        }
                        if (acc == nullptr || acc->kind != JsonNode::Kind::String ||
                            !known_accidental(acc->string)) {
                            fail(ScoreIRDiagnosticCode::MalformedPitch, field,
                                 field + " has an unknown accidental");
                        }
                        if (oct == nullptr || oct->kind != JsonNode::Kind::Number ||
                            !is_integer_token(oct->number)) {
                            fail(ScoreIRDiagnosticCode::MalformedPitch, field,
                                 field + " needs an integer 'octave'");
                        } else if (!(oct->number.size() == 1 && oct->number[0] >= '0' &&
                                     oct->number[0] <= '9')) {
                            fail(ScoreIRDiagnosticCode::MalformedPitch, field,
                                 field + " octave must be in 0..9");
                        }
                    };
                    const JsonNode* written = pitch_n->find("written");
                    const JsonNode* sounding = pitch_n->find("sounding");
                    if (written == nullptr) {
                        fail(ScoreIRDiagnosticCode::MissingRequiredField, where + ".pitch",
                             where + ".pitch is missing 'written'");
                    } else {
                        check_spelling(where + ".pitch.written", *written);
                    }
                    if (sounding == nullptr) {
                        fail(ScoreIRDiagnosticCode::MissingRequiredField, where + ".pitch",
                             where + ".pitch is missing 'sounding'");
                    } else {
                        check_spelling(where + ".pitch.sounding", *sounding);
                    }
                    const JsonNode* display = pitch_n->find("displayAccidental");
                    if (display == nullptr || display->kind != JsonNode::Kind::String) {
                        fail(ScoreIRDiagnosticCode::MissingRequiredField, where + ".pitch",
                             where + ".pitch is missing a string 'displayAccidental'");
                    }
                }
                const JsonNode* role_refs = item.find("roleRefs");
                if (role_refs == nullptr || role_refs->kind != JsonNode::Kind::Array) {
                    fail(ScoreIRDiagnosticCode::MissingRequiredField, where,
                         where + " is missing a 'roleRefs' array");
                } else {
                    if (role_refs->array.empty()) {
                        fail(ScoreIRDiagnosticCode::EmptyRoleRefs, where + ".roleRefs",
                             where + ".roleRefs must not be empty");
                    }
                    for (std::size_t r = 0; r < role_refs->array.size(); ++r) {
                        const JsonNode& rr = role_refs->array[r];
                        if (rr.kind != JsonNode::Kind::String) {
                            fail(ScoreIRDiagnosticCode::MistypedReference, where + ".roleRefs",
                                 "roleRefs entries must be strings at " + where);
                            continue;
                        }
                        try {
                            check_ref(where + ".roleRefs[" + std::to_string(r) + "]",
                                      EntityId::from_string(rr.string), EntityKind::Role);
                        } catch (std::invalid_argument const&) {
                            fail(ScoreIRDiagnosticCode::MalformedEntityId,
                                 where + ".roleRefs[" + std::to_string(r) + "]",
                                 "malformed roleRef '" + rr.string + "'");
                        }
                    }
                    // Duplicate role refs (exact string comparison).
                    for (std::size_t r = 0; r < role_refs->array.size(); ++r) {
                        for (std::size_t s = r + 1; s < role_refs->array.size(); ++s) {
                            if (role_refs->array[r].kind == JsonNode::Kind::String &&
                                role_refs->array[s].kind == JsonNode::Kind::String &&
                                role_refs->array[r].string == role_refs->array[s].string) {
                                fail(ScoreIRDiagnosticCode::DuplicateRoleRef,
                                     where + ".roleRefs",
                                     "duplicate roleRef '" + role_refs->array[r].string +
                                         "' at " + where);
                            }
                        }
                    }
                }
            } else if (kind_s == "rest") {
                // rest: no pitch required; roleRefs optional but each entry must
                // be a string referencing a role (parity with the schema).
                if (const JsonNode* role_refs = item.find("roleRefs"); role_refs != nullptr) {
                    if (role_refs->kind != JsonNode::Kind::Array) {
                        fail(ScoreIRDiagnosticCode::UnknownField, where + ".roleRefs",
                             "roleRefs must be an array at " + where);
                    } else {
                        for (std::size_t r = 0; r < role_refs->array.size(); ++r) {
                            const JsonNode& rr = role_refs->array[r];
                            if (rr.kind != JsonNode::Kind::String) {
                                fail(ScoreIRDiagnosticCode::MistypedReference,
                                     where + ".roleRefs[" + std::to_string(r) + "]",
                                     "roleRefs entries must be strings at " + where);
                                continue;
                            }
                            try {
                                check_ref(where + ".roleRefs[" + std::to_string(r) + "]",
                                          EntityId::from_string(rr.string), EntityKind::Role);
                            } catch (std::invalid_argument const&) {
                                fail(ScoreIRDiagnosticCode::MalformedEntityId,
                                     where + ".roleRefs[" + std::to_string(r) + "]",
                                     "malformed roleRef '" + rr.string + "'");
                            }
                        }
                    }
                }
            }
        }
    };

    for (std::size_t c = 0; c < sizeof(kCollections) / sizeof(kCollections[0]); ++c) {
        const JsonNode* coll = root.find(kCollections[c].key);
        if (coll == nullptr || coll->kind != JsonNode::Kind::Array) {
            continue;
        }
        for (std::size_t i = 0; i < coll->array.size(); ++i) {
            JsonNode const& item = coll->array[i];
            if (item.kind != JsonNode::Kind::Object) {
                continue;
            }
            const std::string where = std::string(kCollections[c].key) + "[" + std::to_string(i) + "]";
            if (kCollections[c].type == EntityKind::Part) {
                process_entity(where, item, EntityKind::Part, {"id", "type", "name"});
            } else if (kCollections[c].type == EntityKind::Staff) {
                process_entity(where, item, EntityKind::Staff, {"id", "partRef", "label"});
            } else if (kCollections[c].type == EntityKind::Voice) {
                process_entity(where, item, EntityKind::Voice, {"id", "staffRef", "label"});
            } else if (kCollections[c].type == EntityKind::Role) {
                process_entity(where, item, EntityKind::Role, {"id", "label"});
            } else if (kCollections[c].type == EntityKind::Measure) {
                process_entity(where, item, EntityKind::Measure,
                               {"id", "index", "number", "pickup", "actualDuration"});
            } else {
                process_entity(where, item, EntityKind::Event,
                               {"id", "kind", "measureRef", "offset", "duration", "pitch",
                                "roleRefs", "voiceRef", "raw"});
            }
        }
    }

    if (!diags.empty()) {
        return false;
    }

    // Build the canonical document (RationalTime fields re-serialized
    // canonically; key order is canonicalized recursively by the writer).
    JsonNode canon;
    canon.kind = JsonNode::Kind::Object;
    auto add = [&canon](std::string key, JsonNode value) {
        canon.object.emplace_back(std::move(key), std::move(value));
    };
    auto str = [](std::string const& s) {
        JsonNode n;
        n.kind = JsonNode::Kind::String;
        n.string = s;
        return n;
    };
    auto time_node = [&](JsonNode const& t) {
        JsonNode ct;
        ct.kind = JsonNode::Kind::Object;
        if (const JsonNode* n = t.find("numerator"); n != nullptr) {
            ct.object.emplace_back("numerator", *n);
        }
        if (const JsonNode* d = t.find("denominator"); d != nullptr) {
            ct.object.emplace_back("denominator", *d);
        }
        return ct;
    };

    add("$schema", str(kScoreIRSchemaId));
    add("version", str(kScoreIRVersion));
    add("scoreId", str(score_id.to_string()));
    if (const JsonNode* meta = root.find("metadata"); meta != nullptr) {
        JsonNode m;
        m.kind = JsonNode::Kind::Object;
        if (const JsonNode* title = meta->find("title"); title != nullptr) {
            m.object.emplace_back("title", *title);
        }
        add("metadata", std::move(m));
    }
    for (std::size_t c = 0; c < sizeof(kCollections) / sizeof(kCollections[0]); ++c) {
        const JsonNode* coll = root.find(kCollections[c].key);
        JsonNode arr;
        arr.kind = JsonNode::Kind::Array;
        if (coll != nullptr && coll->kind == JsonNode::Kind::Array) {
            if (kCollections[c].type == EntityKind::Event) {
                for (auto const& e : coll->array) {
                    JsonNode ce;
                    ce.kind = JsonNode::Kind::Object;
                    auto copy_key = [&ce, &e](char const* k) {
                        if (const JsonNode* v = e.find(k); v != nullptr) {
                            ce.object.emplace_back(k, *v);
                        }
                    };
                    copy_key("id");
                    copy_key("kind");
                    copy_key("measureRef");
                    if (const JsonNode* t = e.find("offset"); t != nullptr) {
                        ce.object.emplace_back("offset", time_node(*t));
                    }
                    if (const JsonNode* t = e.find("duration"); t != nullptr) {
                        ce.object.emplace_back("duration", time_node(*t));
                    }
                    copy_key("pitch");
                    copy_key("roleRefs");
                    copy_key("voiceRef");
                    copy_key("raw");
                    arr.array.push_back(std::move(ce));
                }
            } else {
                arr.array = coll->array;
            }
        }
        add(kCollections[c].key, std::move(arr));
    }

    // Counts.
    std::size_t parts = 0, staves = 0, voices = 0, roles = 0, measures = 0, events = 0,
                 pickups = 0;
    auto count_arr = [&](char const* key, std::size_t& n) {
        if (const JsonNode* coll = root.find(key); coll != nullptr && coll->kind == JsonNode::Kind::Array) {
            n = coll->array.size();
        }
    };
    count_arr("parts", parts);
    count_arr("staves", staves);
    count_arr("voices", voices);
    count_arr("performerRoles", roles);
    count_arr("measures", measures);
    count_arr("events", events);
    if (const JsonNode* measures_n = root.find("measures");
        measures_n != nullptr && measures_n->kind == JsonNode::Kind::Array) {
        for (auto const& m : measures_n->array) {
            if (const JsonNode* p = m.find("pickup"); p != nullptr && p->kind == JsonNode::Kind::Bool && p->boolean) {
                ++pickups;
            }
        }
    }

    out.score_id_ = score_id;
    out.parts_ = parts;
    out.staves_ = staves;
    out.voices_ = voices;
    out.roles_ = roles;
    out.measures_ = measures;
    out.events_ = events;
    out.pickups_ = pickups;
    out.canonical_json_ = json_write(canon);
    return true;
}

ScoreIRParseResult ScoreIRDocument::from_json(std::string_view json)
{
    ScoreIRParseResult result;
    ScoreIRDocument doc;
    if (validate_document(json, doc, result.diagnostics)) {
        result.document = std::move(doc);
    }
    return result;
}

std::string ScoreIRDocument::to_json() const { return canonical_json_; }

ScoreIRValidationResult validate_scoreir(std::string_view json)
{
    ScoreIRValidationResult result;
    const ScoreIRParseResult r = ScoreIRDocument::from_json(json);
    result.diagnostics = r.diagnostics;
    return result;
}

}  // namespace choirloom::score
