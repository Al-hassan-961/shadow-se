// SPDX-License-Identifier: MIT
// Shadow SE - minimal JSON value type with parser and serializer.
//
// Supports null, bool, number, string, array and object. Objects preserve
// insertion order. The parser handles standard escapes including \uXXXX and
// surrogate pairs. Used by the HTTP/JSON gateway; kept dependency-free.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace shadowse::json {

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Value();                    // null
    Value(bool b);              // Bool
    Value(int n);               // Number
    Value(double n);            // Number
    Value(const char* s);       // String
    Value(std::string s);       // String
    Value(std::vector<Value> a);                 // Array
    Value(std::vector<std::pair<std::string, Value>> o);  // Object

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }

    bool asBool(bool def = false) const;
    double asNumber(double def = 0.0) const;
    const std::string& asString(const std::string& def = {}) const;

    const std::vector<Value>& array() const;
    const std::vector<std::pair<std::string, Value>>& object() const;

    // Array / object building.
    void push(Value v);
    Value& operator[](const std::string& key);          // creates if absent
    const Value* find(const std::string& key) const;    // nullptr if absent

    // Serialization.
    std::string dump(bool pretty = false) const;

private:
    Type type_;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    std::vector<std::pair<std::string, Value>> object_;
};

// Parses a complete JSON document. On failure returns false and sets `err`.
bool parse(const std::string& text, Value* out, std::string* err);

// Escapes a string for embedding in JSON ("" backslashes, controls).
std::string escapeString(const std::string& s);

} // namespace shadowse::json
