// ============================================================================
// tests/unit/rational_time_tests.cpp - unit tests for M0-001 RationalTime.
//
// Self-contained, dependency-free test runner (the M0 harness/CI does not
// exist yet). Compile with any C++20 compiler:
//   clang++ -std=c++20 -Wall -Wextra -pedantic rational_time_tests.cpp
//   cl  /std:c++20 /W4 /EHsc rational_time_tests.cpp
// or via the slice-local CMake/CTest setup (see root CMakeLists.txt). When
// built through CMake, CHOIRLOOM_TEST_SOURCE_DIR is an absolute path to the
// repository root so the golden fixture and schema files resolve from the
// build tree; when compiled directly, run from the repository root.
//
// Coverage: normalization, canonical zero, negatives, exact ordering,
// checked arithmetic, errors/overflow, MUSICAL TUPLET values, canonical JSON
// API, arithmetic regressions (final-representable overflow), and the golden
// fixture (parse, structural/semantic validation, exact JSON round-trip).
// ============================================================================

#include "rational_time.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef CHOIRLOOM_TEST_SOURCE_DIR
#define CHOIRLOOM_TEST_SOURCE_DIR "."
#endif

// ---------------------------------------------------------------------------
// Compile-time checks: exercise the constexpr paths of every operation.
// ---------------------------------------------------------------------------
static_assert(choirloom::score::RationalTime(2, 4) ==
              choirloom::score::RationalTime(1, 2));
static_assert(choirloom::score::RationalTime(0, 5).is_zero());
static_assert(choirloom::score::RationalTime(4, -6) ==
              choirloom::score::RationalTime(-2, 3));
static_assert(choirloom::score::RationalTime(-4, -6) ==
              choirloom::score::RationalTime(2, 3));
static_assert(choirloom::score::RationalTime(0) ==
              choirloom::score::RationalTime::zero());
static_assert(choirloom::score::RationalTime(1, 2) <
              choirloom::score::RationalTime(2, 3));
static_assert(choirloom::score::RationalTime(-1, 2) <
              choirloom::score::RationalTime(1, 3));
static_assert(choirloom::score::RationalTime(1, 2) + choirloom::score::RationalTime(1, 2) ==
              choirloom::score::RationalTime(1));
static_assert(choirloom::score::RationalTime(1, 3) + choirloom::score::RationalTime(1, 6) ==
              choirloom::score::RationalTime(1, 2));
static_assert(choirloom::score::RationalTime(1, 2) - choirloom::score::RationalTime(1, 3) ==
              choirloom::score::RationalTime(1, 6));
static_assert(choirloom::score::RationalTime(1, 2) / choirloom::score::RationalTime(1, 4) ==
              choirloom::score::RationalTime(2));
static_assert(choirloom::score::RationalTime(2, 3).reciprocal() ==
              choirloom::score::RationalTime(3, 2));
static_assert(choirloom::score::RationalTime(1, 8) * choirloom::score::RationalTime(2, 3) ==
              choirloom::score::RationalTime(1, 12));  // triplet eighth
static_assert(choirloom::score::RationalTime(1, 12) * choirloom::score::RationalTime(3) ==
              choirloom::score::RationalTime(1, 4));   // beat
static_assert(choirloom::score::RationalTime(1, 8) * choirloom::score::RationalTime(2, 3) *
                  choirloom::score::RationalTime(2, 3) ==
              choirloom::score::RationalTime(1, 18));  // nested tuplet

namespace {

using choirloom::score::RationalTime;
using Int = RationalTime::Int;

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

// ---------------------------------------------------------------------------
// Minimal JSON parser for the known fixture/schema shapes (test-only; no JSON
// library dependency). Parses any JSON document into a small node tree.
// ---------------------------------------------------------------------------
namespace mini_json {

struct Node {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Node> array;
    std::vector<std::pair<std::string, Node>> object;

    bool is_object() const { return kind == Kind::Object; }
    bool is_array() const { return kind == Kind::Array; }
    bool is_string() const { return kind == Kind::String; }

    const Node* find(std::string const& key) const
    {
        for (auto const& kv : object) {
            if (kv.first == key) {
                return &kv.second;
            }
        }
        return nullptr;
    }
};

class ParseError : public std::runtime_error {
public:
    explicit ParseError(std::string const& msg) : std::runtime_error(msg) {}
};

class Parser {
public:
    explicit Parser(std::string const& text) : text_(text) {}

    Node parse()
    {
        const Node n = parse_value();
        skip_ws();
        if (pos_ != text_.size()) {
            throw ParseError("trailing content after JSON value");
        }
        return n;
    }

private:
    std::string const& text_;
    std::size_t pos_ = 0;

    static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
    void skip_ws() { while (pos_ < text_.size() && is_ws(text_[pos_])) ++pos_; }

    char peek()
    {
        if (pos_ >= text_.size()) {
            throw ParseError("unexpected end of input");
        }
        return text_[pos_];
    }

    Node parse_value()
    {
        skip_ws();
        const char c = peek();
        switch (c) {
            case '{':
                return parse_object();
            case '[':
                return parse_array();
            case '"': {
                Node n;
                n.kind = Node::Kind::String;
                n.string = parse_string();
                return n;
            }
            case 't':
                expect_literal("true");
                {
                    Node n;
                    n.kind = Node::Kind::Bool;
                    n.boolean = true;
                    return n;
                }
            case 'f':
                expect_literal("false");
                {
                    Node n;
                    n.kind = Node::Kind::Bool;
                    n.boolean = false;
                    return n;
                }
            case 'n':
                expect_literal("null");
                return Node();
            default:
                if (c == '-' || (c >= '0' && c <= '9')) {
                    return parse_number();
                }
                throw ParseError("unexpected character in JSON");
        }
    }

    void expect_literal(char const* lit)
    {
        for (; *lit != '\0'; ++lit) {
            if (pos_ >= text_.size() || text_[pos_] != *lit) {
                throw ParseError("invalid literal");
            }
            ++pos_;
        }
    }

    Node parse_object()
    {
        ++pos_;  // consume '{'
        Node n;
        n.kind = Node::Kind::Object;
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return n;
        }
        while (true) {
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                throw ParseError("expected string key");
            }
            std::string key = parse_string();
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != ':') {
                throw ParseError("expected ':'");
            }
            ++pos_;
            n.object.emplace_back(std::move(key), parse_value());
            skip_ws();
            if (pos_ >= text_.size()) {
                throw ParseError("unterminated object");
            }
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}') {
                ++pos_;
                return n;
            }
            throw ParseError("expected ',' or '}'");
        }
    }

    Node parse_array()
    {
        ++pos_;  // consume '['
        Node n;
        n.kind = Node::Kind::Array;
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return n;
        }
        while (true) {
            n.array.push_back(parse_value());
            skip_ws();
            if (pos_ >= text_.size()) {
                throw ParseError("unterminated array");
            }
            if (text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']') {
                ++pos_;
                return n;
            }
            throw ParseError("expected ',' or ']'");
        }
    }

    std::string parse_string()
    {
        ++pos_;  // consume '"'
        std::string out;
        while (true) {
            if (pos_ >= text_.size()) {
                throw ParseError("unterminated string");
            }
            const char c = text_[pos_];
            if (c == '"') {
                ++pos_;
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                throw ParseError("control character in string");
            }
            if (c == '\\') {
                ++pos_;
                if (pos_ >= text_.size()) {
                    throw ParseError("unterminated escape");
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
                    default:   throw ParseError("unsupported escape");
                }
                ++pos_;
            } else {
                out.push_back(c);
                ++pos_;
            }
        }
    }

    Node parse_number()
    {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-') {
            ++pos_;
        }
        bool any = false;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            ++pos_;
            any = true;
        }
        if (!any) {
            throw ParseError("malformed number");
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            bool frac = false;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
                frac = true;
            }
            if (!frac) {
                throw ParseError("malformed number");
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
                throw ParseError("malformed number");
            }
        }
        Node n;
        n.kind = Node::Kind::Number;
        n.number = std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr);
        return n;
    }
};

inline Node parse(std::string const& text) { return Parser(text).parse(); }

}  // namespace mini_json

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------
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

// String value of node[key], or "" if absent / not a string.
std::string child_string(mini_json::Node const* n, std::string const& key)
{
    if (n == nullptr || !n->is_object()) {
        return std::string();
    }
    const mini_json::Node* v = n->find(key);
    return (v != nullptr && v->is_string()) ? v->string : std::string();
}

// ---------------------------------------------------------------------------
// Normalization
// ---------------------------------------------------------------------------
void test_normalization()
{
    const char* g = "normalize";

    {
        const RationalTime t(2, 4);
        CHECK(g, t.numerator() == 1 && t.denominator() == 2);
    }
    {
        const RationalTime t(0, 5);
        CHECK(g, t.numerator() == 0 && t.denominator() == 1);
        CHECK(g, t.is_zero());
    }
    CHECK(g, RationalTime(10, 15) == RationalTime(2, 3));
    CHECK(g, RationalTime(-4, 6) == RationalTime(-2, 3));
    CHECK(g, RationalTime(4, -6) == RationalTime(-2, 3));
    CHECK(g, RationalTime(-4, -6) == RationalTime(2, 3));
    CHECK(g, RationalTime(0, -7) == RationalTime(0));  // sign of zero normalized away
    CHECK(g, RationalTime(0, -7).denominator() == 1);
    CHECK(g, RationalTime(3) == RationalTime(3, 1));
    CHECK(g, RationalTime(3).is_integer());

    // Whole-value extremes normalize without loss.
    CHECK(g, RationalTime(RationalTime::kMax, RationalTime::kMax) == RationalTime(1));
    CHECK(g, RationalTime(RationalTime::kMin, RationalTime::kMin) == RationalTime(1));
    {
        const RationalTime t(RationalTime::kMin, 2);
        CHECK(g, t == RationalTime(-(Int{1} << 62), 1));
        CHECK(g, t.is_negative() && t.is_integer());
    }
    // 1/kMax is representable (denominator stays kMax).
    CHECK(g, RationalTime(1, RationalTime::kMax).denominator() == RationalTime::kMax);
    // kMin/1 keeps the exact minimum int64.
    CHECK(g, RationalTime(RationalTime::kMin).numerator() == RationalTime::kMin);

    CHECK_THROWS(g, RationalTime(1, 0), std::domain_error);
    CHECK_THROWS(g, RationalTime(0, 0), std::domain_error);
    CHECK_THROWS(g, RationalTime(RationalTime::kMin, -1), std::overflow_error);   // value 2^63
    CHECK_THROWS(g, RationalTime(1, RationalTime::kMin), std::overflow_error);    // den 2^63
    CHECK_THROWS(g, RationalTime(-1, RationalTime::kMin), std::overflow_error);   // den 2^63
}

// ---------------------------------------------------------------------------
// Zero
// ---------------------------------------------------------------------------
void test_zero()
{
    const char* g = "zero";

    CHECK(g, RationalTime().is_zero());
    CHECK(g, RationalTime(0) == RationalTime::zero());
    CHECK(g, RationalTime(0, 2) == RationalTime(0, 1));
    CHECK(g, RationalTime(3, 4) + RationalTime() == RationalTime(3, 4));
    CHECK(g, RationalTime(3, 4) - RationalTime() == RationalTime(3, 4));
    CHECK(g, RationalTime(3, 4) * RationalTime() == RationalTime(0));
    CHECK(g, RationalTime() * RationalTime(3, 4) == RationalTime(0));
    CHECK(g, RationalTime(3, 4) - RationalTime(3, 4) == RationalTime(0));
    CHECK(g, RationalTime() < RationalTime(1, 1000));
    CHECK(g, RationalTime(-1, 1000) < RationalTime());
    CHECK(g, RationalTime() < RationalTime(1));
}

// ---------------------------------------------------------------------------
// Negatives
// ---------------------------------------------------------------------------
void test_negative()
{
    const char* g = "negative";

    CHECK(g, -RationalTime(1, 4) == RationalTime(-1, 4));
    CHECK(g, -RationalTime(-2, 3) == RationalTime(2, 3));
    CHECK(g, RationalTime(-1, 4).is_negative());
    CHECK(g, !RationalTime(-1, 4).is_positive());
    CHECK(g, RationalTime(-1, 2) * RationalTime(-1, 2) == RationalTime(1, 4));
    CHECK(g, RationalTime(-1, 2) / RationalTime(-1, 4) == RationalTime(2));
    CHECK(g, RationalTime(-1, 2) + RationalTime(-1, 2) == RationalTime(-1));
    CHECK(g, RationalTime(-1, 4) < RationalTime() && RationalTime() < RationalTime(1, 4));
    CHECK(g, RationalTime(-1, 3) > RationalTime(-1, 2));
}

// ---------------------------------------------------------------------------
// Exact total ordering
// ---------------------------------------------------------------------------
void test_comparison()
{
    const char* g = "ordering";

    CHECK(g, RationalTime(1, 2) == RationalTime(2, 4));
    CHECK(g, RationalTime(1, 2) != RationalTime(1, 3));
    CHECK(g, RationalTime(1, 2) < RationalTime(2, 3));
    CHECK(g, RationalTime(2, 3) > RationalTime(1, 2));
    CHECK(g, RationalTime(1, 3) <= RationalTime(1, 2));
    CHECK(g, RationalTime(2, 3) >= RationalTime(2, 3));
    CHECK(g, RationalTime(2, 3) <= RationalTime(2, 3));
    CHECK(g, RationalTime(1, 4) < RationalTime(1, 3) && RationalTime(1, 3) < RationalTime(1, 2) &&
          RationalTime(1, 2) < RationalTime(2, 3) && RationalTime(2, 3) < RationalTime(3, 4));

    // Extreme magnitudes - comparison must never overflow.
    CHECK(g, RationalTime(RationalTime::kMax, 1) > RationalTime(RationalTime::kMax - 1, 1));
    CHECK(g, RationalTime(RationalTime::kMin, 1) < RationalTime(-1, 1));
    CHECK(g, RationalTime(RationalTime::kMin, 1) < RationalTime(RationalTime::kMin, 2));
    CHECK(g, RationalTime(RationalTime::kMin, 2) < RationalTime(0));
    CHECK(g, RationalTime(1, RationalTime::kMax) < RationalTime(2, RationalTime::kMax));
    CHECK(g, RationalTime(RationalTime::kMax - 1, RationalTime::kMax) < RationalTime(1));
    CHECK(g, RationalTime(RationalTime::kMax - 1, RationalTime::kMax) >
              RationalTime(RationalTime::kMax - 2, RationalTime::kMax));
    CHECK(g, RationalTime(1, RationalTime::kMax) < RationalTime(1, RationalTime::kMax - 1));
}

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------
void test_arithmetic()
{
    const char* g = "arith";

    CHECK(g, RationalTime(1, 4) + RationalTime(1, 4) == RationalTime(1, 2));
    CHECK(g, RationalTime(1, 3) + RationalTime(1, 6) == RationalTime(1, 2));
    CHECK(g, RationalTime(1, 2) + RationalTime(1, 2) == RationalTime(1));
    CHECK(g, RationalTime(1, 8) + RationalTime(1, 8) + RationalTime(1, 8) == RationalTime(3, 8));
    CHECK(g, RationalTime(1, 4) + RationalTime(1, 3) == RationalTime(7, 12));
    CHECK(g, RationalTime(1, 2) - RationalTime(1, 3) == RationalTime(1, 6));
    CHECK(g, RationalTime(1) - RationalTime(1, 4) == RationalTime(3, 4));
    CHECK(g, RationalTime(1, 4) - RationalTime(1, 2) == RationalTime(-1, 4));
    CHECK(g, RationalTime(1, 2) * RationalTime(1, 2) == RationalTime(1, 4));
    CHECK(g, RationalTime(2) * RationalTime(1, 4) == RationalTime(1, 2));
    CHECK(g, RationalTime(1, 4) * RationalTime(2) == RationalTime(1, 2));
    CHECK(g, RationalTime(1, 2) * RationalTime(1, 3) == RationalTime(1, 6));
    CHECK(g, RationalTime(1, 2) / RationalTime(1, 4) == RationalTime(2));
    CHECK(g, RationalTime(1, 2) / RationalTime(2, 3) == RationalTime(3, 4));
    CHECK(g, RationalTime(1) / RationalTime(2) == RationalTime(1, 2));

    // Compound assignment.
    {
        RationalTime t(1, 4);
        t += RationalTime(1, 4);
        CHECK(g, t == RationalTime(1, 2));
        t -= RationalTime(1, 8);
        CHECK(g, t == RationalTime(3, 8));
        t *= RationalTime(2);
        CHECK(g, t == RationalTime(3, 4));
        t /= RationalTime(3);
        CHECK(g, t == RationalTime(1, 4));
    }

    // Algebraic identities (spot checks).
    CHECK(g, RationalTime(1, 3) + RationalTime(1, 7) == RationalTime(1, 7) + RationalTime(1, 3));
    CHECK(g, (RationalTime(1, 3) + RationalTime(1, 5)) + RationalTime(1, 7) ==
              RationalTime(1, 3) + (RationalTime(1, 5) + RationalTime(1, 7)));
    CHECK(g, RationalTime(1, 3) * (RationalTime(1, 5) + RationalTime(1, 7)) ==
              RationalTime(1, 3) * RationalTime(1, 5) + RationalTime(1, 3) * RationalTime(1, 7));
}

// ---------------------------------------------------------------------------
// Errors / overflow - no wrap, no saturation, no float fallback
// ---------------------------------------------------------------------------
void test_overflow()
{
    const char* g = "overflow";

    // Add / subtract overflow.
    CHECK_THROWS(g, RationalTime(RationalTime::kMax, 1) + RationalTime(1, 1), std::overflow_error);
    CHECK_THROWS(g, RationalTime(RationalTime::kMin, 1) + RationalTime(-1, 1), std::overflow_error);
    CHECK_THROWS(g, RationalTime(RationalTime::kMax, 1) - RationalTime(-1, 1), std::overflow_error);
    // Multiply overflow.
    CHECK_THROWS(g, RationalTime(RationalTime::kMax, 1) * RationalTime(2, 1), std::overflow_error);
    CHECK_THROWS(g, RationalTime(RationalTime::kMin, 1) * RationalTime(2, 1), std::overflow_error);
    CHECK_THROWS(g, RationalTime(RationalTime::kMax, 1) / RationalTime(1, RationalTime::kMax), std::overflow_error);  // kMax / (1/kMax) == kMax^2
    // Unary minus / reciprocal overflow.
    CHECK_THROWS(g, -RationalTime(RationalTime::kMin), std::overflow_error);
    CHECK_THROWS(g, RationalTime(RationalTime::kMin).reciprocal(), std::overflow_error);
    // Division by zero / reciprocal of zero (also for non-canonical zero input).
    CHECK_THROWS(g, RationalTime(1, 2) / RationalTime(0), std::domain_error);
    CHECK_THROWS(g, RationalTime(1, 2) / RationalTime(0, 2), std::domain_error);
    CHECK_THROWS(g, RationalTime(0).reciprocal(), std::domain_error);

    // Boundary values that must NOT throw (cross-cancellation keeps int64).
    CHECK(g, RationalTime(RationalTime::kMax, 1) + RationalTime(0) == RationalTime(RationalTime::kMax, 1));
    CHECK(g, RationalTime(RationalTime::kMin, 1) * RationalTime(1) == RationalTime(RationalTime::kMin, 1));
    CHECK(g, RationalTime(RationalTime::kMax, 1) * RationalTime(RationalTime::kMax - 1, RationalTime::kMax) ==
              RationalTime(RationalTime::kMax - 1, 1));
    CHECK(g, RationalTime(1, RationalTime::kMax) * RationalTime(RationalTime::kMax, 1) == RationalTime(1));
    CHECK(g, RationalTime(1, RationalTime::kMax) + RationalTime(1, RationalTime::kMax) ==
              RationalTime(2, RationalTime::kMax));
    // Dividing by the reciprocal exactly recovers kMax (representable, no throw).
    CHECK(g, RationalTime(1) / RationalTime(1, RationalTime::kMax) ==
              RationalTime(RationalTime::kMax, 1));
}

// ---------------------------------------------------------------------------
// Musical tuplets (the focus of this slice)
// ---------------------------------------------------------------------------
void test_tuplets()
{
    const char* g = "tuplets";

    // Triplet ratio 3:2 = 2/3, duplet ratio 2:3 = 3/2, etc.
    const RationalTime triplet_ratio(2, 3);
    const RationalTime duplet_ratio(3, 2);
    const RationalTime quintuplet_ratio(4, 5);  // 5:4
    const RationalTime septuplet_ratio(4, 7);   // 7:4

    // Triplet eighth: 1/8 * 2/3 == 1/12.
    CHECK(g, RationalTime(1, 8) * triplet_ratio == RationalTime(1, 12));
    // Triplet quarter: 1/4 * 2/3 == 1/6.
    CHECK(g, RationalTime(1, 4) * triplet_ratio == RationalTime(1, 6));
    // Triplet sixteenth: 1/16 * 2/3 == 1/24.
    CHECK(g, RationalTime(1, 16) * triplet_ratio == RationalTime(1, 24));

    // Three triplet eighths sum to exactly one quarter (the beat).
    CHECK(g, RationalTime(1, 12) * RationalTime(3) == RationalTime(1, 4));
    CHECK(g, RationalTime(1, 12) + RationalTime(1, 12) + RationalTime(1, 12) == RationalTime(1, 4));

    // A beat divided by 3 is a triplet eighth (division form).
    CHECK(g, RationalTime(1, 4) / RationalTime(3) == RationalTime(1, 12));

    // Two triplet quarters equal one third: 1/6 + 1/6 == 1/3.
    CHECK(g, RationalTime(1, 6) * RationalTime(2) == RationalTime(1, 3));

    // Quintuplet (5:4) sixteenth: 1/16 * 4/5 == 1/20; five fill the beat.
    CHECK(g, RationalTime(1, 16) * quintuplet_ratio == RationalTime(1, 20));
    CHECK(g, RationalTime(1, 20) * RationalTime(5) == RationalTime(1, 4));

    // Septuplet (7:4) eighth: 1/8 * 4/7 == 1/14; seven fill a half note.
    CHECK(g, RationalTime(1, 8) * septuplet_ratio == RationalTime(1, 14));
    CHECK(g, RationalTime(1, 14) * RationalTime(7) == RationalTime(1, 2));

    // Duplet in 6/8 (2:3): 1/8 * 3/2 == 3/16; two equal 3/8 (a dotted quarter).
    CHECK(g, RationalTime(1, 8) * duplet_ratio == RationalTime(3, 16));
    CHECK(g, RationalTime(3, 16) * RationalTime(2) == RationalTime(3, 8));

    // Nested exact tuplet arithmetic: triplet within a triplet of an eighth.
    CHECK(g, RationalTime(1, 8) * triplet_ratio * triplet_ratio == RationalTime(1, 18));

    // Consistency across different construction paths.
    CHECK(g, RationalTime(1, 12) == RationalTime(1, 6) * RationalTime(1, 2));
    CHECK(g, RationalTime(1, 8) * triplet_ratio == RationalTime(1, 4) / RationalTime(3));

    // Undoing a tuplet ratio restores the base duration.
    CHECK(g, RationalTime(1, 12) / triplet_ratio == RationalTime(1, 8));

    // Tuplet durations compare exactly with ordinary notes.
    CHECK(g, RationalTime(1, 12) < RationalTime(1, 8));
    CHECK(g, RationalTime(1, 20) < RationalTime(1, 16));
    CHECK(g, RationalTime(3, 16) > RationalTime(1, 8));

    // Mixed tuplet + ordinary note sum.
    CHECK(g, RationalTime(1, 12) + RationalTime(1, 8) == RationalTime(5, 24));

    // Ratio identity: multiplying by the inverse ratio is the identity.
    CHECK(g, triplet_ratio * RationalTime(3, 2) == RationalTime(1));
}

// ---------------------------------------------------------------------------
// Display / conversion helpers
// ---------------------------------------------------------------------------
void test_display()
{
    const char* g = "display";

    CHECK(g, RationalTime(1, 4).to_string() == "1/4");
    CHECK(g, RationalTime().to_string() == "0/1");
    CHECK(g, RationalTime(-2, 3).to_string() == "-2/3");
    CHECK(g, RationalTime(3).to_string() == "3/1");

    CHECK(g, RationalTime(1, 4).to_double() == 0.25);
    CHECK(g, RationalTime(1, 1).to_double() == 1.0);
    {
        const double d = RationalTime(1, 12).to_double();
        CHECK(g, d > 0.08333 && d < 0.08334);
    }

    {
        std::ostringstream os;
        os << RationalTime(1, 4);
        CHECK(g, os.str() == "1/4");
    }
}

// ---------------------------------------------------------------------------
// Canonical JSON API
// ---------------------------------------------------------------------------
void test_json_api()
{
    const char* g = "json";

    // Exact canonical wire format, including int64 extremes.
    CHECK(g, RationalTime(1, 4).to_canonical_json() == "{\"numerator\":\"1\",\"denominator\":\"4\"}");
    CHECK(g, RationalTime(0).to_canonical_json() == "{\"numerator\":\"0\",\"denominator\":\"1\"}");
    CHECK(g, RationalTime(-2, 3).to_canonical_json() == "{\"numerator\":\"-2\",\"denominator\":\"3\"}");
    CHECK(g, RationalTime(RationalTime::kMin).to_canonical_json() ==
              "{\"numerator\":\"-9223372036854775808\",\"denominator\":\"1\"}");
    CHECK(g, RationalTime(RationalTime::kMax).to_canonical_json() ==
              "{\"numerator\":\"9223372036854775807\",\"denominator\":\"1\"}");
    CHECK(g, RationalTime(1, RationalTime::kMax).to_canonical_json() ==
              "{\"numerator\":\"1\",\"denominator\":\"9223372036854775807\"}");

    // Round-trip at the boundaries.
    CHECK(g, RationalTime::from_canonical_json(RationalTime(RationalTime::kMin).to_canonical_json()) ==
              RationalTime(RationalTime::kMin));
    CHECK(g, RationalTime::from_canonical_json(RationalTime(1, RationalTime::kMax).to_canonical_json()) ==
              RationalTime(1, RationalTime::kMax));
    CHECK(g, RationalTime::from_canonical_json("{\"numerator\":\"9223372036854775807\",\"denominator\":\"1\"}") ==
              RationalTime(RationalTime::kMax));

    // Whitespace around tokens and field order are permitted.
    CHECK(g, RationalTime::from_canonical_json(" \t\r\n{ \"numerator\" : \"1\" , \"denominator\" : \"4\" } ") ==
              RationalTime(1, 4));
    CHECK(g, RationalTime::from_canonical_json("{\"denominator\":\"4\",\"numerator\":\"1\"}") ==
              RationalTime(1, 4));

    // Malformed / structural failures -> std::invalid_argument.
    CHECK_THROWS(g, RationalTime::from_canonical_json(""), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{}"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"1\"}"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"denominator\":\"4\"}"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"1\",\"denominator\":\"4\",\"x\":\"1\"}"),
                 std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"1\",\"denominator\":\"4\",\"numerator\":\"2\"}"),
                 std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"foo\":\"1\",\"denominator\":\"4\"}"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":1,\"denominator\":\"4\"}"), std::invalid_argument);   // non-string
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":[1],\"denominator\":\"4\"}"), std::invalid_argument); // non-string
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"1\",\"denominator\":\"4\"}junk"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"1\",\"denominator\":\"4\""), std::invalid_argument); // unclosed

    // Invalid decimal grammar -> std::invalid_argument.
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"\",\"denominator\":\"4\"}"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"+1\",\"denominator\":\"4\"}"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"01\",\"denominator\":\"4\"}"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"-01\",\"denominator\":\"4\"}"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"-0\",\"denominator\":\"1\"}"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"1e3\",\"denominator\":\"4\"}"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"1\",\"denominator\":\"04\"}"), std::invalid_argument);

    // Denominator zero / negative -> std::invalid_argument.
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"1\",\"denominator\":\"0\"}"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"1\",\"denominator\":\"-3\"}"), std::invalid_argument);

    // Non-reduced input is rejected (never silently normalized).
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"2\",\"denominator\":\"4\"}"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"3\",\"denominator\":\"6\"}"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"0\",\"denominator\":\"4\"}"), std::invalid_argument);
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"2\",\"denominator\":\"2\"}"), std::invalid_argument);

    // Out-of-int64 wire values -> std::out_of_range.
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"9223372036854775808\",\"denominator\":\"1\"}"),
                 std::out_of_range);  // 2^63 > kMax
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"-9223372036854775809\",\"denominator\":\"1\"}"),
                 std::out_of_range);  // below kMin
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"1\",\"denominator\":\"9223372036854775808\"}"),
                 std::out_of_range);  // denominator 2^63
    CHECK_THROWS(g, RationalTime::from_canonical_json("{\"numerator\":\"99999999999999999999\",\"denominator\":\"1\"}"),
                 std::out_of_range);
}

// ---------------------------------------------------------------------------
// Arithmetic regressions: final result representable while intermediates
// (unreduced numerator sum or lcm) exceed int64 - must NOT throw.
// ---------------------------------------------------------------------------
void test_regressions()
{
    const char* g = "regress";

    // lcm of the denominators exceeds int64, but the reduced sum fits.
    CHECK(g, RationalTime(1, 6000000006) + RationalTime(1, 6000000009) ==
              RationalTime(1333333335, 4000000010000000006LL));
    // Inverse: subtracting one addend recovers the other.
    CHECK(g, RationalTime(1333333335, 4000000010000000006LL) - RationalTime(1, 6000000009) ==
              RationalTime(1, 6000000006));

    // Unreduced numerator sum exceeds int64; reduced result fits:
    // kMax/3 + (kMax-2)/3 == (2*kMax - 2)/3 == (2^64 - 4)/3 == 6148914691236517204.
    CHECK(g, (RationalTime(RationalTime::kMax, 3) + RationalTime(RationalTime::kMax - 2, 3)) ==
              RationalTime(6148914691236517204));
    // Constructor normalization reduces the operands before addition; the exact
    // result remains representable.
    CHECK(g, RationalTime(Int{1} << 62, 4) + RationalTime(Int{1} << 62, 2) ==
              RationalTime(3 * (Int{1} << 60)));

    // int64_min / int64_min division and cancellation (gcd == 2^63) boundary.
    CHECK(g, RationalTime(RationalTime::kMin) / RationalTime(RationalTime::kMin) == RationalTime(1));
    CHECK(g, RationalTime(RationalTime::kMin) / RationalTime(2) == RationalTime(-(Int{1} << 62)));
    CHECK(g, RationalTime(RationalTime::kMin, RationalTime::kMin) == RationalTime(1));

    // Sound overflow is still reported (no false negatives either).
    CHECK_THROWS(g, (RationalTime(1, 6000000006) + RationalTime(1, 6000000009)) *
                        RationalTime(RationalTime::kMax),
                 std::overflow_error);
}

// ---------------------------------------------------------------------------
// Golden fixture: parse, structurally validate against the collection/value
// schema contract, deserialize each value, reserialize, compare exact JSON.
// ---------------------------------------------------------------------------
void test_golden_fixture()
{
    const char* g = "golden";
    const std::string root = test_root();
    const std::string fixture_path = root + "/tests/golden/rational_time.samples.json";
    const std::string value_schema_path = root + "/schemas/score/rational-time.schema.json";
    const std::string coll_schema_path = root + "/schemas/score/rational-time-collection.schema.json";

    // Load the fixture file itself (proves the fixture is read from disk).
    bool ok = false;
    const std::string fixture_text = read_text_file(fixture_path, ok);
    CHECK(g, ok && !fixture_text.empty());

    mini_json::Node fixture;
    bool parsed = true;
    try {
        fixture = mini_json::parse(fixture_text);
    } catch (mini_json::ParseError const&) {
        parsed = false;
    }
    CHECK(g, parsed && fixture.is_object());
    if (!parsed || !fixture.is_object()) {
        return;
    }

    // Envelope: $schema must point at the versioned collection schema.
    const mini_json::Node* schema_ref = fixture.find("$schema");
    CHECK(g, schema_ref != nullptr && schema_ref->is_string());
    CHECK(g, schema_ref != nullptr && schema_ref->is_string() &&
              schema_ref->string.find("rational-time-collection.schema.json") != std::string::npos);
    // Top-level keys are restricted to the envelope ($schema, description, cases).
    CHECK(g, fixture.object.size() <= 3);

    const mini_json::Node* cases = fixture.find("cases");
    CHECK(g, cases != nullptr && cases->is_array() && !cases->array.empty());

    // Load the schema files (contract source).
    const std::string value_schema_text = read_text_file(value_schema_path, ok);
    CHECK(g, ok && !value_schema_text.empty());
    const std::string coll_schema_text = read_text_file(coll_schema_path, ok);
    CHECK(g, ok && !coll_schema_text.empty());

    mini_json::Node value_schema, coll_schema;
    try {
        value_schema = mini_json::parse(value_schema_text);
        coll_schema = mini_json::parse(coll_schema_text);
    } catch (mini_json::ParseError const&) {
        CHECK(g, false);
        return;
    }
    CHECK(g, value_schema.is_object() && coll_schema.is_object());

    // Value schema contract: strict object with required numerator/denominator
    // string patterns.
    const mini_json::Node* vprops = value_schema.find("properties");
    const mini_json::Node* vreq = value_schema.find("required");
    CHECK(g, vprops != nullptr && vprops->is_object());
    CHECK(g, vreq != nullptr && vreq->is_array() && vreq->array.size() == 2);
    CHECK(g, child_string(&value_schema, "type") == "object");
    CHECK(g, value_schema.find("additionalProperties") != nullptr);
    const mini_json::Node* num_p = (vprops != nullptr && vprops->is_object()) ? vprops->find("numerator") : nullptr;
    const mini_json::Node* den_p = (vprops != nullptr && vprops->is_object()) ? vprops->find("denominator") : nullptr;
    const std::string num_pattern = child_string(num_p, "pattern");
    const std::string den_pattern = child_string(den_p, "pattern");
    CHECK(g, !num_pattern.empty() && !den_pattern.empty());

    // Collection schema contract: object requiring "cases"; references the
    // value schema by its $id.
    CHECK(g, child_string(&coll_schema, "type") == "object");
    const mini_json::Node* cprops = coll_schema.find("properties");
    const mini_json::Node* ccases = (cprops != nullptr && cprops->is_object()) ? cprops->find("cases") : nullptr;
    CHECK(g, ccases != nullptr && ccases->is_object());
    CHECK(g, coll_schema_text.find("choirloom:score/rational-time/0.1.0") != std::string::npos);

    // Walk every case: structure, schema pattern, semantic decode, and exact
    // canonical JSON round-trip.
    const std::regex num_re(num_pattern), den_re(den_pattern);
    std::size_t count = 0;
    for (auto const& c : cases->array) {
        ++count;
        CHECK(g, c.is_object());
        const mini_json::Node* name = c.find("name");
        const mini_json::Node* note = c.find("note");
        const mini_json::Node* value = c.find("value");
        CHECK(g, name != nullptr && name->is_string() && !name->string.empty());
        CHECK(g, note == nullptr || note->is_string());
        CHECK(g, value != nullptr && value->is_object());
        CHECK(g, c.object.size() <= 3);  // name, note, value only
        if (value == nullptr || !value->is_object()) {
            continue;
        }
        const mini_json::Node* num = value->find("numerator");
        const mini_json::Node* den = value->find("denominator");
        CHECK(g, num != nullptr && num->is_string());
        CHECK(g, den != nullptr && den->is_string());
        CHECK(g, value->object.size() == 2);  // strict RationalTime value
        if (num == nullptr || den == nullptr) {
            continue;
        }

        // Syntactic level: the value strings must satisfy the schema patterns.
        CHECK(g, std::regex_match(num->string, num_re));
        CHECK(g, std::regex_match(den->string, den_re));

        // Semantic level: from_canonical_json must accept (enforces int64
        // range, positive denominator, canonical reduced form).
        const std::string wire = "{\"numerator\":\"" + num->string +
                                 "\",\"denominator\":\"" + den->string + "\"}";
        RationalTime t = RationalTime::zero();
        bool decoded = true;
        try {
            t = RationalTime::from_canonical_json(wire);
        } catch (...) {
            decoded = false;
        }
        CHECK(g, decoded);
        if (!decoded) {
            continue;
        }

        // Reserialize and compare the exact canonical JSON.
        CHECK(g, t.to_canonical_json() == wire);

        // Prove the fixture values are actually consumed (not hard-coded).
        const std::string case_name = (name != nullptr && name->is_string()) ? name->string : std::string();
        if (case_name == "zero") {
            CHECK(g, t == RationalTime());
        } else if (case_name == "whole-note") {
            CHECK(g, t == RationalTime(1));
        } else if (case_name == "quarter") {
            CHECK(g, t == RationalTime(1, 4));
        } else if (case_name == "eighth") {
            CHECK(g, t == RationalTime(1, 8));
        } else if (case_name == "triplet-eighth") {
            CHECK(g, t == RationalTime(1, 12));
        } else if (case_name == "triplet-quarter") {
            CHECK(g, t == RationalTime(1, 6));
        } else if (case_name == "nested-triplet") {
            CHECK(g, t == RationalTime(1, 18));
        } else if (case_name == "compound") {
            CHECK(g, t == RationalTime(5, 24));
        } else if (case_name == "negative") {
            CHECK(g, t == RationalTime(-2, 3));
        }
    }
    CHECK(g, count == cases->array.size());
    CHECK(g, count >= 8);  // a real golden set was exercised, not an empty one
}

}  // namespace

int main()
{
    test_normalization();
    test_zero();
    test_negative();
    test_comparison();
    test_arithmetic();
    test_overflow();
    test_tuplets();
    test_display();
    test_json_api();
    test_regressions();
    test_golden_fixture();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
