// ============================================================================
// tests/unit/test_json.h - minimal dependency-free JSON parser (TEST-ONLY).
//
// Parses any JSON document into a small node tree. Used by the M0-003
// entity_revision tests to validate golden fixtures and schemas. This is NOT
// a JSON Schema library and is not part of the product core.
// ============================================================================

#pragma once

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace test_json {

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

// String value of node[key], or "" if absent / not a string.
inline std::string child_string(Node const* n, std::string const& key)
{
    if (n == nullptr || !n->is_object()) {
        return std::string();
    }
    const Node* v = n->find(key);
    return (v != nullptr && v->is_string()) ? v->string : std::string();
}

}  // namespace test_json
