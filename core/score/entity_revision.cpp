// ============================================================================
// core/score/entity_revision.cpp - out-of-line definitions for the M0-003
// EntityId / Revision primitives (metadata JSON wire + comparison helper).
// See entity_revision.h for the full identity contract.
// ============================================================================

#include "entity_revision.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace choirloom::score {
namespace {

bool is_ascii_ws(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Hex digit value, or -1 for non-hex characters.
int hex_val(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

// Encode a Unicode code point as UTF-8.
void append_utf8(std::string& out, std::uint32_t cp)
{
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
}

std::string json_escape(std::string const& s)
{
    std::string out;
    out.reserve(s.size() + 8);
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
    return out;
}

// Minimal scanner for exactly the canonical ProjectRevisionMetadata JSON
// object {"revisionId":..., "parentRevisionId":..., "origin":..., "summary":...}
// with JSON whitespace permitted between tokens.
struct MetadataJsonScanner {
    std::string_view src;
    std::size_t pos = 0;

    explicit MetadataJsonScanner(std::string_view s) : src(s) {}

    [[noreturn]] static void fail(std::string_view msg)
    {
        throw std::invalid_argument(
            std::string("ProjectRevisionMetadata::from_json: ") + std::string(msg));
    }

    void skip_ws() noexcept
    {
        while (pos < src.size() && is_ascii_ws(src[pos])) {
            ++pos;
        }
    }

    void expect(char c, std::string_view msg)
    {
        if (pos >= src.size() || src[pos] != c) {
            fail(msg);
        }
        ++pos;
    }

    std::string parse_string(std::string_view what)
    {
        expect('"', what);
        std::string out;
        while (true) {
            if (pos >= src.size()) {
                fail("unterminated string");
            }
            const char c = src[pos];
            if (c == '"') {
                ++pos;
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                fail("control character inside string");
            }
            if (c == '\\') {
                ++pos;
                if (pos >= src.size()) {
                    fail("unterminated string");
                }
                const char e = src[pos];
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
                        // \uXXXX (UTF-16 code unit; surrogate pairs combined).
                        // This decodes exactly the escapes to_json may produce
                        // (control bytes as \u00XX) so that
                        // from_json(to_json(metadata)) is an exact inverse.
                        if (pos + 4 >= src.size()) {
                            fail("unterminated \\u escape");
                        }
                        std::uint32_t cp = 0;
                        for (int k = 1; k <= 4; ++k) {
                            const int h = hex_val(src[pos + k]);
                            if (h < 0) {
                                fail("invalid \\u escape");
                            }
                            cp = (cp << 4) | static_cast<std::uint32_t>(h);
                        }
                        pos += 4;  // shared ++pos below advances past the hex
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (pos + 6 >= src.size() || src[pos + 1] != '\\' ||
                                src[pos + 2] != 'u') {
                                fail("unpaired high surrogate in \\u escape");
                            }
                            std::uint32_t lo = 0;
                            for (int k = 3; k <= 6; ++k) {
                                const int h = hex_val(src[pos + k]);
                                if (h < 0) {
                                    fail("invalid \\u escape");
                                }
                                lo = (lo << 4) | static_cast<std::uint32_t>(h);
                            }
                            if (lo < 0xDC00 || lo > 0xDFFF) {
                                fail("unpaired surrogate in \\u escape");
                            }
                            pos += 6;  // shared ++pos below advances past the hex
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                            fail("lone low surrogate in \\u escape");
                        }
                        append_utf8(out, cp);
                        break;
                    }
                    default:   fail("unsupported escape sequence inside string");
                }
                ++pos;
            } else {
                out.push_back(c);
                ++pos;
            }
        }
    }
};

// Defined byte order for deterministic metadata comparison (identity itself is
// non-ordering; this order exists only for deterministic tooling).
std::strong_ordering compare_byte_order(std::array<std::uint8_t, 16> const& x,
                                        std::array<std::uint8_t, 16> const& y)
{
    for (std::size_t i = 0; i < x.size(); ++i) {
        if (x[i] != y[i]) {
            return x[i] < y[i] ? std::strong_ordering::less
                               : std::strong_ordering::greater;
        }
    }
    return std::strong_ordering::equal;
}

}  // namespace

ProjectRevisionMetadata ProjectRevisionMetadata::from_values(
    RevisionId revision, std::optional<RevisionId> parent, RevisionOrigin origin,
    std::string summary)
{
    if (origin != RevisionOrigin::ManualEdit && origin != RevisionOrigin::Recognition &&
        origin != RevisionOrigin::Migration) {
        throw std::invalid_argument(
            "ProjectRevisionMetadata: unknown RevisionOrigin value");
    }
    if (parent.has_value() && *parent == revision) {
        throw std::invalid_argument(
            "ProjectRevisionMetadata: parentRevisionId must not equal revisionId (self-parent)");
    }
    std::size_t begin = 0;
    std::size_t end = summary.size();
    while (begin < end && is_ascii_ws(summary[begin])) {
        ++begin;
    }
    while (end > begin && is_ascii_ws(summary[end - 1])) {
        --end;
    }
    if (begin == end) {
        throw std::invalid_argument("ProjectRevisionMetadata: summary must be non-empty");
    }
    return ProjectRevisionMetadata(revision, parent, origin, std::move(summary));
}

std::string ProjectRevisionMetadata::to_json() const
{
    std::string out = "{\"revisionId\":\"" + revision_.to_string() + "\"";
    if (parent_.has_value()) {
        out += ",\"parentRevisionId\":\"" + parent_->to_string() + "\"";
    }
    out += ",\"origin\":\"";
    out += to_string(origin_);
    out += "\",\"summary\":\"";
    out += json_escape(summary_);
    out += "\"}";
    return out;
}

ProjectRevisionMetadata ProjectRevisionMetadata::from_json(std::string_view json)
{
    MetadataJsonScanner sc(json);
    sc.skip_ws();
    sc.expect('{', "expected '{' at start of object");

    bool has_revision = false;
    bool has_parent = false;
    bool has_origin = false;
    bool has_summary = false;
    std::string revision_str;
    std::string parent_str;
    std::string origin_str;
    std::string summary_str;

    while (true) {
        sc.skip_ws();
        const std::string key = sc.parse_string("expected field name");
        sc.skip_ws();
        sc.expect(':', "expected ':' after field name");
        sc.skip_ws();
        const std::string val = sc.parse_string("field value must be a JSON string");
        sc.skip_ws();

        if (key == "revisionId") {
            if (has_revision) {
                MetadataJsonScanner::fail("duplicate field \"revisionId\"");
            }
            revision_str = val;
            has_revision = true;
        } else if (key == "parentRevisionId") {
            if (has_parent) {
                MetadataJsonScanner::fail("duplicate field \"parentRevisionId\"");
            }
            parent_str = val;
            has_parent = true;
        } else if (key == "origin") {
            if (has_origin) {
                MetadataJsonScanner::fail("duplicate field \"origin\"");
            }
            origin_str = val;
            has_origin = true;
        } else if (key == "summary") {
            if (has_summary) {
                MetadataJsonScanner::fail("duplicate field \"summary\"");
            }
            summary_str = val;
            has_summary = true;
        } else {
            MetadataJsonScanner::fail("unknown field: \"" + key + "\"");
        }

        sc.skip_ws();
        if (sc.pos < sc.src.size() && sc.src[sc.pos] == ',') {
            ++sc.pos;
            continue;
        }
        if (sc.pos < sc.src.size() && sc.src[sc.pos] == '}') {
            ++sc.pos;
            break;
        }
        MetadataJsonScanner::fail("expected ',' or '}' between fields");
    }

    if (!has_revision) {
        MetadataJsonScanner::fail("missing field \"revisionId\"");
    }
    if (!has_origin) {
        MetadataJsonScanner::fail("missing field \"origin\"");
    }
    if (!has_summary) {
        MetadataJsonScanner::fail("missing field \"summary\"");
    }
    sc.skip_ws();
    if (sc.pos != sc.src.size()) {
        MetadataJsonScanner::fail("trailing content after JSON object");
    }

    const RevisionId revision = RevisionId::from_string(revision_str);
    const std::optional<RevisionId> parent =
        has_parent ? std::optional<RevisionId>(RevisionId::from_string(parent_str))
                   : std::nullopt;
    const RevisionOrigin origin = revision_origin_from_string(origin_str);  // unknown -> invalid_argument
    return from_values(revision, parent, origin, summary_str);  // revalidates self-parent / empty summary
}

std::strong_ordering compare_project_revisions(ProjectRevisionMetadata const& a,
                                               ProjectRevisionMetadata const& b)
{
    // 1) revisionId (defined byte order; purely for deterministic comparison).
    if (auto const c = compare_byte_order(a.revision_id().bytes(),
                                          b.revision_id().bytes());
        c != std::strong_ordering::equal) {
        return c;
    }
    // 2) parentRevisionId: absent < present; then defined byte order.
    auto const& pa = a.parent_revision_id();
    auto const& pb = b.parent_revision_id();
    if (!pa.has_value() && !pb.has_value()) {
        // equal so far
    } else if (!pa.has_value()) {
        return std::strong_ordering::less;
    } else if (!pb.has_value()) {
        return std::strong_ordering::greater;
    } else if (auto const c = compare_byte_order(pa->bytes(), pb->bytes());
               c != std::strong_ordering::equal) {
        return c;
    }
    // 3) origin: enum declaration order (ManualEdit < Recognition < Migration).
    if (a.origin() != b.origin()) {
        return a.origin() < b.origin() ? std::strong_ordering::less
                                       : std::strong_ordering::greater;
    }
    // 4) summary: lexicographic.
    return a.summary() <=> b.summary();
}

}  // namespace choirloom::score
