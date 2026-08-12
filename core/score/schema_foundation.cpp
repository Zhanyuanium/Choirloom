// ============================================================================
// core/score/schema_foundation.cpp - M0-004 Schema / Versioning Foundation.
// See schema_foundation.h for the authoritative contract.
// ============================================================================

#include "schema_foundation.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>

namespace choirloom::score {
namespace {

// ---------------------------------------------------------------------------
// Minimal dependency-free JSON reader (catalog + on-disk schema inspection).
// Strict: duplicate object keys are rejected; numbers keep their raw text.
// ---------------------------------------------------------------------------
struct JsonNode {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    std::string string;
    std::string number;  // raw numeric token text (numbers are NOT coerced)
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
            return n;
        }
        if (c == 'f') {
            expect_literal("false");
            JsonNode n;
            n.kind = JsonNode::Kind::Bool;
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
        // Strict JSON number grammar (RFC 8259): the integer part is
        // `0 | [1-9][0-9]*` - leading zeros such as "01" (or "-01") are
        // invalid.
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
        ++pos_;  // consume '{'
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
            // Strict: duplicate object keys are rejected.
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
        ++pos_;  // consume '['
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
        ++pos_;  // consume '"'
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
                        // Append code point as UTF-8.
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

// Recursively collect every "$ref" string value.
void collect_refs(JsonNode const& node, std::vector<std::string>& out)
{
    if (node.kind == JsonNode::Kind::Object) {
        for (auto const& kv : node.object) {
            if (kv.first == "$ref" && kv.second.kind == JsonNode::Kind::String) {
                out.push_back(kv.second.string);
            }
            collect_refs(kv.second, out);
        }
    } else if (node.kind == JsonNode::Kind::Array) {
        for (auto const& item : node.array) {
            collect_refs(item, out);
        }
    }
}

const char* kJsonSchemaDialectId = "https://json-schema.org/draft/2020-12/schema";

// --- SchemaVersion parsing --------------------------------------------------
int parse_component_digit(char c)
{
    return (c >= '0' && c <= '9') ? c - '0' : -1;
}

// Strict single component. Grammar violations throw std::invalid_argument;
// overflow beyond uint32 throws std::out_of_range.
std::uint32_t parse_uint32_component(std::string_view seg)
{
    if (seg.empty()) {
        throw std::invalid_argument("SchemaVersion: empty component");
    }
    if (seg[0] == '0') {
        if (seg.size() != 1) {
            throw std::invalid_argument("SchemaVersion: leading zero in component");
        }
        return 0;
    }
    std::uint64_t v = 0;
    for (char c : seg) {
        const int d = parse_component_digit(c);
        if (d < 0) {
            throw std::invalid_argument("SchemaVersion: non-digit in component");
        }
        v = v * 10 + static_cast<std::uint64_t>(d);
        if (v > std::numeric_limits<std::uint32_t>::max()) {
            throw std::out_of_range("SchemaVersion: component exceeds uint32 range");
        }
    }
    return static_cast<std::uint32_t>(v);
}

// --- SchemaId parsing -------------------------------------------------------
bool valid_kebab(std::string_view s)
{
    if (s.empty()) {
        return false;
    }
    bool prev_hyphen = true;  // start of a segment
    for (char c : s) {
        if (c == '-') {
            if (prev_hyphen) {
                return false;
            }
            prev_hyphen = true;
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            prev_hyphen = false;
        } else {
            return false;
        }
    }
    return !prev_hyphen;  // must not end with '-'
}

bool is_absolute_path(std::string const& p)
{
    if (p.empty()) {
        return false;
    }
    if (p[0] == '/' || p[0] == '\\') {
        return true;
    }
    // Windows drive letter, e.g. "C:/..." or "C:\..."
    return p.size() >= 2 &&
           ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':';
}

bool has_traversal(std::string const& p)
{
    // Split on '/' or '\\' and reject any ".." segment.
    std::size_t start = 0;
    for (std::size_t i = 0; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '/' || p[i] == '\\') {
            if (i - start == 2 && p[start] == '.' && p[start + 1] == '.') {
                return true;
            }
            start = i + 1;
        }
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// SchemaVersion
// ---------------------------------------------------------------------------
SchemaVersion SchemaVersion::from_string(std::string_view s)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '.') {
            parts.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    if (parts.size() != 3) {
        throw std::invalid_argument("SchemaVersion: expected exactly three dot-separated components");
    }
    return SchemaVersion(parse_uint32_component(parts[0]),
                         parse_uint32_component(parts[1]),
                         parse_uint32_component(parts[2]));
}

SchemaVersion SchemaVersion::from_components(Component major, Component minor,
                                             Component patch) noexcept
{
    return SchemaVersion(major, minor, patch);
}

std::string SchemaVersion::to_string() const
{
    return std::to_string(major_) + "." + std::to_string(minor_) + "." +
           std::to_string(patch_);
}

// ---------------------------------------------------------------------------
// SchemaId
// ---------------------------------------------------------------------------
SchemaId SchemaId::from_string(std::string_view s)
{
    constexpr std::string_view kPrefix = "choirloom:score/";
    if (s.size() <= kPrefix.size() || s.substr(0, kPrefix.size()) != kPrefix) {
        throw std::invalid_argument("SchemaId: must be canonical 'choirloom:score/<kebab-name>'");
    }
    if (!valid_kebab(s.substr(kPrefix.size()))) {
        throw std::invalid_argument("SchemaId: name must be lower-case kebab (a-z0-9 with single '-' separators)");
    }
    return SchemaId(std::string(s));
}

std::string SchemaId::to_string() const { return value_; }

// ---------------------------------------------------------------------------
// SchemaDocumentId
// ---------------------------------------------------------------------------
SchemaDocumentId SchemaDocumentId::from_string(std::string_view s)
{
    const std::size_t slash = s.find_last_of('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 >= s.size()) {
        throw std::invalid_argument("SchemaDocumentId: expected canonical '<SchemaId>/<SchemaVersion>'");
    }
    const SchemaId id = SchemaId::from_string(s.substr(0, slash));
    const SchemaVersion version = SchemaVersion::from_string(s.substr(slash + 1));
    return SchemaDocumentId(id, version);
}

SchemaDocumentId SchemaDocumentId::from_parts(SchemaId id, SchemaVersion version)
{
    return SchemaDocumentId(std::move(id), version);
}

std::string SchemaDocumentId::to_string() const
{
    return id_.to_string() + "/" + version_.to_string();
}

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------
SchemaStatus schema_status_from_string(std::string_view s)
{
    if (s == "Draft") {
        return SchemaStatus::Draft;
    }
    if (s == "Stable") {
        return SchemaStatus::Stable;
    }
    if (s == "Deprecated") {
        return SchemaStatus::Deprecated;
    }
    throw std::invalid_argument("unknown SchemaStatus: \"" + std::string(s) + "\"");
}

SchemaRuleKind schema_rule_kind_from_string(std::string_view s)
{
    if (s == "readable") {
        return SchemaRuleKind::Readable;
    }
    if (s == "migration") {
        return SchemaRuleKind::Migration;
    }
    throw std::invalid_argument("unknown SchemaRuleKind: \"" + std::string(s) + "\"");
}

// ---------------------------------------------------------------------------
// SchemaCatalog
// ---------------------------------------------------------------------------
SchemaCatalog SchemaCatalogBuilder::build() const
{
    SchemaCatalog c;
    c.format_ = format_;
    c.entries_ = entries_;
    c.rules_ = rules_;
    std::sort(c.entries_.begin(), c.entries_.end(),
              [](SchemaCatalogEntry const& a, SchemaCatalogEntry const& b) {
                  if (a.schema_id != b.schema_id) {
                      return a.schema_id < b.schema_id;
                  }
                  return a.schema_version < b.schema_version;
              });
    std::sort(c.rules_.begin(), c.rules_.end(),
              [](SchemaCatalogRule const& a, SchemaCatalogRule const& b) {
                  if (a.from != b.from) {
                      return a.from < b.from;
                  }
                  if (a.to != b.to) {
                      return a.to < b.to;
                  }
                  return static_cast<int>(a.kind) < static_cast<int>(b.kind);
              });
    return c;
}

SchemaCatalogLoadResult SchemaCatalog::from_json(std::string_view json)
{
    SchemaCatalogLoadResult result;
    JsonNode root;
    try {
        root = json_parse(json);
    } catch (JsonParseError const& e) {
        result.diagnostics.push_back(ValidationDiagnostic{
            DiagnosticCode::MalformedCatalogJson, std::nullopt, std::nullopt,
            std::nullopt, std::string("catalog JSON is malformed: ") + e.what()});
        return result;
    }
    if (root.kind != JsonNode::Kind::Object) {
        result.diagnostics.push_back(ValidationDiagnostic{
            DiagnosticCode::MalformedCatalogJson, std::nullopt, std::nullopt,
            std::nullopt, "catalog root must be a JSON object"});
        return result;
    }

    auto fail = [&](std::string const& msg) {
        result.diagnostics.push_back(ValidationDiagnostic{
            DiagnosticCode::MalformedCatalogJson, std::nullopt, std::nullopt,
            std::nullopt, msg});
    };

    // Strict: unknown top-level fields are rejected.
    for (auto const& kv : root.object) {
        if (kv.first != "$schema" && kv.first != "description" &&
            kv.first != "catalogFormatVersion" && kv.first != "entries" &&
            kv.first != "rules") {
            fail("unknown catalog field: \"" + kv.first + "\"");
        }
    }

    // A catalog INSTANCE must bind to its schema document via "$schema" with
    // the exact canonical value; missing/mismatched/non-string is rejected.
    const JsonNode* schema_binding = root.find("$schema");
    if (schema_binding == nullptr) {
        fail("catalog instance is missing its '$schema' binding (expected 'choirloom:score/schema-catalog/0.1.0')");
    } else if (schema_binding->kind != JsonNode::Kind::String) {
        fail("'$schema' must be a string identifying the catalog schema document");
    } else if (schema_binding->string != "choirloom:score/schema-catalog/0.1.0") {
        fail("'$schema' must be exactly 'choirloom:score/schema-catalog/0.1.0' (found '" +
             schema_binding->string + "')");
    }

    // catalogFormatVersion is read from the parsed node (raw numeric token);
    // a malformed or unsupported format rejects the catalog before it is ever
    // returned.
    std::uint32_t fmt = 1;
    bool fmt_valid = false;
    const JsonNode* fmt_node = root.find("catalogFormatVersion");
    if (fmt_node == nullptr) {
        fail("catalog is missing 'catalogFormatVersion'");
    } else if (fmt_node->kind != JsonNode::Kind::Number) {
        fail("'catalogFormatVersion' must be a number");
    } else {
        bool overflow = false;
        std::uint64_t v = 0;
        for (char c : fmt_node->number) {
            if (c < '0' || c > '9') {
                overflow = true;
                break;
            }
            v = v * 10 + static_cast<std::uint64_t>(c - '0');
            if (v > std::numeric_limits<std::uint32_t>::max()) {
                overflow = true;
                break;
            }
        }
        if (overflow || fmt_node->number.empty()) {
            fail("'catalogFormatVersion' is out of the supported uint32 range");
        } else {
            fmt = static_cast<std::uint32_t>(v);
            fmt_valid = true;
        }
    }
    if (fmt_valid && fmt != 1) {
        result.diagnostics.push_back(ValidationDiagnostic{
            DiagnosticCode::CatalogFormatUnsupported, std::nullopt, std::nullopt,
            std::nullopt,
            "unsupported catalogFormatVersion " + std::to_string(fmt) +
                " (supported: 1)"});
    }
    if (!result.diagnostics.empty()) {
        return result;  // malformed/unsupported format: no catalog is returned
    }

    SchemaCatalogBuilder builder;
    builder.catalog_format_version(fmt);

    const JsonNode* entries = root.find("entries");
    if (entries == nullptr || entries->kind != JsonNode::Kind::Array) {
        fail("catalog is missing an 'entries' array");
        return result;
    }
    if (entries->array.empty()) {
        fail("catalog 'entries' must not be empty");
        return result;
    }

    for (std::size_t i = 0; i < entries->array.size(); ++i) {
        JsonNode const& e = entries->array[i];
        if (e.kind != JsonNode::Kind::Object) {
            fail("entry " + std::to_string(i) + " is not an object");
            continue;
        }
        for (auto const& kv : e.object) {
            if (kv.first != "schemaId" && kv.first != "schemaVersion" &&
                kv.first != "documentId" && kv.first != "path" && kv.first != "status") {
                fail("entry " + std::to_string(i) + " has unknown field \"" + kv.first + "\"");
            }
        }
        const JsonNode* id_n = e.find("schemaId");
        const JsonNode* ver_n = e.find("schemaVersion");
        const JsonNode* doc_n = e.find("documentId");
        const JsonNode* path_n = e.find("path");
        const JsonNode* status_n = e.find("status");
        if (id_n == nullptr || ver_n == nullptr || doc_n == nullptr ||
            path_n == nullptr || status_n == nullptr) {
            fail("entry " + std::to_string(i) +
                 " is missing one of schemaId/schemaVersion/documentId/path/status");
            continue;
        }
        if (id_n->kind != JsonNode::Kind::String || ver_n->kind != JsonNode::Kind::String ||
            doc_n->kind != JsonNode::Kind::String || path_n->kind != JsonNode::Kind::String ||
            status_n->kind != JsonNode::Kind::String) {
            fail("entry " + std::to_string(i) + " has non-string field values");
            continue;
        }
        SchemaId schema_id = SchemaId::from_string("choirloom:score/placeholder");
        bool ok_id = true;
        try {
            schema_id = SchemaId::from_string(id_n->string);
        } catch (std::invalid_argument const&) {
            ok_id = false;
        }
        if (!ok_id) {
            result.diagnostics.push_back(ValidationDiagnostic{
                DiagnosticCode::SchemaIdFormat, std::nullopt, std::nullopt,
                std::nullopt, "entry " + std::to_string(i) + " has a malformed schemaId"});
            continue;
        }
        SchemaVersion version = SchemaVersion::from_components(0, 0, 0);
        bool ok_ver = true;
        try {
            version = SchemaVersion::from_string(ver_n->string);
        } catch (std::invalid_argument const&) {
            ok_ver = false;
            result.diagnostics.push_back(ValidationDiagnostic{
                DiagnosticCode::SchemaVersionFormat, std::nullopt, std::nullopt,
                std::nullopt, "entry " + std::to_string(i) + " has a malformed schemaVersion"});
        } catch (std::out_of_range const&) {
            ok_ver = false;
            result.diagnostics.push_back(ValidationDiagnostic{
                DiagnosticCode::SchemaVersionOutOfRange, std::nullopt, std::nullopt,
                std::nullopt,
                "entry " + std::to_string(i) + " has a schemaVersion out of uint32 range"});
        }
        if (!ok_ver) {
            continue;
        }
        SchemaDocumentId document_id =
            SchemaDocumentId::from_string("choirloom:score/placeholder/0.0.0");
        bool ok_doc = true;
        try {
            document_id = SchemaDocumentId::from_string(doc_n->string);
        } catch (std::invalid_argument const&) {
            ok_doc = false;
            result.diagnostics.push_back(ValidationDiagnostic{
                DiagnosticCode::SchemaDocumentIdFormat, std::nullopt, std::nullopt,
                std::nullopt, "entry " + std::to_string(i) + " has a malformed documentId"});
        } catch (std::out_of_range const&) {
            ok_doc = false;
            result.diagnostics.push_back(ValidationDiagnostic{
                DiagnosticCode::SchemaVersionOutOfRange, std::nullopt, std::nullopt,
                std::nullopt,
                "entry " + std::to_string(i) +
                    " has a documentId with a schemaVersion out of uint32 range"});
        }
        if (!ok_doc) {
            continue;
        }
        SchemaStatus status = SchemaStatus::Draft;
        bool ok_status = true;
        try {
            status = schema_status_from_string(status_n->string);
        } catch (std::invalid_argument const&) {
            ok_status = false;
        }
        if (!ok_status) {
            fail("entry " + std::to_string(i) + " has an unknown status");
            continue;
        }
        builder.add_entry(SchemaCatalogEntry{schema_id, version, document_id,
                                             path_n->string, status});
    }

    const JsonNode* rules = root.find("rules");
    if (rules != nullptr) {
        if (rules->kind != JsonNode::Kind::Array) {
            fail("'rules' must be an array");
        } else {
            for (std::size_t i = 0; i < rules->array.size(); ++i) {
                JsonNode const& r = rules->array[i];
                if (r.kind != JsonNode::Kind::Object) {
                    fail("rule " + std::to_string(i) + " is not an object");
                    continue;
                }
                for (auto const& kv : r.object) {
                    if (kv.first != "from" && kv.first != "to" && kv.first != "kind") {
                        fail("rule " + std::to_string(i) + " has unknown field \"" + kv.first + "\"");
                    }
                }
                const JsonNode* from_n = r.find("from");
                const JsonNode* to_n = r.find("to");
                const JsonNode* kind_n = r.find("kind");
                if (from_n == nullptr || to_n == nullptr || kind_n == nullptr) {
                    fail("rule " + std::to_string(i) + " is missing from/to/kind");
                    continue;
                }
                if (from_n->kind != JsonNode::Kind::String ||
                    to_n->kind != JsonNode::Kind::String ||
                    kind_n->kind != JsonNode::Kind::String) {
                    fail("rule " + std::to_string(i) + " has non-string field values");
                    continue;
                }
                try {
                    builder.add_rule(SchemaCatalogRule{
                        SchemaDocumentId::from_string(from_n->string),
                        SchemaDocumentId::from_string(to_n->string),
                        schema_rule_kind_from_string(kind_n->string)});
                } catch (std::invalid_argument const&) {
                    fail("rule " + std::to_string(i) + " has malformed from/to/kind");
                } catch (std::out_of_range const&) {
                    result.diagnostics.push_back(ValidationDiagnostic{
                        DiagnosticCode::SchemaVersionOutOfRange, std::nullopt, std::nullopt,
                        std::nullopt,
                        "rule " + std::to_string(i) +
                            " has a from/to documentId with a schemaVersion out of uint32 range"});
                }
            }
        }
    }

    if (!result.diagnostics.empty()) {
        return result;  // strict: no catalog is returned with parse problems
    }
    result.catalog = builder.build();
    return result;
}

// ---------------------------------------------------------------------------
// Catalog structural validation
// ---------------------------------------------------------------------------
std::vector<ValidationDiagnostic> validate_catalog(SchemaCatalog const& catalog)
{
    std::vector<ValidationDiagnostic> out;
    auto diag = [&](DiagnosticCode code, std::string const& msg) {
        out.push_back(ValidationDiagnostic{code, std::nullopt, std::nullopt,
                                           std::nullopt, msg});
    };

    // Duplicate (schemaId, schemaVersion) tuples / documentIds / paths.
    for (std::size_t i = 0; i < catalog.entries().size(); ++i) {
        for (std::size_t j = i + 1; j < catalog.entries().size(); ++j) {
            auto const& a = catalog.entries()[i];
            auto const& b = catalog.entries()[j];
            if (a.schema_id == b.schema_id && a.schema_version == b.schema_version) {
                diag(DiagnosticCode::DuplicateSchemaEntry,
                     "duplicate (schemaId, schemaVersion) tuple: " +
                         a.schema_id.to_string() + " " + a.schema_version.to_string());
            }
            if (a.document_id == b.document_id) {
                diag(DiagnosticCode::DuplicateDocumentId,
                     "duplicate documentId: " + a.document_id.to_string());
            }
            if (a.path == b.path) {
                diag(DiagnosticCode::DuplicateSchemaPath,
                     "duplicate path: " + a.path);
            }
        }
    }

    for (auto const& e : catalog.entries()) {
        if (e.document_id.schema_id() != e.schema_id) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::DocumentIdSchemaIdMismatch, e.schema_id, e.schema_version,
                e.path,
                "documentId schemaId '" + e.document_id.schema_id().to_string() +
                    "' does not match entry schemaId '" + e.schema_id.to_string() + "'"});
        }
        if (e.document_id.schema_version() != e.schema_version) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::DocumentIdVersionMismatch, e.schema_id, e.schema_version,
                e.path,
                "documentId version '" + e.document_id.schema_version().to_string() +
                    "' does not match entry schemaVersion '" +
                    e.schema_version.to_string() + "'"});
        }
        // Path policy: repo-relative, forward slashes, no traversal.
        if (is_absolute_path(e.path)) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::AbsoluteSchemaPath, e.schema_id, e.schema_version,
                e.path, "schema path must be repo-relative, not absolute"});
        }
        if (has_traversal(e.path)) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::PathTraversal, e.schema_id, e.schema_version,
                e.path, "schema path must not contain '..' segments"});
        }
        if (e.path.find('\\') != std::string::npos) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::PathTraversal, e.schema_id, e.schema_version,
                e.path, "schema path must use forward slashes"});
        }
    }

    // Rules: registered targets, no cross-SchemaId edges, no duplicate or
    // conflicting same-direction rules.
    std::vector<std::string> registered;
    registered.reserve(catalog.entries().size());
    for (auto const& e : catalog.entries()) {
        registered.push_back(e.document_id.to_string());
    }
    for (std::size_t i = 0; i < catalog.rules().size(); ++i) {
        auto const& a = catalog.rules()[i];
        if (a.from.schema_id() != a.to.schema_id()) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::CrossSchemaEdge, std::nullopt, std::nullopt,
                std::nullopt,
                "rule crosses SchemaIds: " + a.from.to_string() + " -> " + a.to.to_string()});
        }
        std::vector<std::string> targets = {a.from.to_string(), a.to.to_string()};
        for (auto const& t : targets) {
            if (std::find(registered.begin(), registered.end(), t) == registered.end()) {
                out.push_back(ValidationDiagnostic{
                    DiagnosticCode::RuleTargetUnknown, std::nullopt, std::nullopt,
                    std::nullopt, "rule references unregistered documentId '" + t + "'"});
            }
        }
        for (std::size_t j = i + 1; j < catalog.rules().size(); ++j) {
            auto const& b = catalog.rules()[j];
            if (a.from == b.from && a.to == b.to) {
                if (a.kind == b.kind) {
                    out.push_back(ValidationDiagnostic{
                        DiagnosticCode::DuplicateRule, std::nullopt, std::nullopt,
                        std::nullopt,
                        "duplicate rule " + a.from.to_string() + " -> " + a.to.to_string()});
                } else {
                    out.push_back(ValidationDiagnostic{
                        DiagnosticCode::ConflictingRule, std::nullopt, std::nullopt,
                        std::nullopt,
                        "conflicting rule kinds for " + a.from.to_string() + " -> " +
                            a.to.to_string()});
                }
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// File-backed validation
// ---------------------------------------------------------------------------
std::vector<ValidationDiagnostic> validate_catalog_files(SchemaCatalog const& catalog,
                                                         std::string_view repo_root)
{
    std::vector<ValidationDiagnostic> out;
    std::vector<std::string> registered;
    registered.reserve(catalog.entries().size());
    for (auto const& e : catalog.entries()) {
        registered.push_back(e.document_id.to_string());
    }

    for (auto const& e : catalog.entries()) {
        // Repo-root containment is enforced HERE, before any file I/O, so that
        // a caller invoking validate_catalog_files directly (without
        // validate_catalog) still gets the same path policy.
        if (is_absolute_path(e.path)) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::AbsoluteSchemaPath, e.schema_id, e.schema_version,
                e.path, "schema path must be repo-relative, not absolute"});
            continue;
        }
        if (e.path.find('\\') != std::string::npos) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::PathTraversal, e.schema_id, e.schema_version,
                e.path, "schema path must use forward slashes"});
            continue;
        }
        if (has_traversal(e.path)) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::PathTraversal, e.schema_id, e.schema_version,
                e.path, "schema path must not contain '..' segments"});
            continue;
        }
        // Canonical containment: resolve symlinks/junctions and normalize the
        // repo root and the target path BEFORE any file I/O, then require the
        // target to be inside the root by PATH COMPONENTS (not string prefix).
        std::error_code cec;
        const auto root_abs = std::filesystem::absolute(std::string(repo_root), cec);
        const auto root_canon = std::filesystem::weakly_canonical(root_abs, cec);
        const auto full_canon =
            std::filesystem::weakly_canonical(root_abs / std::filesystem::path(e.path), cec);
        if (cec) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::PathTraversal, e.schema_id, e.schema_version,
                e.path,
                "schema path could not be canonicalized (error: " + cec.message() + ")"});
            continue;
        }
        auto root_it = root_canon.begin();
        auto full_it = full_canon.begin();
        bool inside = true;
        for (; root_it != root_canon.end(); ++root_it, ++full_it) {
            if (full_it == full_canon.end() || *full_it != *root_it) {
                inside = false;
                break;
            }
        }
        if (!inside) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::PathTraversal, e.schema_id, e.schema_version,
                e.path, "canonical schema path resolves outside the repository root"});
            continue;
        }
        const std::filesystem::path& full = full_canon;

        if (!std::filesystem::exists(full)) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::MissingSchemaFile, e.schema_id, e.schema_version,
                e.path, "schema file does not exist: " + e.path});
            continue;
        }
        std::ifstream in(full, std::ios::binary);
        if (!in) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::MissingSchemaFile, e.schema_id, e.schema_version,
                e.path, "schema file could not be opened: " + e.path});
            continue;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        JsonNode node;
        try {
            node = json_parse(ss.str());
        } catch (JsonParseError const& ex) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::MalformedCatalogJson, e.schema_id, e.schema_version,
                e.path, "schema file is not valid JSON: " + std::string(ex.what())});
            continue;
        }
        if (const JsonNode* id = node.find("$id");
            id == nullptr || id->kind != JsonNode::Kind::String) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::OnDiskIdMismatch, e.schema_id, e.schema_version,
                e.path, "schema file is missing a string '$id'"});
        } else if (id->string != e.document_id.to_string()) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::OnDiskIdMismatch, e.schema_id, e.schema_version,
                e.path, "on-disk '$id' '" + id->string +
                            "' does not match catalog documentId '" +
                            e.document_id.to_string() + "'"});
        }
        if (const JsonNode* ver = node.find("version");
            ver == nullptr || ver->kind != JsonNode::Kind::String) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::OnDiskVersionMismatch, e.schema_id, e.schema_version,
                e.path, "schema file is missing a string 'version'"});
        } else if (ver->string != e.schema_version.to_string()) {
            out.push_back(ValidationDiagnostic{
                DiagnosticCode::OnDiskVersionMismatch, e.schema_id, e.schema_version,
                e.path, "on-disk 'version' '" + ver->string +
                            "' does not match catalog schemaVersion '" +
                            e.schema_version.to_string() + "'"});
        }
        // Local $ref resolution: registered entries or the dialect id only.
        std::vector<std::string> refs;
        collect_refs(node, refs);
        for (auto const& ref : refs) {
            if (ref == kJsonSchemaDialectId) {
                continue;
            }
            if (std::find(registered.begin(), registered.end(), ref) == registered.end()) {
                out.push_back(ValidationDiagnostic{
                    DiagnosticCode::UnregisteredRef, e.schema_id, e.schema_version,
                    e.path,
                    "unresolved local $ref '" + ref +
                        "' (not a registered documentId nor the JSON Schema dialect id)"});
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------
CatalogLookupResult lookup_schema(SchemaCatalog const& catalog, SchemaId const& id,
                                  SchemaVersion const& version)
{
    CatalogLookupResult result;
    bool id_known = false;
    SchemaVersion max_version = SchemaVersion::from_components(0, 0, 0);
    for (auto const& e : catalog.entries()) {
        if (e.schema_id == id) {
            id_known = true;
            if (e.schema_version > max_version) {
                max_version = e.schema_version;
            }
            if (e.schema_version == version) {
                result.entry = e;
                return result;
            }
        }
    }
    if (!id_known) {
        result.diagnostics.push_back(ValidationDiagnostic{
            DiagnosticCode::UnknownSchema, id, std::nullopt, std::nullopt,
            "unknown SchemaId '" + id.to_string() + "'"});
        return result;
    }
    if (version > max_version) {
        result.diagnostics.push_back(ValidationDiagnostic{
            DiagnosticCode::UnsupportedFutureVersion, id, version, std::nullopt,
            "version " + version.to_string() + " is newer than any registered version of '" +
                id.to_string() + "'"});
    } else {
        result.diagnostics.push_back(ValidationDiagnostic{
            DiagnosticCode::UnknownSchemaVersion, id, version, std::nullopt,
            "no registered version " + version.to_string() + " for SchemaId '" +
                id.to_string() + "'"});
    }
    return result;
}

// ---------------------------------------------------------------------------
// Raw-version inspection (structured codes; callers do not parse first)
// ---------------------------------------------------------------------------
VersionInspectionResult inspect_schema_version(SchemaCatalog const& catalog,
                                               SchemaId const& id,
                                               std::optional<std::string_view> raw_version)
{
    VersionInspectionResult result;
    if (!raw_version.has_value()) {
        result.diagnostics.push_back(ValidationDiagnostic{
            DiagnosticCode::MissingSchemaVersion, id, std::nullopt, std::nullopt,
            "no schema version provided for SchemaId '" + id.to_string() + "'"});
        return result;
    }
    SchemaVersion version = SchemaVersion::from_components(0, 0, 0);
    bool ok = true;
    try {
        version = SchemaVersion::from_string(*raw_version);
    } catch (std::invalid_argument const&) {
        ok = false;
        result.diagnostics.push_back(ValidationDiagnostic{
            DiagnosticCode::MalformedSchemaVersion, id, std::nullopt, std::nullopt,
            "malformed schema version '" + std::string(*raw_version) +
                "' for SchemaId '" + id.to_string() + "'"});
    } catch (std::out_of_range const&) {
        ok = false;
        result.diagnostics.push_back(ValidationDiagnostic{
            DiagnosticCode::SchemaVersionOutOfRange, id, std::nullopt, std::nullopt,
            "schema version '" + std::string(*raw_version) +
                "' exceeds the uint32 range for SchemaId '" + id.to_string() + "'"});
    }
    if (!ok) {
        return result;
    }
    const CatalogLookupResult lookup = lookup_schema(catalog, id, version);
    result.entry = lookup.entry;
    result.diagnostics = lookup.diagnostics;
    return result;
}

// ---------------------------------------------------------------------------
// Compatibility (explicit directional edges only; no semver inference)
// ---------------------------------------------------------------------------
CompatibilityResult schema_compatibility(SchemaCatalog const& catalog, SchemaId const& id,
                                         SchemaVersion const& from,
                                         SchemaVersion const& to)
{
    CompatibilityResult result;
    // Invalid/unvalidated rules must never produce success: gate on the
    // catalog's structural validation first.
    const std::vector<ValidationDiagnostic> structural = validate_catalog(catalog);
    if (!structural.empty()) {
        result.value = Compatibility::Unsupported;
        result.diagnostics = structural;
        return result;
    }
    const SchemaDocumentId from_doc = SchemaDocumentId::from_parts(id, from);
    const SchemaDocumentId to_doc = SchemaDocumentId::from_parts(id, to);

    const CatalogLookupResult from_lookup = lookup_schema(catalog, id, from);
    const CatalogLookupResult to_lookup = lookup_schema(catalog, id, to);
    if (!from_lookup.entry.has_value() || !to_lookup.entry.has_value()) {
        result.value = Compatibility::Unsupported;
        result.diagnostics = from_lookup.diagnostics;
        result.diagnostics.insert(result.diagnostics.end(),
                                  to_lookup.diagnostics.begin(),
                                  to_lookup.diagnostics.end());
        return result;
    }
    if (from == to) {
        result.value = Compatibility::Exact;
        return result;
    }
    for (auto const& r : catalog.rules()) {
        if (r.from == from_doc && r.to == to_doc) {
            result.value = (r.kind == SchemaRuleKind::Readable)
                               ? Compatibility::Readable
                               : Compatibility::RequiresMigration;
            return result;
        }
    }
    result.value = Compatibility::Unsupported;
    result.diagnostics.push_back(ValidationDiagnostic{
        DiagnosticCode::UnsupportedDirection, id, from,
        std::nullopt,
        "no declared compatibility edge from " + from_doc.to_string() + " to " +
            to_doc.to_string()});
    return result;
}

// ---------------------------------------------------------------------------
// Migration planning (contract only; no execution/mutation)
// ---------------------------------------------------------------------------
MigrationPlan plan_migration(SchemaCatalog const& catalog,
                             SchemaDocumentId const& source,
                             SchemaDocumentId const& target)
{
    MigrationPlan plan{source, target, {}, {}};
    // Invalid/unvalidated rules must never produce a successful plan: gate on
    // the catalog's structural validation first.
    const std::vector<ValidationDiagnostic> structural = validate_catalog(catalog);
    if (!structural.empty()) {
        plan.diagnostics = structural;
        return plan;
    }

    if (source == target) {
        return plan;  // nothing to migrate
    }

    const CatalogLookupResult src = lookup_schema(catalog, source.schema_id(),
                                                  source.schema_version());
    const CatalogLookupResult tgt = lookup_schema(catalog, target.schema_id(),
                                                  target.schema_version());
    if (!src.entry.has_value() || !tgt.entry.has_value()) {
        plan.diagnostics = src.diagnostics;
        plan.diagnostics.insert(plan.diagnostics.end(), tgt.diagnostics.begin(),
                                tgt.diagnostics.end());
        return plan;
    }

    // Directed path search over migration-kind edges in deterministic
    // (sorted) order. DFS from source; returns the first path found.
    struct Edge {
        SchemaDocumentId from;
        SchemaDocumentId to;
    };
    std::vector<Edge> edges;
    for (auto const& r : catalog.rules()) {
        if (r.kind == SchemaRuleKind::Migration) {
            edges.push_back(Edge{r.from, r.to});
        }
    }

    std::vector<SchemaDocumentId> path_nodes;
    std::vector<std::string> visited;
    auto contains = [&visited](std::string const& s) {
        return std::find(visited.begin(), visited.end(), s) != visited.end();
    };
    bool found = false;

    std::function<void(SchemaDocumentId)> dfs = [&](SchemaDocumentId node) {
        if (found) {
            return;
        }
        if (node == target) {
            found = true;
            return;
        }
        visited.push_back(node.to_string());
        for (auto const& e : edges) {
            if (e.from == node && !contains(e.to.to_string())) {
                path_nodes.push_back(e.to);
                dfs(e.to);
                if (found) {
                    return;
                }
                path_nodes.pop_back();
            }
        }
    };
    dfs(source);

    if (found) {
        SchemaDocumentId cur = source;
        for (auto const& n : path_nodes) {
            plan.steps.push_back(MigrationStep{cur, n, SchemaRuleKind::Migration});
            cur = n;
        }
        return plan;
    }
    plan.diagnostics.push_back(ValidationDiagnostic{
        DiagnosticCode::MissingMigrationEdge, std::nullopt, std::nullopt, std::nullopt,
        "no declared migration path from " + source.to_string() + " to " +
            target.to_string()});
    return plan;
}

}  // namespace choirloom::score
