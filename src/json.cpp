// SPDX-License-Identifier: MIT
// Shadow SE - minimal JSON value type with parser and serializer.
#include "shadowse/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>

namespace shadowse::json {

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

Value::Value() : type_(Type::Null) {}
Value::Value(bool b) : type_(Type::Bool), bool_(b) {}
Value::Value(int n) : type_(Type::Number), number_(static_cast<double>(n)) {}
Value::Value(double n) : type_(Type::Number), number_(n) {}
Value::Value(const char* s) : type_(Type::String), string_(s) {}
Value::Value(std::string s) : type_(Type::String), string_(std::move(s)) {}
Value::Value(std::vector<Value> a) : type_(Type::Array), array_(std::move(a)) {}
Value::Value(std::vector<std::pair<std::string, Value>> o)
    : type_(Type::Object), object_(std::move(o)) {}

bool Value::asBool(bool def) const {
    return type_ == Type::Bool ? bool_ : def;
}
double Value::asNumber(double def) const {
    return type_ == Type::Number ? number_ : def;
}
const std::string& Value::asString(const std::string& def) const {
    return type_ == Type::String ? string_ : def;
}
const std::vector<Value>& Value::array() const {
    static const std::vector<Value> kEmpty;
    return type_ == Type::Array ? array_ : kEmpty;
}
const std::vector<std::pair<std::string, Value>>& Value::object() const {
    static const std::vector<std::pair<std::string, Value>> kEmpty;
    return type_ == Type::Object ? object_ : kEmpty;
}

void Value::push(Value v) {
    if (type_ != Type::Array) {
        array_.clear();
        type_ = Type::Array;
    }
    array_.push_back(std::move(v));
}

Value& Value::operator[](const std::string& key) {
    if (type_ != Type::Object) {
        object_.clear();
        type_ = Type::Object;
    }
    for (auto& [k, v] : object_) {
        if (k == key) {
            return v;
        }
    }
    object_.emplace_back(key, Value{});
    return object_.back().second;
}

const Value* Value::find(const std::string& key) const {
    if (type_ != Type::Object) {
        return nullptr;
    }
    for (const auto& [k, v] : object_) {
        if (k == key) {
            return &v;
        }
    }
    return nullptr;
}

std::string escapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

void dumpValue(const Value& v, std::string& out, bool pretty, int indent) {
    auto newlineIndent = [&](int level) {
        out.push_back('\n');
        out.append(static_cast<std::size_t>(level) * 2, ' ');
    };
    switch (v.type()) {
        case Type::Null:
            out += "null";
            break;
        case Type::Bool:
            out += v.asBool() ? "true" : "false";
            break;
        case Type::Number: {
            const double n = v.asNumber();
            if (n == static_cast<double>(static_cast<std::int64_t>(n)) &&
                std::isfinite(n) && std::fabs(n) < 9.2e18) {
                out += std::to_string(static_cast<std::int64_t>(n));
            } else {
                std::ostringstream os;
                os << n;
                out += os.str();
            }
            break;
        }
        case Type::String:
            out.push_back('"');
            out += escapeString(v.asString());
            out.push_back('"');
            break;
        case Type::Array: {
            const auto& a = v.array();
            if (a.empty()) {
                out += "[]";
                break;
            }
            out.push_back('[');
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (i) out.push_back(',');
                if (pretty) newlineIndent(indent + 1);
                dumpValue(a[i], out, pretty, indent + 1);
            }
            if (pretty) newlineIndent(indent);
            out.push_back(']');
            break;
        }
        case Type::Object: {
            const auto& o = v.object();
            if (o.empty()) {
                out += "{}";
                break;
            }
            out.push_back('{');
            for (std::size_t i = 0; i < o.size(); ++i) {
                if (i) out.push_back(',');
                if (pretty) newlineIndent(indent + 1);
                out.push_back('"');
                out += escapeString(o[i].first);
                out += "\":";
                if (pretty) out.push_back(' ');
                dumpValue(o[i].second, out, pretty, indent + 1);
            }
            if (pretty) newlineIndent(indent);
            out.push_back('}');
            break;
        }
    }
}

std::string Value::dump(bool pretty) const {
    std::string out;
    dumpValue(*this, out, pretty, 0);
    return out;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text), pos_(0) {}

    bool parseValue(Value* out, std::string* err) {
        skipWs();
        if (pos_ >= s_.size()) {
            return fail("unexpected end of input", err);
        }
        const char c = s_[pos_];
        if (c == '{') return parseObject(out, err);
        if (c == '[') return parseArray(out, err);
        if (c == '"') return parseString(out, err);
        if (c == 't') return parseLiteral("true", true, out, err);
        if (c == 'f') return parseLiteral("false", false, out, err);
        if (c == 'n') return parseNull(out, err);
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(out, err);
        return fail(std::string("unexpected character '") + c + "'", err);
    }

    bool atEnd() {
        skipWs();
        return pos_ >= s_.size();
    }

private:
    const std::string& s_;
    std::size_t pos_;

    bool fail(const std::string& msg, std::string* err) {
        if (err) *err = "JSON parse error at offset " + std::to_string(pos_) + ": " + msg;
        return false;
    }
    void skipWs() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' ||
                                    s_[pos_] == '\r')) {
            ++pos_;
        }
    }
    bool expect(char c) {
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }
    bool parseLiteral(const char* lit, bool value, Value* out, std::string* err) {
        const std::size_t n = std::char_traits<char>::length(lit);
        if (s_.compare(pos_, n, lit) != 0) {
            return fail("invalid literal", err);
        }
        pos_ += n;
        *out = Value(value);
        return true;
    }
    bool parseNull(Value* out, std::string* err) {
        if (s_.compare(pos_, 4, "null") != 0) {
            return fail("invalid literal", err);
        }
        pos_ += 4;
        *out = Value{};  // JSON null
        return true;
    }
    bool parseNumber(Value* out, std::string* err) {
        const std::size_t start = pos_;
        if (expect('-')) {
            // ok
        }
        if (pos_ >= s_.size()) return fail("bad number", err);
        while (pos_ < s_.size() && (s_[pos_] >= '0' && s_[pos_] <= '9')) ++pos_;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            while (pos_ < s_.size() && (s_[pos_] >= '0' && s_[pos_] <= '9')) ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            while (pos_ < s_.size() && (s_[pos_] >= '0' && s_[pos_] <= '9')) ++pos_;
        }
        const std::string num = s_.substr(start, pos_ - start);
        if (num.empty() || num == "-") return fail("bad number", err);
        *out = Value(std::strtod(num.c_str(), nullptr));
        return true;
    }
    bool parseString(Value* out, std::string* err) {
        if (!expect('"')) return fail("expected string", err);
        std::string value;
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') {
                *out = Value(std::move(value));
                return true;
            }
            if (c == '\\') {
                if (pos_ >= s_.size()) return fail("bad escape", err);
                const char e = s_[pos_++];
                switch (e) {
                    case '"': value.push_back('"'); break;
                    case '\\': value.push_back('\\'); break;
                    case '/': value.push_back('/'); break;
                    case 'b': value.push_back('\b'); break;
                    case 'f': value.push_back('\f'); break;
                    case 'n': value.push_back('\n'); break;
                    case 'r': value.push_back('\r'); break;
                    case 't': value.push_back('\t'); break;
                    case 'u': {
                        std::uint32_t cp = 0;
                        if (!readHex4(&cp)) return fail("bad \\u escape", err);
                        if (cp >= 0xD800 && cp <= 0xDBFF) {  // high surrogate
                            if (pos_ + 1 >= s_.size() || s_[pos_] != '\\' || s_[pos_ + 1] != 'u') {
                                return fail("missing low surrogate", err);
                            }
                            pos_ += 2;
                            std::uint32_t low = 0;
                            if (!readHex4(&low) || low < 0xDC00 || low > 0xDFFF) {
                                return fail("bad low surrogate", err);
                            }
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                        appendUtf8(value, cp);
                        break;
                    }
                    default:
                        return fail("bad escape character", err);
                }
            } else {
                if (static_cast<unsigned char>(c) < 0x20) return fail("control char in string", err);
                value.push_back(c);
            }
        }
        return fail("unterminated string", err);
    }
    bool readHex4(std::uint32_t* out) {
        if (pos_ + 4 > s_.size()) return false;
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
        }
        *out = v;
        return true;
    }
    static void appendUtf8(std::string& out, std::uint32_t cp) {
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
    bool parseArray(Value* out, std::string* err) {
        if (!expect('[')) return fail("expected '['", err);
        std::vector<Value> arr;
        skipWs();
        if (expect(']')) {
            *out = Value(std::move(arr));
            return true;
        }
        for (;;) {
            skipWs();
            Value v;
            if (!parseValue(&v, err)) return false;
            arr.push_back(std::move(v));
            skipWs();
            if (expect(']')) break;
            if (!expect(',')) return fail("expected ',' or ']'", err);
        }
        *out = Value(std::move(arr));
        return true;
    }
    bool parseObject(Value* out, std::string* err) {
        if (!expect('{')) return fail("expected '{'", err);
        std::vector<std::pair<std::string, Value>> obj;
        skipWs();
        if (expect('}')) {
            *out = Value(std::move(obj));
            return true;
        }
        for (;;) {
            skipWs();
            Value key;
            if (!parseString(&key, err)) return fail("expected object key", err);
            skipWs();
            if (!expect(':')) return fail("expected ':'", err);
            skipWs();
            Value val;
            if (!parseValue(&val, err)) return false;
            obj.emplace_back(key.asString(), std::move(val));
            skipWs();
            if (expect('}')) break;
            if (!expect(',')) return fail("expected ',' or '}'", err);
        }
        *out = Value(std::move(obj));
        return true;
    }
};

} // namespace

bool parse(const std::string& text, Value* out, std::string* err) {
    if (out == nullptr) {
        if (err) *err = "null output pointer";
        return false;
    }
    Parser p(text);
    if (!p.parseValue(out, err)) {
        return false;
    }
    if (!p.atEnd()) {
        if (err) *err = "JSON parse error: trailing data after document";
        return false;
    }
    return true;
}

} // namespace shadowse::json
