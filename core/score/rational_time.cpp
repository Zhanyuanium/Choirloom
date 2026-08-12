// ============================================================================
// core/score/rational_time.cpp - out-of-line definitions for RationalTime.
// Includes the dependency-free canonical JSON encode/decode.
// See rational_time.h for the full design and policy documentation.
// ============================================================================

#include "rational_time.h"

#include <ostream>
#include <string>
#include <string_view>

namespace choirloom::score {
namespace {

bool is_json_whitespace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Minimal scanner for exactly the object shape
//   {"numerator":"...","denominator":"..."}
// with JSON whitespace permitted between tokens. No other structure is
// accepted (no arrays, numbers, nested objects, or extra fields).
struct CanonicalJsonScanner {
    std::string_view src;
    std::size_t pos = 0;

    explicit CanonicalJsonScanner(std::string_view s) : src(s) {}

    [[noreturn]] static void fail(std::string_view msg)
    {
        throw std::invalid_argument(
            std::string("RationalTime::from_canonical_json: ") + std::string(msg));
    }

    void skip_ws() noexcept
    {
        while (pos < src.size() && is_json_whitespace(src[pos])) {
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

    // Parse a JSON string literal (handles the standard simple escapes,
    // rejects control characters and unsupported escapes).
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

// Parse a canonical decimal integer magnitude. Grammar:
//   numerator:    0 | -?[1-9][0-9]*
//   denominator:  [1-9][0-9]*
// Rejects '+', leading zeros and "-0". Values outside the int64 wire range
// (2^63 magnitude for the numerator, int64 max for the denominator) throw
// std::out_of_range. Grammar failures throw std::invalid_argument.
std::uint64_t parse_wire_magnitude(std::string_view sv, bool is_denominator)
{
    const bool negative = !sv.empty() && sv[0] == '-';
    std::size_t i = negative ? 1 : 0;
    if (i >= sv.size()) {
        CanonicalJsonScanner::fail(is_denominator ? "empty denominator"
                                                  : "empty numerator");
    }
    if (is_denominator && negative) {
        CanonicalJsonScanner::fail("denominator must be positive");
    }
    if (sv[i] == '0') {
        if (sv.size() != i + 1) {
            CanonicalJsonScanner::fail("leading zeros are not canonical");
        }
        if (negative) {
            CanonicalJsonScanner::fail("negative zero is not canonical");
        }
        return 0;
    }
    if (sv[i] < '1' || sv[i] > '9') {
        CanonicalJsonScanner::fail("not a decimal integer");
    }
    const std::uint64_t limit =
        (negative && !is_denominator)
            ? (std::uint64_t(1) << 63)                       // |int64 min|
            : static_cast<std::uint64_t>(RationalTime::kMax);
    std::uint64_t mag = 0;
    for (; i < sv.size(); ++i) {
        if (sv[i] < '0' || sv[i] > '9') {
            CanonicalJsonScanner::fail("not a decimal integer");
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(sv[i] - '0');
        if (mag > (limit - digit) / 10) {
            throw std::out_of_range(
                "RationalTime::from_canonical_json: value exceeds int64 range");
        }
        mag = mag * 10 + digit;
    }
    return mag;
}

}  // namespace

std::string RationalTime::to_string() const
{
    return std::to_string(num_) + "/" + std::to_string(den_);
}

std::ostream& operator<<(std::ostream& os, RationalTime t)
{
    return os << t.numerator() << '/' << t.denominator();
}

std::string RationalTime::to_canonical_json() const
{
    return std::string("{\"numerator\":\"") + std::to_string(num_) +
           "\",\"denominator\":\"" + std::to_string(den_) + "\"}";
}

RationalTime RationalTime::from_canonical_json(std::string_view json)
{
    CanonicalJsonScanner sc(json);
    sc.skip_ws();
    sc.expect('{', "expected '{' at start of object");
    sc.skip_ws();

    const std::string key1 = sc.parse_string("expected field name");
    sc.skip_ws();
    sc.expect(':', "expected ':' after field name");
    sc.skip_ws();
    const std::string val1 = sc.parse_string("field value must be a JSON string");
    sc.skip_ws();

    const bool saw_num = (key1 == "numerator");
    const bool saw_den = (key1 == "denominator");
    if (!saw_num && !saw_den) {
        CanonicalJsonScanner::fail("unknown field: \"" + key1 + "\"");
    }
    std::string num_str = saw_num ? val1 : std::string();
    std::string den_str = saw_den ? val1 : std::string();
    bool has_num = saw_num;
    bool has_den = saw_den;

    sc.expect(',', "expected ',' between fields");
    sc.skip_ws();

    const std::string key2 = sc.parse_string("expected field name");
    sc.skip_ws();
    sc.expect(':', "expected ':' after field name");
    sc.skip_ws();
    const std::string val2 = sc.parse_string("field value must be a JSON string");
    sc.skip_ws();

    if (key2 == "numerator") {
        if (has_num) {
            CanonicalJsonScanner::fail("duplicate field \"numerator\"");
        }
        num_str = val2;
        has_num = true;
    } else if (key2 == "denominator") {
        if (has_den) {
            CanonicalJsonScanner::fail("duplicate field \"denominator\"");
        }
        den_str = val2;
        has_den = true;
    } else {
        CanonicalJsonScanner::fail("unknown field: \"" + key2 + "\"");
    }

    if (!has_num) {
        CanonicalJsonScanner::fail("missing field \"numerator\"");
    }
    if (!has_den) {
        CanonicalJsonScanner::fail("missing field \"denominator\"");
    }

    sc.expect('}', "expected '}' at end of object");
    sc.skip_ws();
    if (sc.pos != sc.src.size()) {
        CanonicalJsonScanner::fail("trailing content after JSON object");
    }

    const std::uint64_t num_mag = parse_wire_magnitude(num_str, false);
    const std::uint64_t den_mag = parse_wire_magnitude(den_str, true);

    const bool num_neg = !num_str.empty() && num_str[0] == '-';
    const Int num = num_neg
                        ? ((num_mag == (std::uint64_t(1) << 63))
                               ? kMin
                               : -static_cast<Int>(num_mag))
                        : static_cast<Int>(num_mag);
    const Int den = static_cast<Int>(den_mag);

    if (!is_canonical_form(num, den)) {
        CanonicalJsonScanner::fail("fraction is not in canonical reduced form");
    }
    return RationalTime(num, den);
}

}  // namespace choirloom::score
